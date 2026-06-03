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

        // isAllowedToLoot covers all ownership and group-loot-method rules:
        // it checks roundRobinPlayer (set at creature death), allowedGUIDs
        // (post-roll assignment), group membership, and loot type. Any
        // corpse that passes this check has items the player is entitled
        // to take under the server's own loot rules.
        if (!player->isAllowedToLoot(c))
            return true;

        return false;
    });

    size_t processed = 0;
    for (Creature* corpse : nearbyCorpses)
    {
        if (processed >= maxCorpses)
            break;

        if (player->AutoTakeCreatureLoot(corpse))
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
