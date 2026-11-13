# Scenes that change Link's age

Game 1.1.1. Found on 13/11/2026 from the game's event scripts (`romfs/Event/EventFlow/*.bfevfl`).
Only the Shadow Medallion case has been seen in the game.

## The problem

Watching the Shadow Medallion scene as a child turns Link into an adult. The scene ends by sending Link to a map
that only exists at one age (`Graveyard_Adult`), and the new map sets his age. The scene doesn't check his age
first, because in normal play he can only reach it as an adult.

Any scene that ends by sending Link to a `_Child` or `_Adult` map without checking his age does the same. If you
watch it at the other age, for example through **warps > arrival cutscenes** or a warp with a boot event, Link
arrives at the destination map's age.

## How they were found

1. Every event script was read with the `evfl` package: 914 files and 5,350 entry points.
2. Each entry point's map changes were collected, including those in the sub-flows it calls. There are 95 map
   changes in all, made with `GameSystemActor` `EventReserveBancChange`, which takes `NextBanc`, `NextStartPos`
   and `BootEventName`.
3. A scene counts if it can reach a map change to a `_Child` or `_Adult` map without passing an age check
   (`Player_Event` `EventQueryPlayerIsChild`, or a query of the `Sequence_IsChildhood` GameData field).
4. **Scenes that save were left out.** A scene was dropped if it, or a script it calls, saves the game or opens a
   game-over or return-to-title screen. The actions that count are:
   - `EventTriggerRequestAutoSave`
   - `EventSaveAndWaitTemporarySave`
   - `EventManualRequestAutoSaveCaption`
   - `EventOpenGameOverScreen`
   - `EventOpenGameOverReturnTitleCheckScreen`

   Where a map change starts another scene (its `BootEventName`), that scene was checked too.

## The list

### Watched as a child, these make Link adult

| Scene | Script entry | Sends Link to |
|---|---|---|
| Rauru and the Light Medallion | `DoorOfTime/SageRoom_MeetRauru` | `Market_Adult` (Temple of Time) |
| Burning Kakariko, then Nocturne of Shadow | `Dm_SY_BancChangeArrive/KakarikoVillage_Arrive`, then `…/KakarikoVillage_FlashbackScenes` | `KakarikoVillage_Adult`, then `KakarikoVillage_Adult_Alternative` |
| Losing to a Gerudo guard at the fortress | `SageOfSpirit_RescueCarpenter/GerudoFortress_MiddleBossLose`, `…Lose2`, `…Lose3`, `…Lose4`, `…/GerudoFortress_GerudoThiefLose` | `GerudoValley_Adult` |

### Watched as an adult, these make Link a child

| Scene | Script entry | Sends Link to |
|---|---|---|
| Deku Tree blue warp and the Deku Tree's death | `Dm_SY_BancChange_EscapeDungeon/BossArea_WarpIn`, then `Dm_Kokiri_0170` | `KokiriForest_Child` |
| Zora's Sapphire (Ruto after Jabu-Jabu), and leaving Jabu-Jabu | `Dm_InsideJabu_Jabu_0050` (`EntryPoint0`, `6`–`9`), `Dm_SY_BancChange_EscapeDungeon/Get_StoneOfWater_LeaveFromJabuJabuBelly`, then `Dm_ZoraFountain_0040` | `ZoraFountain_Child` |
| Impa sending Link off after Zelda's Lullaby | `DoorOfTime/Dm_MelodyGet_Zelda_After_Npc_Hylia_Impa_Child_Talk`, `DoorOfTime/DoorOfTime_AreaOut` | `HyruleField_Child` |
| Kaepora Gaebora carrying Link down from Death Mountain | `Npc_Kaepora_DeathMountainTrail/Talk`, then `Dm_DeathMtTrail_0035` | `KakarikoVillage_Child` |

**Not fully checked.** Three of these start a scene after the map change that isn't an entry point in any event
script, so it couldn't be checked for a save. The map probably starts it itself:
- `KokiriForest_WarpFromInsideTheDekuTree`, after the Deku Tree warp
- `Get_StoneOfWater_Complete`, at Zora's Fountain
- `HyruleField_DoorOfTime_SendOff`, after Impa

If one of them gives a Spiritual Stone, it probably saves, and that row should go. The Zora's Fountain one is the
most likely.

## Left out because they save

These scenes also change Link's age, but they save the game.

