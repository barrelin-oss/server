# Helbreath Olympia: what the client folder holds, and what we took

Source: `C:\Users\Jorge Barrelin\AppData\Roaming\Helbreath Olympia` (client of the Olympia
private server, patch 18.2.3b, 2016-2022 development). No server code or configs; everything
here was read from the client's assets, its `contents/` data files, the patch notes it caches
(`news.json`) and the strings inside `OlympiaGame.exe`. Numbers and rules below are therefore
what the client shows or the patch notes state, not the server formulas.

## 1. Taken (done)

| What | Where it went | Notes |
|---|---|---|
| 79 sprite packs (.opk) | `client/bin/assets/sprites/*.pak` via `client/tools/opk2pak.mjs` | equipment, NPCs, UI, tiles; all registered (equipment tables, monster table 70-112, tile registry) |
| 16 maps (.amd) | client + server `bin/mapdata` | arena1-9, astoria, huntzone5/6, oldelvine, village, wzdtwr_1f/2f; no spawners yet |
| Music (.wav) | client falls back to .wav | title screen plays MainTm |
| Tile/object ids of the Olympia packs | `tile_sprite_registry.cpp` | lgn_objects 249, lgn_maptiles 315-319 |
| 33 quests + 14 persons | `quests.yaml` ids 200-231, `npcs.yaml`, `dialogs.yaml`, `bin/mapdata/*.yaml` | see section 3; checked live at Enzu, Daara and the others |
| 69 item descriptions | `items.yaml` `description:` | of the 147 Olympia texts, the ones whose item exists here; the client does not show them yet |
| 19 NPC templates | `npcs.yaml` sprites 100-112 | Scarecrow, Ghost, Bat, chests, guard variants, officers, Princess, Black-Beholder |

## 2. Data files in `contents/` (all readable)

| File | Content | Use |
|---|---|---|
| `items.dat` | 678 items: id, name, description, 48-byte stat block (binary, header `0b 00 00 00`, hash, count) | names and descriptions extracted (scratch `olympia-items.json`); stat block not decoded |
| `quests.json` | 33 quests: person, texts, required/obsolete level, period (daily), objectives kill/gather with map and elite flag, rewards exp/contribution/items/item_pick | ported (section 3) |
| `persons.json` | 14 named NPCs with spawns per nation and actions (buy_potions, sell_items, rest, city_hall_menu, cash_shop, mailbox, dk_shop, rebirth); two are "elite" monsters that give quests | ported |
| `specialties.json` | 29 monsters: base_kills and a bonus ladder (damage, damage_pct, damage_reduction[_pct], hit_ratio[_pct], drop_rate) | mechanic, not ported (section 4) |
| `magic.csv`, `magiccfg.txt` | 90 spells: type, duration, mana, area, damage, int requirement, cost, category, element | balance reference; ours has 66 spells in `magic.yaml` |
| `bitemcfg.txt` (85 BuildItem), `citemcfg.txt` (25 CraftItem), `craftitem.csv`, `alchemy.csv` (18) | forge / craft / alchemy recipes with skill limits, difficulty and "chance to lose" | ours: 92 build, 14 craft, 14 alchemy recipes; Olympia adds necklace upgrade chains (DF/DM/MR/MS) and socket gems |
| `windows.json` | layout of every client window (achievements, enchanting, market, rebirth, talents, alchemy, manufacture, crafting, gm_panel, upgrade, level_set...) | UI reference |
| `help.js` | help book: world, movement, combat, interface, magic, skill system | text for our help/F1 |
| `freya.js` | tutorial script (Freya, the beginner guide): text nodes, `reach_pos`, `reach_level` steps, `/resetfreya` | tutorial design |
| `gamemsglist.txt` | crusade/war messages (grand magic generator, meteor strike, building limits, recall points) | text |
| `news.json` | 36 patch notes 8.1 (2016) to 18.1 (2022) | the best description of the mechanics (section 4) |
| `contents*.txt` | terms, help pages by id | text |
| `beta/locale.csv` | 2789 UI keys in English, Spanish, Japanese, Korean, Polish, Swedish, French | not our keys; usable as a translation table for the same wording |

