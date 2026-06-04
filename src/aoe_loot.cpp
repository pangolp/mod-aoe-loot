/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "aoe_loot.h"

std::map<uint64, bool> AoeLootCommandScript::playerAoeLootEnabled;

bool AutoTakeCreatureLoot(Player* player, Creature* creature)
{
    if (!creature || !creature->HasDynamicFlag(UNIT_DYNFLAG_LOOTABLE))
        return false;

    if (!player->isAllowedToLoot(creature))
        return false;

    Loot* loot = &creature->loot;

    // Ensure per-player item tracking is initialised (no-op if already done
    // for this player at creature death via Loot::FillLoot).
    loot->FillNotNormalLootFor(player);

    // Temporarily redirect the player's active loot GUID so StoreLootItem
    // internal checks operate against this creature.
    ObjectGuid savedLootGuid = player->GetLootGUID();
    player->SetLootGUID(creature->GetGUID());
    loot->AddLooter(player->GetGUID());

    bool tookAnything = false;
    uint32 const maxSlots = loot->GetMaxSlotInLootFor(player);
    for (uint32 slot = 0; slot < maxSlots; ++slot)
    {
        if (slot > std::numeric_limits<uint8>::max())
            break;

        InventoryResult msg;
        if (player->StoreLootItem(static_cast<uint8>(slot), loot, msg))
            tookAnything = true;
    }

    loot->RemoveLooter(player->GetGUID());
    player->SetLootGUID(savedLootGuid);

    if (loot->isLooted())
    {
        creature->AllLootRemovedFromCorpse();
        creature->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
    }

    return tookAnything;
}

void AOELootPlayer::OnPlayerLogin(Player* player)
{
    if (!player)
        return;

    if (sConfigMgr->GetOption<bool>("AOELoot.Enable", true) && sConfigMgr->GetOption<bool>("AOELoot.Message", true))
        if (WorldSession* session = player->GetSession())
            ChatHandler(session).PSendModuleSysMessage(MODULE_STRING, AOE_LOGIN_MESSAGE);
}

void AOELootPlayer::OnPlayerCreatureLootOpened(Player* player, Creature* mainCreature)
{
    if (!player || !mainCreature)
        return;

    if (!sConfigMgr->GetOption<bool>("AOELoot.Enable", true))
        return;

    // Per-player toggle
    uint64 const playerGuid = player->GetGUID().GetRawValue();
    if (AoeLootCommandScript::hasPlayerAoeLootEnabled(playerGuid) &&
        !AoeLootCommandScript::getPlayerAoeLootEnabled(playerGuid))
        return;

    // Respect group config option
    if (player->GetGroup() && !sConfigMgr->GetOption<bool>("AOELoot.Group", true))
        return;

    // If this player opened a corpse whose round-robin slot belongs to another
    // group member, immediately auto-take for the rightful owner. The hook fires
    // after SMSG_LOOT_RESPONSE is sent, so the opener already sees the items in
    // their client window — but all item takes are validated server-side. By
    // consuming the items for the owner first (synchronously, before any
    // CMSG_AUTOSTORE_LOOT_ITEM can arrive), the opener's subsequent take
    // attempts will find the slots already marked is_looted and be rejected.
    if (Group* group = player->GetGroup())
    {
        if (mainCreature->GetLootRecipientGroup() == group)
        {
            LootMethod const method = group->GetLootMethod();
            if (method == GROUP_LOOT || method == ROUND_ROBIN || method == NEED_BEFORE_GREED)
            {
                ObjectGuid const rrGuid = mainCreature->loot.roundRobinPlayer;
                if (rrGuid && rrGuid != player->GetGUID())
                {
                    if (Player* owner = ObjectAccessor::FindConnectedPlayer(rrGuid))
                        if (owner->GetMap() == mainCreature->GetMap())
                            AutoTakeCreatureLoot(owner, mainCreature);
                }
            }
        }
    }

    float range = sConfigMgr->GetOption<float>("AOELoot.Range", 55.0f);
    if (range < 5.0f)   range = 5.0f;
    if (range > 100.0f) range = 100.0f;

    size_t const maxCorpses = static_cast<size_t>(sConfigMgr->GetOption<uint32>("AOELoot.MaxCorpses", 20));

    std::list<Creature*> nearbyCorpses;
    player->GetDeadCreatureListInGrid(nearbyCorpses, range);

    nearbyCorpses.remove_if([&](Creature* c)
    {
        if (!c || c->GetGUID() == mainCreature->GetGUID())
            return true;

        if (!c->HasDynamicFlag(UNIT_DYNFLAG_LOOTABLE))
            return true;

        // isAllowedToLoot checks group membership and whether the player has
        // at least one item available (including quest items). It is a
        // necessary but not sufficient condition for auto-take: it returns
        // true for a player who has quest items on another player's
        // round-robin corpse, which would cause AutoTakeCreatureLoot to
        // take regular grey items that belong to the other player (because
        // StoreLootItem does not block round-robin items in GROUP_LOOT mode
        // — that protection is enforced client-side via LOOT_SLOT_TYPE_LOCKED).
        if (!player->isAllowedToLoot(c))
            return true;

        // For group loot methods that distribute via round-robin, only
        // auto-collect corpses where this player is the designated looter.
        // roundRobinPlayer is set in Loot::FillLoot at creature death
        // (Unit::Kill → Group::UpdateLooterGuid → SetLootRecipient →
        // FillLoot:569) and correctly reflects the group's rotation.
        if (Group* group = player->GetGroup())
        {
            if (c->GetLootRecipientGroup() == group)
            {
                LootMethod const method = group->GetLootMethod();
                if (method == GROUP_LOOT || method == ROUND_ROBIN || method == NEED_BEFORE_GREED)
                {
                    ObjectGuid const rrPlayer = c->loot.roundRobinPlayer;
                    if (rrPlayer && rrPlayer != player->GetGUID())
                        return true;
                }
            }
        }

        return false;
    });

    size_t processed = 0;
    for (Creature* corpse : nearbyCorpses)
    {
        if (processed >= maxCorpses)
            break;

        if (AutoTakeCreatureLoot(player, corpse))
            ++processed;
    }
}

