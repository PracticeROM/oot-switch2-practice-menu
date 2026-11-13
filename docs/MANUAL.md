# sloperinaGZ manual

Everything sloperinaGZ can do, menu by menu. New to it? Start with the [usage guide](USAGE.md).

## Contents

- [1 Introduction](#1-introduction)
  - [1.1 Controls](#11-controls)
  - [1.2 Reading the menu](#12-reading-the-menu)
  - [1.3 Shortcuts in the game](#13-shortcuts-in-the-game)
- [2 Menus](#2-menus)
  - [2.1 Link](#21-link)
  - [2.2 Warps](#22-warps)
  - [2.3 Scene](#23-scene)
  - [2.4 Cheats](#24-cheats)
  - [2.5 Inventory](#25-inventory)
  - [2.6 Equips](#26-equips)
  - [2.7 Story](#27-story)
  - [2.8 Watches](#28-watches)
  - [2.9 Debug](#29-debug)
  - [2.10 Settings](#210-settings)
  - [2.11 Log](#211-log)
  - [2.12 About](#212-about)
- [3 On the screen](#3-on-the-screen)
- [4 About frame advance](#4-about-frame-advance)
- [5 Settings file](#5-settings-file)
- [6 Languages](#6-languages)
- [7 Known limits](#7-known-limits)

## 1 Introduction

sloperinaGZ is a practice menu for **The Legend of Zelda: Ocarina of Time (Switch 2)**, versions 1.0.0 and 1.1.1, modelled
on gz, the practice ROM for the N64 game. The game loads it itself, so it runs on a console (Atmosphère) and in
emulators alike. Open the menu with **R + right stick down**.

### 1.1 Controls

| Button | Does |
|---|---|
| **R + right stick down** | Open / close the menu |
| **D-pad up / down** | Move |
| **D-pad left / right** | Change the value, or pick a button in a row |
| **L** | Select / press |
| **L held + D-pad left / right** | Numbers: change by 10 |
| **R + D-pad left** | Back one menu (closes the menu at the top) |

While the menu is open the game doesn't see the controller.

### 1.2 Reading the menu

- The **top line** shows where you are, e.g. `sloperinaGZ > inventory > collection`.
- The **bottom line** says what the buttons do for the selected item.
- **Grey lines** are headings or information; they can't be selected. Items under a heading are indented.
- `name >` opens another menu. `☑ name` is a checkbox. `name  < value >` is a switch. `name  < 3 > / 9` is a
  number (with its maximum).
- Some rows are **rows of buttons** (watches, the memory viewer): D-pad left / right picks a button, which turns
  blue, and **L** presses it.
- A **^** or **v** at the side means the list goes on above or below.
- Read-only information sits under **return**, after a blank line.

### 1.3 Shortcuts in the game

With the menu closed:

| Shortcut | Does |
|---|---|
| **R + D-pad right** | Show / hide the watches |
| **R + A + D-pad left** | Start / stop the timer |
| **R + B + D-pad left** | Reset the timer |
| **R + B + D-pad right** | Reset the lag counter |
| **R + A + D-pad right** | Reset **jump height** to FLT_MAX (as after a load: the jumpslash then works at any height) |
| **R + D-pad up** | Save position: where Link is and which way he faces |
| **R + D-pad down** | Load position: put Link back there (same map only) |
| **L3** | Levitate on / off (if enabled, [2.10.2](#2102-levitate)) |
| **X / B** held | Levitate up / down; release to hover |
| **R3** held | Levitate: hold height even if X/B is held |
| **L** or **R** (your choice) | Frame advance (if enabled, [2.10.3](#2103-frame-advance)) |

## 2 Menus

### 2.1 Link

| Item | What it does |
|---|---|
| **health** | Link's health in hearts and quarters. D-pad changes it a quarter heart at a time; **L** fills it. |
| **heart containers** | 1 to 20. Lowering it below your health lowers your health too. |
| **rupees** | Up to your wallet's size. **L** fills it. |
| **age** | **child** or **adult**: what the next **reload map** gives you. |
| **reload map** | Reloads the map you're in, at the age chosen above. Changing age loads the other age's version of the map and swaps sword and shield with the ones from the other age (adult-only tunics and boots come off a child). |

| **save position (R + d-up)** | Remembers where Link is, which way he faces and the map (gz's save position). The first save in an area only finds Link's position in memory ("Locating Link's starting position, please try again"; he's lifted a little while it's tested, as with levitate, and it's quicker while he's moving): press it again once it says it's found him. |
| **load position (R + d-down)** | Puts Link back there, facing the same way. Only in the same map. With nothing saved: "No position saved yet". |

Under **return**: Link's **position** (x, y, z), his **current age** and the **map**.

### 2.2 Warps

| Item | What it does |
|---|---|
| **places** | Every map, sorted into **overworld (child)**, **overworld (adult)**, **dungeons**, **boss & battle rooms** and **other**. Pick a map, then a spawn point; you go there straight away with the game's own fade. |
| **cancel warp** | Cancels a warp that's waiting (see below). |
| **arrival cutscenes** | Arrive with the spawn point's own arrival scene instead of the standard one. |

If the game ever refuses the instant warp, the warp waits for you instead: walk through any exit or loading zone
and you'll come out at the chosen place. Under **return**: the **current** map and the **warp** state.

### 2.3 Scene

| Item | What it does |
|---|---|
| **reload map** | Reloads the current map (at the age set in **link**). |
| *collision* | |
| **environment** | See below. |
| *save and load* | |
| **save position** | See [2.3.2](#232-save-position). |
| *enemies* | |
| **display enemy health** | A bar with **life/max** over each living enemy within 30 m of Link (red under a quarter); it goes when the enemy reaches 0. Enemies that haven't appeared yet (Wolfos still in the ground, Stalfos before they drop) have none, and none are drawn while the game is paused. In the game's style a bar fades out while its enemy is hidden behind walls or other scenery. |
| **display through walls** | Also show the bars of enemies hidden behind the scenery (the game's style). Indented under **display enemy health**. |

#### 2.3.1 Environment

**air walls**: the map's invisible walls, drawn as outlines over the game.

| Item | What it does |
|---|---|
| **show** | Draw the kinds ticked under it (its options, indented below it). **Red**: a wall Link is inside, whatever its kind. |
| **that push link back** | **Amber**: walls Link can't stand on, so he's pushed back off them (the ledge in front of the Forest Temple in Sacred Forest Meadow as a child). On by default. |
| **that stop link** | **Cyan**: invisible walls he can't go through (round the rocks in the Lost Woods). On by default. |
| **other kinds** | **Violet**: walls that don't touch Link on foot: those for enemies, NPCs, animals, Epona, the camera, bombs and the hookshot, and those that only stop him while he climbs. |
| **through link** | Also draw them where Link is in front of them (in the game's style they're hidden behind him). |

The two lines under the menu, below **return** and apart from its options, count the walls in this map and those near you, and say whether Link is inside one
(how deep, and which way the game will push him out). Outlines show through the scenery; in the game's style
Link stays in front of them.

#### 2.3.2 Save position

Where a save puts Link when it's loaded (1.1.1 only). A save doesn't keep where Link stands: it keeps a
restart point, which follows him while the game finds his spot safe and stays behind when it doesn't (in the
air, on a moving block or platform, in deep water, during some actions). The game keeps it in two slots and a
save takes **A** if it's set, else **B**. On loading, the game also searches round that point for a safe place,
up to 5 m away, so Link can still appear somewhere else.

| Item | What it does |
|---|---|
| **show slots** | Draw each slot that's set as a ring on the ground, an arrow the way Link will face and a pole. **Pink**: slot A, **green**: slot B. A cross on the pole marks the one a save would keep. |
| **where a load puts link** | **Yellow**: runs the game's own search round the slot a save would keep and draws its answer: a ring and a taller pole, and a line from the slot to it when the load would move Link. It tests the scene as it is now, so a moving block that would be back in its place after a load (or an enemy that would be gone) can make it differ. Link's own body counts in that search, so it's run while he stands on the slot (as he does on a load) and that answer is kept when he walks off; a slot he hasn't stood on is marked **approx.** until he does. Run as on foot, even on Epona. Indented under **show slots**; on by default. |

The lines under the menu give each slot's x, y, z and the way Link will face (degrees), with a **\*** on
the one a save would keep, or **not set**, or **not found** (the title screen, loading, 1.0.0); and where a load
would put Link and how far that is from the slot, or **stays**.

### 2.4 Cheats

**infinite**: energy, rupees, magic, deku sticks, deku nuts, deku seeds, bombs, arrows, bombchus, magic beans,
business scrub seeds (30): each kept full while ticked.

**player**: **2x movement speed** makes Link move twice as far each frame. **walk through walls**: hold the stick
against a wall, rock or air wall and Link walks on through it at the height he was at, until you let go of the stick
or turn it. While floating, levitate's own **through walls** setting applies instead. (To keep jump height at its
maximum, see **watches > lock jump height**.)

### 2.5 Inventory

| Menu | What's in it |
|---|---|
| **items** | **child items** (deku sticks, boomerang, fairy slingshot, magic beans, child trade item), **adult items** (fairy bow, megaton hammer, fire / ice / light arrow, the **hookshot** switch: none / hookshot / longshot, adult trade item), **universal** (bombs, bombchus, deku nuts, Din's fire, Farore's wind, Nayru's love, ocarina, bottles A to E, business scrub seed). |
| **ammo** | How much of each kind of ammo you carry, up to your bag's size. **L** fills it. |
| **equipment items** | **swords** (kokiri, master, and the **big sword** switch: none / giant's knife / broken giant's knife / biggoron's sword), **shields** (kokiri, hylian, mirror), **tunics** (goron, zora), **boots** (iron, hover), **other** (lenses of truth, echo gem, **mask slot**: one of the mask shop's masks at a time, **amiibo mask**: none / fairy-kid mask / princess mask, the masks Link's and Zelda's amiibo give; in the game's style each shows the chosen mask's picture). |
| **collection** | **quest rewards** (fairy ocarina, ocarina of time, gerudo card); **strength upgrades** (goron's bracelet, **gauntlets**: none / silver / gold); **capacity upgrades** (**scale**, and the size of each bag and the wallet: deku stick bag, deku nut bag, deku seed bag, bomb bag, quiver, wallet); **fairy upgrades** (double defense, **spin attack**: the Great Fairy of Power's stronger spin attack, set as her fountain's event having been seen, **magic**: your magic and the meter); **collectibles** (**skulltulas**: the tokens' count, and they show in the collection screen). |
| **quest items** | **spiritual stones** and **medallions**. |
| **songs** | **normal songs** and **warp songs**. |
| **dungeon items** | Per dungeon: map, compass, boss key, and **small keys** where it has them. |

Everything goes through the game's own code where it needs it: items appear on the pause screen, the HUD
redraws, taking away an item you're wearing takes it off Link, and an item on a button comes off the button.

### 2.6 Equips

What Link is wearing and holding right now (read only).

### 2.7 Story

| Item | What it does |
|---|---|
| **all tutorials seen** | Marks every tutorial message as seen, so they stop popping up. |
| *each quest* | The quest's current stage; D-pad left / right (or **L**) changes it. It takes effect after a loading zone. |

### 2.8 Watches

gz's watch list: values shown on the screen in a box while you play.

| Item | What it does |
|---|---|
| **show on screen** | Show the watches box (also **R + D-pad right** in the game). |
| **watches box > position** | Top-left, top-right, bottom-left, bottom-right, top-centre, bottom-centre, or **custom**. |
| **watches box > move watches position** | Move the box anywhere: the D-pad glides it (hold to speed up), **L** leaves it there. It's shown while you move it, and so is the input display if it's on. |

**watches**: your list, five at a time. With more, **↑** above it and **↓** below it scroll the list (hold **L** to keep
going), and in the game's style a scroll bar beside it shows where you are. Each watch is a
row of buttons:

| Button | Does |
|---|---|
| **✕** | Remove the watch. |
| **☑** | Show or hide it in the box (it stays in the list). |
| **🔧** | Open the value in **debug > memory** (for watches that read one place in memory). |
| *the name* | Rename it, with gz's on-screen keyboard. An empty name brings back its own name. |
| **< type >** | How the value is shown (next type each press). |
| **↑ ↓** | Move the watch up or down the list. |

Under the list:

- **📁 (known watches)**: the built-in watches: x, y, z, facing, move angle, speed, y speed, camera angle /
  pitch / distance, input, stick, lag, timer, frame, map, air wall, jump height. Each can be in the list once.
  **jump height** is the height your last jump set (takeoff + 0.7; FLT_MAX until you jump after a load), which
  the jumpslash launch is limited to. It's always in the list (hide it to keep it off screen).
- **+ (custom watch)**: a watch of any memory address. Type the address (D-pad left / right: digit, up / down:
  change it), **L** to add. It starts from your last memory watch, as in gz. One watch per address. Changed your
  mind? **B**, or D-pad left past the first digit to the **x** that takes the folder's place and **L**: no watch is
  added.

**Display types.** Positions and speeds: **f32** (decimal), **s32** (whole number), **x32** (the float's bits).
Angles: **deg**, **hex** (the game's 16-bit angle, 0x4000 = 90°), **s16**, **rad**. Counters: **dec**, **hex**.
Timer: **m:s**, **sec**. Stick: **%**, **raw**. Memory watches: **u8 s8 x8 u16 s16 x16 u32 s32 x32 f32 x64**.

**timer**: **start/stop timer**, **reset timer**, **reset lag counter** (also shortcuts,
[1.3](#13-shortcuts-in-the-game)). **reset jump height** is under its own heading, below.

**The keyboard** (renaming): the text line at the top (D-pad left / right moves the cursor, **L** finishes), then
rows of keys (D-pad picks a key, **L** types it) and a row of **shift**, **space**, **back**, **del** and **ok**.
**return** cancels. Up to 12 characters.

Memory watches aren't saved (addresses change every time the game starts); everything else is.

**jump height** is the seventh default watch. It resolves the current player's jumpslash height
threshold on each read, showing an f32 with nine significant digits, **Max** for `FLT_MAX`, or **N/A**
if unavailable. A padlock beside its name in the HUD and Watches list indicates that **lock jump height**
is enabled. Custom names are preserved; long names are shortened on screen to leave room for the icon.

Under the watch list, the **jump height** controls change that threshold:

- **reset jump height** writes `FLT_MAX` (about `3.40282347e+38`) once (also **R + A + D-pad right**). A later jump
  may change it again.
- **lock jump height** keeps writing `FLT_MAX` on each mod frame, including while the watch or menu is
  hidden. It follows the current player across scene changes and skips unavailable or read-only memory.
  The lock is off at each game launch and is not saved. Turn it off to stop writing; the game can then
  update the threshold normally on a later jump. Turning it off does not restore an old value.

These controls modify live gameplay memory. The watch itself still only reads it. This is a per-frame
freeze, not a patch of the game's setter: writes and checks within a single game frame may still happen
before the mod runs. Build and host tests do not establish whether this prevents every launch restriction
in-game; test normal jumps, sidehops, jumpslashes, scene changes, and restarts on the supported game version.

### 2.9 Debug

**tests > dive tests**: **from heights** and **while moving** measure how deep Link goes into water by how he falls in. Start
one with Link swimming at the surface (facing open water for **while moving**) and leave the controls alone: it
measures the surface, drops him again and again from set heights (from heights: 1 to 192 above the surface; while moving:
16 and 96 above it, 0 to 13.6 units/s the way he faces), twice each, and writes each drop's fall time, speed
going in, and deepest point to `log.txt` ("dive test: ..."). Choose it again to stop.

#### 2.9.1 Heap

How much memory the game has and uses (refreshed every second), and **view the heap in memory**.

#### 2.9.2 Memory

gz's memory viewer.

| Item | What it does |
|---|---|
| **address** | **L** to type an address; D-pad left / right moves a page. |
| **view** | byte, halfword, word, float, pointer. |
| **go to** | Jump to: main, sloperinaGZ, link actor, link status, camera, game data, save strings. |
| **↑ ↓** | Scroll a row (hold **L** to keep scrolling). |
| *rows* | 8 bytes each. **L** edits a row: D-pad left / right moves along it, up / down changes the digit (written at once). On the row's address, **L** adds a watch there; in pointer view, **L** on a value goes where it points. **R + D-pad left** stops editing. |

Unreadable memory shows as `??`; read-only memory can't be changed. **Editing memory can crash the game.**

#### 2.9.3 Pause research

The game's own pause state, for development: values you can watch, change and log.

#### 2.9.4 Flag logging

Records changes to the game's saved progress flags while you play, to learn what each flag is for. Every named story
flag, event flag, item flag, counter and quest stage kept in the save is compared with the frame before. Flags
without a known name are left out, and so is `Player_Rot_For_Map` (which way Link faces, for the map screen). The
files go to `sloperinaGZ/flaglog/` on the SD card. There are three, each with its own switch:

| Switch | File | What |
|---|---|---|
| **record flag changes** | `flags-NNN.csv` | Every flag change except player status. The newest one (the highest number) is added to, across sessions and save loads, with a `start` row for each session; `flags-001.csv` is made when there's none. To start a fresh file, rename or delete the old ones. |
| **record player status** | `PlayerStatusChanges.csv` | `PlayerStatus.*`: health, magic, rupees, what Link wears, bottles. One file, added to each time, with a `start` row for each session. |
| **record story events** | `StoryEvents.csv` | The story menu's values (the quests' stages) when the game changes them, with what most likely caused it. Your own changes from the story menu aren't recorded here. One file, added to each time. |

The switches are kept in the settings, so they stay on across starts. The lines under the menu show each file and
how many rows this session has added. While **record flag changes** is on, **Event flag written to CSV** shows in the bottom right
when a flag changes, at most once a second ("3 event flags written to CSV" when more changed in that time). Each
change is also kept in the log menu: choose **event flags** as its filter (2.11).

Each row has the date and time (the console's clock, e.g. `13/11/2026 15:47:02`), the seconds since this session's recording
started, the frame, the map, Link's position, what Link was doing (`EvtTalk` while he talks; 1.1.1 only), the
flag's type, hash and name, its old and new values, and who changed it: `game`, or `sloperinaGZ` for the menu and
cheats. Other kinds of row:
- **`area`:** Link entered a map. It gives the old and new map, and in the note the entrance, the arrival event
  and the event starter.
- **`String`:** a change to one of the game's named text fields, such as the map.
- **`bulk`:** loading a save changes thousands of flags at once, which shows as this one row.
- **`muted`:** a flag that keeps changing (a timer, or a value a cheat holds) is muted after 10 quick changes.

`StoryEvents.csv` adds, after those columns:

| Column | What |
|---|---|
| `cause` | In words, e.g. `talking: Npc_Kokiri_Mido_Child (Npc_Kokiri_Mido_Child_C1_N)`, `event: …`, `entering HyruleField_Adult -> LakeHylia_Adult - arrival event …`, `after the event …`, or `unknown`. |
| `event`, `event_ago` | The last event that played (a `HavePlayedEvent` or `HaveTalked_Special` flag going on) and how many seconds before. |
| `npc` | That event's NPC, when it's an NPC's and played in the last minute. |
| `last_area`, `area_ago`, `entrance`, `arrival_event` | The last map Link entered, how long before, the entrance and the event it played on arrival. |
| `message_target` | The name the game's messages are using at that moment (`UI_MessageTargetActorName`). |

On a PC with the game's files, `python tools/flaglog.py <file>.csv` writes `<file>-named.csv` for any of the three.
It adds a cause from the game's event scripts (`cause_from_scripts` in the story file, where the recorder's own
`cause` is kept), such as `talking: Npc_Kokiri_Mido_Child` or `getting <item>`. For changes made by event scripts it
also adds the script (`set_by`, e.g. `SageOfWater/IceCavern_WaterSerenade` for the Water Temple quest moving to
`Step04`), the NPC and the dialogue just before the change. The scripts say who *can* set a flag, so when several
can, the `match` column says how sure the pick is; see the tool's help.

### 2.10 Settings

**menu settings**: **language** ([6](#6-languages)), **size** (auto, small 2x, medium 2.5x, large 3x: the menu, watches
and log), **background opacity** (0 to 100%, 100% at first; below 100% the game shows through, slightly behind) and
**hide in game menus** (on at first): no watches, input display or log while the game's pause screen (Plus) or map
(Minus) is open, so they don't cover it. They show again when it closes, and while this menu is open (to move them).

**watch box header** (on at first): a small **watches** header over the watches box, underlined.

**other settings**: the **input display**, **levitate** and **frame advance** menus below.

#### 2.10.1 Input display

gz's input display, at the bottom left unless you move it (hidden while the menu is open).

| Item | What it does |
|---|---|
| **show** | Turn it on. |
| **background** | Put it on a background, like the watches. |
| **left stick**, **right stick**, **buttons** | What it shows. |
| **style** | **row**: gz's line of stick values and buttons. **controller**: a Switch 2 Pro Controller with the buttons lighting up, rings for the sticks and their values underneath. |
| **size** | small, medium, large: half, three quarters or all of the menu's size. |
| **position** | The corner it's in, or **custom** (where **move input display position** left it), as the watches box. |
| **move input display position** | Move it anywhere: the D-pad glides it (hold to speed up), **L** leaves it there. It's shown while you move it, and so are the watches if they're on. |

#### 2.10.2 Levitate

| Item | What it does |
|---|---|
| **enabled** | **L3** toggles floating. Hold **X** to rise, **B** to descend; release both to hover. The left stick moves horizontally. X/B/R3 are hidden from the game while floating; L3 is reserved while enabled. |
| **through walls** | Use the character controller's temporary collision bypass and position setter while floating. L3/off restores its previous collision mode. Held B commands descent through floors. |
| **vertical speed** | X/B speed in units per second, replacing the old rise-speed label. Existing saved settings remain compatible; a saved **0** uses 4 units/s for manual movement. |
| **move speed** | Horizontal speed in units per second. |

The status line under **return** shows what levitate is doing.

No X/B input means hover at any vertical-speed setting. Both held together also hover; **R3**
overrides either button to hold height. Releasing or reversing the buttons clears the previous
vertical movement immediately. The mod menu suspends intentional movement.

With **through walls** on, Link can pass through walls and ceilings; hold **B** to descend
through floors. Exit geometry before stopping levitation, since normal collision resumes then.
With through walls off, normal floor/wall correction remains active.

**L3** ends levitation with zero vertical velocity. Normal gravity then accelerates the fall,
and landing protection prevents fall-distance damage through the first landing. Later ordinary
falls are unchanged. The old short health-refund timer has been removed.

The user confirmed traversal through a previously blocking wall, followed by a gentle L3 fall
and no landing damage. This does not establish behavior at every surface or during every event.
Void protection and a void-trigger viewer are not included. Camera-follow and stationary-position
pause behavior have not been separately fixed.

#### 2.10.3 Frame advance

| Item | What it does |
|---|---|
| **enabled** | Turns frame advance on ([4](#4-about-frame-advance)). Off by default; updating from 0.0.3 or older turns it off once. |
| **in pause screen** | Off (the default): while the game's pause screen or an item menu is open, the button is the game's and doesn't start frame advance. On: it starts frame advance there too. |
| **pause icon** | gz's pause icon at the top left while paused. |
| **button** | **L** or **R**. The game doesn't see that button while frame advance is on. |

### 2.11 Log

Under **log history**: the last 128 messages sloperinaGZ wrote, newest first, five at a time. **↑** above them and
**↓** below them scroll (hold **L** to keep going), and in the game's style a scroll bar beside them shows where you are.

| Item | What it does |
|---|---|
| **filter** `< all >` | Which messages: **all**, only **problems** (anything that couldn't be done), **event flags**, or one kind (warp, settings, notice... whatever kinds there are). |

**event flags** shows the flag changes recorded by flag logging (2.9.4), each with the date and time, e.g.
`13/11 15:47:02 HavePlayedEvent.Npc_Kokiri_Mido_Child_C1C2_TT 0 -> 1`. It keeps the last 128 flag changes in a list of
their own, so other messages don't push them out. Flags without a known name show their hash.

Everything is also written to **`sloperinaGZ/log.txt`** on the SD card (on an emulator, its virtual SD card;
Lichii build 150+: `%APPDATA%\lichii\sdmc\sloperinaGZ\log.txt`; Fritchii v5.8+: `%APPDATA%\fritchii\sdmc\sloperinaGZ\log.txt`; Eden and older Lichii / Fritchii: `%APPDATA%\eden\sdmc\sloperinaGZ\log.txt`), with the time since the game started, about once a
second. It starts fresh each time the game starts. Send it along when you report a problem.

### 2.12 About

The name and version, the build date ("Build Date: ..."), and the website.

## 3 On the screen

| What | Where | Notes |
|---|---|---|
| Menu | Top left, under the D-pad icon | Always on top. |
| Watches | The corner you choose, or anywhere | [2.8](#28-watches) |
| Log | Bottom right | What you just did; stays a second, then fades. gz's grey text with a shadow. |
| Input display | Bottom left, or the corner you choose, or anywhere | [2.10.1](#2101-input-display); in the bottom left, it moves above the watches if they're there too. |
| Pause icon | Top left | Two bars: paused by frame advance. |
| Air walls | Over the game | [2.3.1](#231-environment) |
| Save slots | Over the game | [2.3.2](#232-save-position) |

## 4 About frame advance

With **settings > frame advance > enabled** on and the menu closed (and, unless **in pause screen** is on, the
game's pause screen closed):

- **Tap the button** (L or R): the game pauses (one more frame plays first, so the pause icon is on screen and
  you can see it hasn't crashed). Tap again: one frame. **Hold** it: after a moment, frames play
  one after another (about 15 a second) until you let go.
- **Button + L3**: the game runs again. It always works, even with the menu open (the menu closes): nothing is
  drawn while paused, so a menu opened then can't be seen. Press L3 first, then the button, so the menu doesn't
  take the button press.
- A tap counts when you let go. You can hold or press anything else meanwhile (move the stick, hold A or Z...),
  as in gz: the frame you let through gets it. Only with **R** as the button, R + D-pad and R + right stick down
  stay shortcuts and don't advance.
- **Buffered input**: everything you press while paused, and the last direction you push each stick, is given to
  the next frame, even if you've let go by then.
- While paused the picture doesn't change (the game draws nothing), so the watches and input display update with
  each frame you let through. The menu can still be opened and used while paused; its button works the menu then.

## 5 Settings file

Settings are saved on the SD card in `sloperinaGZ/settings.ini` (on an emulator, its virtual SD card) a couple of
seconds after you close the menu. It's plain `key=value` text: cheats, watches (which, in what order, their types,
names and whether shown), the watches box, menu size, background, hiding in game menus, language, input display,
levitate, frame advance, save position and flag logging. Delete it to go back to the defaults. Your game saves are never touched.

The game's pictures sloperinaGZ uses (the controllers in the input display, the menus' parts, the item icons)
are kept in `sloperinaGZ/cache/` once they're made from your copy of the game, so later starts don't have to make
them again. They're the game's own pictures: don't share them. Delete the folder whenever you like; they're made
afresh at the next start.

## 6 Languages

**settings > language** lists English and every translation on the SD card (`sloperinaGZ/lang/<code>.json`).
To make one, see [INSTALLATION.md](../INSTALLATION.md#languages). The font only has plain letters, so accents are
dropped (é shows as e) and other alphabets can't be shown yet.

## 7 Known limits

- Game versions **1.0.0** (base) and **1.1.1** (update v131072) are supported.
- Collision shows the air walls; the full collision mesh isn't drawn yet.
- Memory watches aren't kept between runs.
- Frame advance freezes the picture while paused ([4](#4-about-frame-advance)).
- Some changes only show after the game redraws them (pause screen, loading zone); story stages need a loading
  zone.
