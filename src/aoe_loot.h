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

#ifndef MODULE_AOELOOT_H
#define MODULE_AOELOOT_H

#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "Player.h"
#include "ScriptMgr.h"
#include <list>
#include <map>
#include <string>

#define MODULE_STRING "mod-aoe-loot"

using namespace Acore::ChatCommands;

enum AoeLootString
{
    AOE_LOGIN_MESSAGE        = 1,
    AOE_ITEM_IN_THE_MAIL     = 2, // unused
    AOE_MODULE_ACTIVE        = 3, // unused
    AOE_QUEST_ITEM_SENT      = 4, // unused
    AOE_LOOT_ALREADY_ENABLED = 5,
    AOE_LOOT_ENABLED         = 6,
    AOE_LOOT_ALREADY_DISABLED = 7,
    AOE_LOOT_DISABLED        = 8
};

class AOELootPlayer : public PlayerScript
{
public:
    AOELootPlayer() : PlayerScript("AOELootPlayer",
        { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_CREATURE_LOOT_OPENED }) {}

    void OnPlayerLogin(Player* player) override;

    // Fires after Player::SendLoot grants permission for a creature, once
    // roundRobinPlayer is set and the loot window has been sent to the client.
    void OnPlayerCreatureLootOpened(Player* player, Creature* mainCreature) override;
};

class AoeLootCommandScript : public CommandScript
{
public:
    AoeLootCommandScript() : CommandScript("AoeLootCommandScript") {}
    ChatCommandTable GetCommands() const override;

    static bool HandleAoeLootOnCommand(ChatHandler* handler, Optional<std::string> args);
    static bool HandleAoeLootOffCommand(ChatHandler* handler, Optional<std::string> args);

    static bool getPlayerAoeLootEnabled(uint64 guid);
    static void setPlayerAoeLootEnabled(uint64 guid, bool mode);
    static bool hasPlayerAoeLootEnabled(uint64 guid);

private:
    static std::map<uint64, bool> playerAoeLootEnabled;
};

void AddSC_AoeLoot()
{
    new AOELootPlayer();
    new AoeLootCommandScript();
}

#endif // MODULE_AOELOOT_H