ChatCommandTable AoeLootCommandScript::GetCommands() const
{
    static ChatCommandTable aoeLootSubCommandTable =
    {
        { "on",  HandleAoeLootOnCommand,  SEC_PLAYER, Console::No },
        { "off", HandleAoeLootOffCommand, SEC_PLAYER, Console::No }
    };

    static ChatCommandTable aoeLootCommandTable =
    {
        { "aoeloot", aoeLootSubCommandTable }
    };

    return aoeLootCommandTable;
}

bool AoeLootCommandScript::hasPlayerAoeLootEnabled(uint64 guid)
{
    return playerAoeLootEnabled.count(guid) > 0;
}

bool AoeLootCommandScript::getPlayerAoeLootEnabled(uint64 guid)
{
    auto it = playerAoeLootEnabled.find(guid);
    if (it != playerAoeLootEnabled.end())
        return it->second;
    return false;
}

void AoeLootCommandScript::setPlayerAoeLootEnabled(uint64 guid, bool mode)
{
    playerAoeLootEnabled[guid] = mode;
}

bool AoeLootCommandScript::HandleAoeLootOnCommand(ChatHandler* handler, Optional<std::string> /*args*/)
{
    Player* player = handler->GetSession()->GetPlayer();
    if (!player)
        return true;

    uint64 const playerGuid = player->GetGUID().GetRawValue();

    if (AoeLootCommandScript::hasPlayerAoeLootEnabled(playerGuid) &&
        AoeLootCommandScript::getPlayerAoeLootEnabled(playerGuid))
    {
        handler->PSendModuleSysMessage(MODULE_STRING, AOE_LOOT_ALREADY_ENABLED);
        return true;
    }

    AoeLootCommandScript::setPlayerAoeLootEnabled(playerGuid, true);
    handler->PSendModuleSysMessage(MODULE_STRING, AOE_LOOT_ENABLED);
    return true;
}

bool AoeLootCommandScript::HandleAoeLootOffCommand(ChatHandler* handler, Optional<std::string> /*args*/)
{
    Player* player = handler->GetSession()->GetPlayer();
    if (!player)
        return true;

    uint64 const playerGuid = player->GetGUID().GetRawValue();

    if (AoeLootCommandScript::hasPlayerAoeLootEnabled(playerGuid) &&
        !AoeLootCommandScript::getPlayerAoeLootEnabled(playerGuid))
    {
        handler->PSendModuleSysMessage(MODULE_STRING, AOE_LOOT_ALREADY_DISABLED);
        return true;
    }

    AoeLootCommandScript::setPlayerAoeLootEnabled(playerGuid, false);
    handler->PSendModuleSysMessage(MODULE_STRING, AOE_LOOT_DISABLED);
    return true;
}
