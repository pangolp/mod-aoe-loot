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
#include "ObjectMgr.h"
#include <algorithm>
#include <limits>

std::map<uint64, bool> AoeLootCommandScript::playerAoeLootEnabled;

void AOELootPlayer::OnPlayerLogin(Player* player)
{
    if (!player)
        return;

    if (sConfigMgr->GetOption<bool>("AOELoot.Enable", true) && sConfigMgr->GetOption<bool>("AOELoot.Message", true))
        if (WorldSession* session = player->GetSession())
            ChatHandler(session).PSendModuleSysMessage(MODULE_STRING, AOE_LOGIN_MESSAGE);
}

bool AOELootServer::CanPacketReceive(WorldSession* session, WorldPacket const& packet)
{
    // Only handle loot packets
    if (packet.GetOpcode() != CMSG_LOOT)
        return true;

    // Basic validation checks
    if (!session)
        return true;

    Player* player = session->GetPlayer();
    if (!player)
        return true;

    // Check if module is enabled
    if (!sConfigMgr->GetOption<bool>("AOELoot.Enable", true))
        return true;

    // Check if player has AOE loot disabled via command
    uint64 playerGuid = player->GetGUID().GetRawValue();
    if (AoeLootCommandScript::hasPlayerAoeLootEnabled(playerGuid) &&
        !AoeLootCommandScript::getPlayerAoeLootEnabled(playerGuid))
        return true;

    // Check group settings
    if (player->GetGroup() && !sConfigMgr->GetOption<bool>("AOELoot.Group", true))
        return true;

    // Get configured loot range
    float range = sConfigMgr->GetOption<float>("AOELoot.Range", 55.0f);

    // Limit range to reasonable values
    if (range < 5.0f)
        range = 5.0f;

    if (range > 100.0f)
        range = 100.0f;

    // Read target GUID from packet
    WorldPacket packetCopy(packet);
    ObjectGuid targetGuid;
    packetCopy >> targetGuid;

    if (!targetGuid)
        return true;

    // Get target creature
    Creature* mainCreature = player->GetMap()->GetCreature(targetGuid);
    if (!mainCreature)
        return true;

    // Check if main creature has loot
    if (!mainCreature->HasDynamicFlag(UNIT_DYNFLAG_LOOTABLE))
        return true;

    // Get nearby corpses
    std::list<Creature*> nearbyCorpses;
    player->GetDeadCreatureListInGrid(nearbyCorpses, range);

    // Remove invalid corpses and main target
    nearbyCorpses.remove_if([&](Creature* c)
        {
            if (!c || c->GetGUID() == targetGuid || !c->HasDynamicFlag(UNIT_DYNFLAG_LOOTABLE))
                return true;

            if (!player->isAllowedToLoot(c))
                return true;

            // For loot methods that assign per-corpse ownership (GROUP_LOOT, ROUND_ROBIN,
            // NEED_BEFORE_GREED), only merge corpses where this player is the tap
            // recipient. Corpses tapped by other group members are left for them to loot
            // directly; they must not be consumed by a different player's AOE pass.
            //
            // NOTE: roundRobinPlayer is set lazily (on first SendLoot call), so it is
            // empty for unopened corpses and cannot be used to determine ownership here.
            // GetLootRecipient() reflects the player who held the tap at creature death
            // and is the correct ownership signal at this stage.
            if (Group* group = player->GetGroup())
            {
                if (c->GetLootRecipientGroup() == group)
                {
                    LootMethod const method = group->GetLootMethod();
                    if (method == ROUND_ROBIN || method == GROUP_LOOT || method == NEED_BEFORE_GREED)
                        return c->GetLootRecipient() != player;
                }
            }

            return false;
        });

    // If no other corpses, process normally
    if (nearbyCorpses.empty())
    {
        player->SendLoot(targetGuid, LOOT_CORPSE);
        return false;
    }

    // Determines whether the current player is eligible to take a specific item
    // from a source creature's loot.
    //
    // Corpse-level ownership (GROUP_LOOT / ROUND_ROBIN / NEED_BEFORE_GREED) is
    // already enforced by the nearbyCorpses filter above using GetLootRecipient().
    // This lambda handles only per-item checks that apply after that filter:
    //   - is_blocked  : active roll in flight — never touch.
    //   - freeforall  : personal drop, any eligible player may take a copy.
    //   - allowedGUIDs: post-roll assignment to specific players.
    //   - standard    : for solo kills verify direct ownership;
    //                   for group kills the filter already guarantees the player
    //                   is the correct recipient, so membership check is enough.
    auto isEligibleForPlayer = [&](LootItem const& item, Creature* src) -> bool
    {
        // Never touch items with an active Need/Greed roll. The roll system owns
        // these until it resolves and populates allowedGUIDs with the winner.
        // Merging a blocked item would mark it as looted in the source while the
        // roll is still in flight, making it disappear for everyone.
        if (item.is_blocked)
            return false;

        // FFA items: any eligible player may take their own copy
        if (item.freeforall)
            return true;

        // Items assigned to specific players (e.g., after a Need/Greed roll result)
        if (!item.allowedGUIDs.empty())
            return item.allowedGUIDs.count(player->GetGUID()) > 0;

        // Standard items: solo kill must match the tap recipient directly;
        // group kill corpses that reach here have already been filtered to this
        // player's tap, so group membership is sufficient.
        Group* recipientGroup = src->GetLootRecipientGroup();
        if (!recipientGroup)
            return src->GetLootRecipient() == player;

        return recipientGroup == player->GetGroup();
    };

    // Get main loot
    Loot* mainLoot = &mainCreature->loot;

    // Use configured max corpses value
    size_t const maxCorpses = static_cast<size_t>(sConfigMgr->GetOption<uint32>("AOELoot.MaxCorpses", 20));
    size_t processedCorpses = 0;

    // Track total gold to merge
    uint32 totalGold = mainLoot->gold;

    // Collect items to merge (don't modify main loot directly yet)
    std::vector<LootItem> itemsToAdd;
    std::vector<LootItem> questItemsToAdd;

    for (Creature* creature : nearbyCorpses)
    {
        if (processedCorpses >= maxCorpses)
            break;

        if (!creature)
            continue;

        Loot* loot = &creature->loot;

        // Skip already looted corpses
        if (loot->isLooted())
            continue;

        // Merge gold. In group loot, gold is taken by whoever opens the corpse
        // first, consistent with vanilla round-robin behavior.
        if (loot->gold > 0)
        {
            if (totalGold < (std::numeric_limits<uint32>::max() - loot->gold))
                totalGold += loot->gold;
            loot->gold = 0;
        }

        // Surgically collect only the items this player is eligible for.
        // Items that belong to other group members (different round-robin slot,
        // specific allowedGUIDs, etc.) are skipped so the corpse remains
        // lootable for the rightful owner.
        bool itemsRemainingForOthers = false;
        for (auto& srcItem : loot->items)
        {
            if (srcItem.is_looted)
                continue;

            if (!isEligibleForPlayer(srcItem, creature))
            {
                itemsRemainingForOthers = true;
                continue;
            }

            if ((mainLoot->items.size() + itemsToAdd.size() +
                 mainLoot->quest_items.size() + questItemsToAdd.size()) >= MAX_LOOT_ITEMS)
                break;

            itemsToAdd.push_back(srcItem);

            // Mark item as taken in the source so the loot state stays consistent
            srcItem.is_looted = true;
            if (loot->unlootedCount > 0)
                --loot->unlootedCount;
        }

        // Collect quest items (per-player, capped to how many the player still needs)
        for (size_t i = 0; i < loot->quest_items.size(); ++i)
        {
            if ((mainLoot->items.size() + itemsToAdd.size() +
                 mainLoot->quest_items.size() + questItemsToAdd.size()) >= MAX_LOOT_ITEMS)
                break;

            LootItem const& questItem = loot->quest_items[i];

            if (questItem.is_looted)
                continue;

            // Skip items the player doesn't need for any active quest
            if (!player->HasQuestForItem(questItem.itemid))
                continue;

            // Calculate how many the player still needs across all active quests
            uint32 maxNeeded = 0;
            for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            {
                uint32 questId = player->GetQuestSlotQuestId(slot);
                if (!questId)
                    continue;

                Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
                if (!quest)
                    continue;

                for (uint8 j = 0; j < QUEST_ITEM_OBJECTIVES_COUNT; ++j)
                {
                    if (quest->RequiredItemId[j] == questItem.itemid && quest->RequiredItemCount[j] > maxNeeded)
                        maxNeeded = quest->RequiredItemCount[j];
                }
            }

            if (maxNeeded == 0)
                continue;

            // Count how many the player already has, plus items pending delivery
            uint32 ownedCount = player->GetItemCount(questItem.itemid, true);
            for (auto const& pending : questItemsToAdd)
            {
                if (pending.itemid == questItem.itemid)
                    ownedCount += pending.count;
            }
            for (auto const& mainQuestItem : mainLoot->quest_items)
            {
                if (mainQuestItem.itemid == questItem.itemid)
                    ownedCount += mainQuestItem.count;
            }

            if (ownedCount >= maxNeeded)
                continue;

            uint32 stillNeeded = maxNeeded - ownedCount;
            LootItem cappedItem = questItem;
            cappedItem.count = std::min(static_cast<uint32>(questItem.count), stillNeeded);
            questItemsToAdd.push_back(cappedItem);
        }

        // Only remove the lootable flag when there is truly nothing left for anyone.
        // If other group members still have eligible items, the corpse must remain
        // lootable so they can claim what belongs to them.
        if (!itemsRemainingForOthers && loot->isLooted())
        {
            creature->AllLootRemovedFromCorpse();
            creature->RemoveDynamicFlag(UNIT_DYNFLAG_LOOTABLE);
        }

        processedCorpses++;
    }

    // Apply merged gold to the main loot window
    mainLoot->gold = totalGold;

    // Add eligible regular items to the main loot window
    for (LootItem const& item : itemsToAdd)
    {
        if (mainLoot->items.size() < MAX_LOOT_ITEMS)
            mainLoot->items.push_back(item);
    }

    // Deliver quest items directly to player inventory
    for (LootItem const& item : questItemsToAdd)
    {
        if (!player->HasQuestForItem(item.itemid))
            continue;

        player->AddItem(item.itemid, item.count);
    }

    // Send merged loot window
    player->SendLoot(targetGuid, LOOT_CORPSE);

    return false;
}

ChatCommandTable AoeLootCommandScript::GetCommands() const
{
    static ChatCommandTable aoeLootSubCommandTable =
    {
        { "on", HandleAoeLootOnCommand, SEC_PLAYER, Console::No },
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

    uint64 playerGuid = player->GetGUID().GetRawValue();

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

    uint64 playerGuid = player->GetGUID().GetRawValue();

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