Not usable: sounds (`.snd`, closed format), `create_acc.opk` (other layout), `inappdata` (empty),
14 weapon packs that do not fit our 64-per-weapon id layout (flamberg, sunofpower, underhammer,
DGH/DGN/DRH/DRN swords, Kloness hammer, L/S axe and sword, ShortSword02, lgn_Staff4).

## 3. The Olympia quest line, as ported

Named givers instead of the city hall officers. Loader additions (`quest_loader.cpp`):
`giver`, `giver_map`, `name`, `description`, `target_type2`/`max_count2`, `gather_item[_2,_3]`/
`gather_count[_2,_3]`, and row type 2 = gather only.

| Person | Base figure | Where | Quests |
|---|---|---|---|
| Enzu | archer guard | default (119,46), arefarm (49,97), elvfarm (118,150) | Humble Beginning (50 Slimes), A New Challenge (200 Orcs), Epidemy (3 Snake Teeth/Skin/Tongue) |
| Daara, Oxyia, Lagus | guards | middled1n | 300 Orcs; (elite Orcs, not ported); 200 Scorpions + 5 Scorpion Skins |
| Lysio / Lisyo | archer guard | areuni / elvuni | Garden Trolls 800, daily 400+400 Orcs, Garden Unicorns 100, daily 40 |
| Irenicus | Gandalf | middleland (179,225) | Trolls 1200, Cyclops 800, Ogres 500 (+daily 200), Werewolves 500 (+daily 200) |
| Litzy | Lizard | dglv2 (266,150) | D2 Cyclops 1000 |
| Fooldya | Wyvern | icebound (212,42) | Ice Golems 700/300, Beholders 600/250, Frosts 600/250 |
| Moeru | Fire-Wyvern | toh3 (234,252) | Demons 200/80, Gargoyles 150/60, Dark Elves 500/200 |

Scaling: Olympia exp / 50, Olympia gold / 5 (Olympia caps at level 140 with rebirths; exp
rewards there run 20k to 2M). Dropped: elite kills (no elites here), Event Token rewards, the
item choice on turn-in (Humble Beginning gives a Dagger), Kiora the shopkeeper and the city hall
persons (our own exist). "Daily" quests are simply repeatable here; Olympia's `period: 20`
(hours) has no equivalent yet.

## 4. Mechanics of Olympia (from the patch notes and the client strings)

Ordered by how much they would add here and how contained they are.

1. **Specialties (monster mastery)** - data in `specialties.json`. Per monster type, every
   `base_kills` kills (scaled: 20 for gargoyles, 150 for slimes) unlocks the next bonus of the
   ladder against that monster: +damage, +damage %, damage reduction (flat or %), hit ratio
   (flat or %), drop rate. Kills are shared among the attackers or the party, like quest kills;
   percentage bonuses multiply. Client: "Mob levels" window, titles from it. Fits our kill
   tracking; needs a per-character counter table, bonus application in combat/loot, a
   protocol push and a window.
2. **Talents** - earned at fixed levels ("You earn a talent at levels: ..."), one picked at
   start, a second later; unlearn with a ticket. Known talents: Xelima (max HP -%), Merien
   (+Str/+Mag), Tank (+Vit), Archery (Str-based bow damage, dash at 100%), exclusive spells
   learned with a talent, crit-related ("Max crits: Lvl / n"). Rebirth Mode swaps the set.
3. **Rebirth / Majestics** - at max level (140) a character can rebirth (7 Zemstones of
   Rebirth); rebirth levels count for map gates; Majestic points are earned and spent (switch
   Rebirth/Normal mode for 1 Majestic + 20k gold, cash shop). Achievements "There and Back
   Again", "True Phoenix", "Majestic".
