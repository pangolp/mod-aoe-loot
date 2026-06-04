# ![logo](https://raw.githubusercontent.com/azerothcore/azerothcore.github.io/master/images/logo-github.png) AzerothCore

# mod-aoe-loot

[English](README.md) | [Español](README_ES.md)

[![Build Status](https://github.com/azerothcore/mod-aoe-loot/workflows/core-build/badge.svg?branch=master&event=push)](https://github.com/azerothcore/mod-aoe-loot/actions)

## Description

This module enables Area of Effect (AOE) looting for AzerothCore. When a player loots a corpse, all nearby eligible corpses are automatically looted as well — items land directly in the player's bags without requiring individual clicks on each body.

## Requirements

- AzerothCore latest master branch
- Core PR [feat/player-creature-loot-opened-hook](https://github.com/azerothcore/azerothcore-wotlk/pull/XXXX) merged *(adds the `OnPlayerCreatureLootOpened` hook used by this module)*

## Installation

### 1. Apply the core PR

This module depends on the `OnPlayerCreatureLootOpened` hook added to the AzerothCore core. Ensure the linked PR above is merged before compiling.

### 2. Clone the module

```bash
cd <ACoreDir>/modules
git clone https://github.com/azerothcore/mod-aoe-loot.git
```

### 3. Recompile AzerothCore

```bash
cd <ACoreDir>/build
cmake .. && make -j$(nproc) && make install
```

### 4. Configure

Copy `conf/mod_aoe_loot.conf.dist` to your server's config directory and adjust the values as needed (see **Configuration** below).

### 5. Restart the worldserver

---

## How it works

When a player loots any corpse:

1. The server applies its normal loot rules to that corpse (round-robin assignment, roll setup, etc.).
2. The module scans for nearby lootable corpses within the configured range.
3. For each eligible nearby corpse, the module calls the server's internal loot-take routine on behalf of the player. Items the player is entitled to go directly into their bags; the normal loot window remains open only for the corpse the player actually clicked.

Items arrive in inventory with the standard "You receive loot" notification — no extra loot windows, no merging.

---

## Group loot behavior

> **Important for server administrators and players**

This module fully respects the group loot method configured for the party.

### Group Loot / Round Robin / Need Before Greed

The server assigns each corpse to a specific group member via its round-robin rotation (set at creature death, before any player opens the loot). The module honors this assignment:

- **Each player auto-collects only the corpses assigned to them** by the round-robin. Other members' corpses are left untouched and remain lootable.
- **Items go directly to the assigned player's bags**, even if that player has not clicked on the corpse yet. This happens automatically when any group member loots nearby.
- **If a player manually opens a corpse assigned to another member**, the rightful owner's items are immediately and silently sent to their bags before the opener can interact with the loot window. The opener receives nothing from that corpse.

In practice: each player needs to loot at least one corpse to trigger the auto-collection of all the corpses assigned to them. Players will find items appearing in their bags from corpses they never clicked.

### Free For All

All group members collect from all nearby eligible corpses without restriction.

### Master Loot

The master looter auto-collects all nearby eligible corpses.

---

## Player commands

| Command | Description |
|---------|-------------|
| `.aoeloot on` | Enable AOE looting for your character |
| `.aoeloot off` | Disable AOE looting for your character |

> Player preferences reset on logout. AOE loot is enabled by default when the module is active.

---

## Configuration

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `AOELoot.Enable` | Boolean | 1 | Enable or disable the module globally |
| `AOELoot.Message` | Boolean | 1 | Show informational message on player login |
| `AOELoot.Range` | Float | 55.0 | Maximum search radius in yards (5.0 – 100.0) |
| `AOELoot.Group` | Boolean | 1 | Enable AOE loot when the player is in a group |
| `AOELoot.MaxCorpses` | Integer | 20 | Maximum nearby corpses processed per loot trigger (1 – 50) |

### Recommended: faster corpse decay

To avoid visual clutter from looted corpses lingering on the ground, reduce the decay rate in `worldserver.conf`:

```conf
# Default: 0.5 — Recommended for AOE loot: 0.01
Rate.Corpse.Decay.Looted = 0.01
```

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| AOE loot not triggering | Verify `AOELoot.Enable = 1` and `.aoeloot on` for your character |
| Only one corpse looted | Check that the core PR is applied and the server is recompiled |
| Items not going to the right player | Confirm the group loot method is set before combat |
| Corpses not disappearing | Set `Rate.Corpse.Decay.Looted = 0.01` in `worldserver.conf` |

---

## Known limitations

- Player toggle preferences (`.aoeloot on/off`) do not persist across logout/login.
- AOE loot triggers only when a player loots a corpse; idle players near a fight do not collect automatically.

---

## Credits

- **acidmanifesto** — [Original author and concept](https://github.com/azerothcore/mod-aoe-loot/pull/2)
- **AzerothCore Community** — Hooks, updates, and improvements
- **Contributors** — Player commands, multi-language support, and bug fixes

## Links

- **AzerothCore:** [Repository](https://github.com/azerothcore) | [Website](https://azerothcore.org/) | [Discord](https://discord.gg/PaqQRkd)
- **Module Repository:** [GitHub](https://github.com/azerothcore/mod-aoe-loot)
- **Issues & Suggestions:** [Issue Tracker](https://github.com/azerothcore/mod-aoe-loot/issues)

## License

This module is released under the [GNU AGPL v3 License](https://github.com/azerothcore/mod-aoe-loot/blob/master/LICENSE).
