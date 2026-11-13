/* Flag logging: changes to the game's saved progress fields (GameData Bool, Int, Enum and Float fields kept in the
 * save slot and not cleared on an area change, and the String64 fields; FINDINGS §1), only those with a known name
 * (flagnames.c), one CSV row each, in sd:/sloperinaGZ/flaglog/. Three files, each with its own switch:
 *
 *   flags-NNN.csv             "record flag changes": everything but PlayerStatus.*; the newest one is kept and added to
 *                             across sessions and save loads, with a "start" row each session
 *   PlayerStatusChanges.csv   "record player status": PlayerStatus.* (health, magic, rupees, equipment, bottles);
 *                             kept and added to, a "start" row each time
 *   StoryEvents.csv           "record story events": the story menu's values (the quests' stages) when the game
 *                             changes them (not the menu), with what most likely caused it; kept and added to
 *
 * Rows: date,time,frame,source,map,x,y,z,link_state,type,hash,name,old,new,note
 *   date        the console's local date and time, "13/11/2026 15:47:02"
 *   time        seconds since this session's recording started
 *   source      game (changed since the last frame) or sloperinaGZ (changed by this frame's menu or cheats)
 *   link_state  what AutoSaveMgr says Link is doing ("Move", "EvtTalk", ...; 1.1.1 only)
 *   type        Bool, Int, Enum, Float or String (the text); area: Link entered a map (old -> new; the note has the
 *               entrance, arrival event and starter); start, bulk, reload and muted rows describe the recording
 *   name        the field's name; Enum values are choice names where known, else hashes
 * StoryEvents.csv adds: cause (in words), event (the last HavePlayedEvent / HaveTalked_Special that went on),
 *   event_ago (seconds), npc (that event's NPC), last_area ("old -> new"), area_ago, entrance, arrival_event,
 *   message_target (UI_MessageTargetActorName now)
 *
 * Each flag change in flags-NNN.csv also goes to the log ("event flag: <name> 0 -> 1") and to a list of the last
 * ones that the log menu's "event flags" filter shows (flaglog_line). tools/flaglog.py adds, from the game's event
 * scripts and messages, the scripts that set each one, their NPCs and the dialogue just before the change. */
#ifndef FLAGLOG_H
#define FLAGLOG_H

#include <stdbool.h>

extern bool flaglog_enabled;        /* the switches: settings.ini flag_log */
extern bool flaglog_status_enabled; /* flag_log_player_status */
extern bool flaglog_story_enabled;  /* flag_log_story */

/* game thread, once at the start of the frame (what the game changed) and once at its end (what this
 * module's menu and cheats changed) */
void flaglog_frame(bool end_of_frame);

/* the log menu's "event flags" list: the i-th flag change, newest first ("13/11 15:47:02 <name> 0 -> 1"), or 0 */
const char *flaglog_line(int i);

/* the menu's lines */
void flaglog_file_text(char *buf, int len);   /* "flags: flags-003.csv, 412 rows" (or off, waiting, ...) */
void flaglog_status_text(char *buf, int len); /* "player status: PlayerStatusChanges.csv, 20 rows" */
void flaglog_story_text(char *buf, int len);  /* "story events: StoryEvents.csv, 3 rows" */

#endif