4. **Enchanting / disenchanting** - first stat needs shards, second needs fragments; shards and
   fragments come from disenchanting (destroys the item) and can be combined/upgraded by level;
   requires Enchanting skill %; an Enchanting Bag stores ingredients; disenchanted items can be
   recovered for 2% skill. Window with tabs.
5. **Sockets and gems** - one socket per statted item ("Use on a non-socketed statted item to
   give one socket"), gems give attributes, Vortex Gem, unsocket via cash shop; gems drop from
   Liches/Ettins, crafted from recipes.
6. **Achievements** - four tabs (General, PvM, PvP, Challenges) with points per category and
   titles: Max Level, Agent of Darkness (max Dark Weapon), There and Back Again, True Phoenix,
   Majestic, Divine Touch (Angelic Pendant), Diplomacy (N quests), Contributor, Merchant, Broker,
   Gold Loot I/II, Enchanter I, First Enemy Kill, kill counts per monster (5,000 Giant Frogs),
   "Wasted Materials".
7. **Guild ranks** - Guildmaster, Captain, Huntmaster and Raidmaster (with Vet/Capt grades),
   Guildsman; Huntmaster raises drop rate, Raidmaster is for raids; ranks auto-release after 15
   min AFK (5 in buildings); members can self-promote when a rank is free; guild summon with
   rules (no summon within 10 s of damage, dead summoner, raiding, map restrictions, cooldown).
8. **Events** - Soccer (Aresden vs Elvine, scored), Siege, Illusion (everyone looks alike),
   Donation event (5 Olympia Coins at City Hall), Halloween night, treasure chests that spawn
   across maps (silver notifies the map, gold shows on the minimap), Apocalypse on a schedule
   (Saturday), Heldenian and Crusade with HUD and commander confirmation, raid days.
9. **Elites** - elite versions of monsters (Orc, Bat with broom...) with their own drops
   (dyes, stones, Renown Scroll, Rebirth Zem, Event Tokens, Olympia Coins); shown on the
   minimap.
10. **Economy** - market/auction ("Your auction is now on the market!"), mailbox at city hall
    and farms, Olympia Coins (donation currency; services: stat/name/guild name change, town
    change, gold, rebirth zems, sex change, dyes), cash shop in buildings, shop discount 10%,
    experience potions and "+N% experience for M minutes", Luck stat (+0.33% crush chance and
    drop chance per point), rested exp.
11. **Stats screen formulas** (client strings): Max HP = Vit*2 + Lvl*2.5 + Str*2/3; Max MP =
    Mag*2 + Lvl*2 + Int/2; Max SP = 10 + Lvl*2 + Str; max weight = Str*5 + Lvl*5; 3 Str = +2 HP,
    1 Str = +1 SP and +5 weight, hands +1 dmg / 20 Str, weapons +0.2% dmg / Str; 1 Vit = +2 HP,
    magic damage reduction (Vit-10)^0.6707/2.5, physical reduction = half of that; 1 Dex = +2
    defense ratio, hit ratio per Dex above threshold; 2 Int = +1 MP, casting probability per 2
    Int above 50; 1 Mag = +2 MP, +0.3% magic damage, +1 magic resistance above threshold.
12. **Quality of life** in the client: `/showalldamage`, `/shownpccasts`, `/bigitems`,
    `/bigtrees`, `/autorelog`, `/autodash`, `/shiftpickup`, `/invertgauges`, `/crusadehud`,
    `/showquest`, `/timestamp`, auto screenshot on enemy kill (`/ekscreenshot`), target lock,
    hardware cursor, quest count on HUD, "Detail Level" setting, gore toggle.

## 5. Suggested order

1. Specialties (data already here; one system, visible in every hunt).
2. Elites (a flag on spawners, stronger stats, own loot table) then the two dropped quests.
3. Daily period on quests (`period` hours) and item choice on turn-in.
4. Achievements (data-driven counters; titles).
5. Enchanting with shards/fragments (touches items, crafting, UI).
6. Talents and rebirth (character progression redesign; last).