| Scene | Script entry | Sends Link to | The save |
|---|---|---|---|
| Forest Medallion (Saria), and the Forest Temple blue warp | `Dm_Awake_Forest_Get`; `Dm_SY_BancChange_EscapeDungeon/ForestTemple_SageOfForest_SageRoom`, `…_EscapeFromTemple` | `KokiriForest_Adult` | the medallion is given through `GetPouchContent`, which autosaves |
| Fire Medallion (Darunia), and the Fire Temple blue warp | `Dm_Awake_Fire_Get`; `…/FireTemple_SageOfFire_SageRoom`, `…/SageOfFire_ClearEvent`, `…/FireTemple_SageOfFire_Kakariko` | `DeathMountainCrater_Adult`; one branch goes to `KakarikoVillage_Adult` | as above |
| Water Medallion (Ruto), and the Water Temple blue warp | `Dm_Awake_Water_Get`; `…/SageOfWater_SageRoom`, `…/SageOfWater_ClearEvent` | `HyruleField_Adult` (`WaterSerenade`) | as above |
| **Shadow Medallion (Impa)**, and the Bongo Bongo blue warp | `Dm_Awake_Shadow_Get`; `…/ShadowTemple_Warp_ChamberOfSages`, `…/ShadowTemple_BossArea_WarpIn` | `Graveyard_Adult` (`ShadowNocturne`) | as above |
| Spirit Medallion (Nabooru), and the Spirit Temple exit | `Dm_Awake_Spirit_Get`; `…/SpiritTemple_SageOfSpirit_SageRoom`, `…/SageOfSpirit_Clear` | `DesertColossus_Adult` | as above |
| Finishing the running man's race | `Npc_Hylia_RunningMan_Adult/RunningRace_Clear` | `GerudoValley_Adult` | `EventTriggerRequestAutoSave` in the race script |
| Caught by the Gerudo guards (every version) | `SageOfSpirit/GerudoFortress_GotoPrison*` | `GerudoValley_Adult` | the Gerudo Valley arrival scene (`Dm_SY_BancChangeArrive_GerudoValley_01`) autosaves |
| Leaving Kokiri Forest (Saria on the bridge) | `Goto_HyruleCastle/KokiriForest_ArriveLostWood` | `HyruleField_Child` | the first Hyrule Field arrival (`Dm_SY_BancChangeArrive_HyruleField`) autosaves |

In each medallion scene the save comes from `GetPouchContent`. It asks `EventQueryIsAutoSavePouchContent` first,
so it only saves if the medallion is one of the items that autosave. The medallion scenes (`Dm_Awake_*_Get`) are
separate from the warp scripts that change the map. That the Chamber of Sages plays one and then the other is
inferred from the names (`ShadowTemple_SageOfShadow_SageRoom` is a boot event, not a script entry).

## Scenes that don't have the problem

- **Placing and taking the Master Sword** (`Market_Child_Field_ChildToAdult`, `Market_Adult_Field_AdultToChild`).
  These change Link's age on purpose: they set `Sequence_IsChildhood` themselves, and they check his age before the
  map change. Meeting Ganondorf in the Chamber of Sages (`DoorOfTime/SageRoom_MeetGanondorf`) sets it to adult too.
- **Dodongo's Cavern exit** (`Get_StoneOfFire_EscapeDungeon`). It checks Link's age and goes to the matching
  `DeathMountainTrail_Child` or `_Adult`.
- **Pandemonium boss rush and rematches** (`SubEvent_AllBossBattle`, `DreamBossStage`). They set
  `Sequence_IsChildhood` for each boss and set it back afterwards.
- **Minigame game-over returns.** Most go to a fixed-age map, but each can only be reached at the minigame's own
  age; the shooting gallery checks it.
- **Warp songs and Farore's Wind** (`Dm_SY_Ocarina/OcarinaWarp`, `WarpEvent/FaroreWind_Warp`). They pick the
  destination by age.
- **Scenes that don't change map.** A scene that stays in the same map can't change Link's age this way.

## Still to confirm

- That the destination map's age is what changes Link's age, rather than something else in these scenes. Only
  the Shadow Medallion case has been seen. To check one, watch it at the other age with **record flag changes**
  on. `Sequence_IsChildhood` is a saved field that flag logging watches, so `flags-NNN.csv` will show when it
  changes, next to the `area` row for the map change.
- What the three unchecked follow-on scenes listed above do.
