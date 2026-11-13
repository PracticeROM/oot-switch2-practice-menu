/* Flag logging (flaglog.h): a copy of the saved progress fields, compared with the game's each frame.
 *
 * The frame hook runs right after the game applies its queued GameData writes (FINDINGS §3), so the
 * first pass of a frame sees everything the game changed since the last one. A second pass at the end of
 * the frame takes in what this module changed (menus, cheats, warps), so those rows say so.
 *
 * Which fields: Bool, Int, Enum and Float objects whose header says "kept in the save slot" (save file 0)
 * and "not cleared on an area change" (reset bit 0 off), and every String64 field (the map, what Link wears, the
 * names the game's messages and map changes use) - but only those with a known name (flagnames.c), less the
 * ones that only say where Link is (IGNORED). Each one's hash comes from its type's hash table (index -> hash),
 * read when recording starts.
 *
 * Where a change goes: PlayerStatus.* (health, magic, rupees, equipment, bottles) to PlayerStatusChanges.csv
 * (the "record player status" switch), everything else to flags-NNN.csv ("record flag changes"). A quest's stage
 * (the story menu's values, data_quests) that the game changes also goes to StoryEvents.csv ("record story
 * events"), with what most likely caused it: the last event played (HavePlayedEvent.* / HaveTalked_Special.*
 * going on), its NPC, the last map entered, and the game's message target name. A change of
 * Sequence_CurrentBanc is an "area" row (old map -> new map), with the entrance, arrival event and event starter
 * the exit reserved just before (Sequence_Reserved*, FINDINGS §5.18).
 *
 * Noise: a load (or a reset) changes thousands at once; more than BULK_LIMIT in one pass is one "bulk" row.
 * A field that keeps changing (a timer, a counter a cheat holds) is muted after MUTE_BURST quick changes. */
#include "flaglog.h"

#include "data.h"
#include "flagnames.h"
#include "gamedata.h"
#include "gfx.h"
#include "lang.h"
#include "link.h"
#include "savepos.h"
#include "sdfs.h"
#include "settings.h"
#include "sys.h"
#include "z64hd.h"

void *aligned_alloc(size_t align, size_t size); /* from main */
void free(void *p);

/* the console's clock and its time zone (nn::time, exported by sdk; the game has initialised it) */
typedef struct { int64_t value; } nn_posix_time_t;
typedef struct { int16_t year; int8_t month, day, hour, minute, second; } nn_calendar_time_t;
bool nn_time_initialized(void) __asm__("_ZN2nn4time13IsInitializedEv");
uint32_t nn_time_now(nn_posix_time_t *out)
  __asm__("_ZN2nn4time23StandardUserSystemClock14GetCurrentTimeEPNS0_9PosixTimeE");
uint32_t nn_time_to_calendar(nn_calendar_time_t *out, void *info, const nn_posix_time_t *time)
  __asm__("_ZN2nn4time14ToCalendarTimeEPNS0_12CalendarTimeEPNS0_22CalendarAdditionalInfoERKNS0_9PosixTimeE");
#define CALENDAR_INFO_SIZE 64 /* CalendarAdditionalInfo (24 bytes), with room to spare */

bool flaglog_enabled;
bool flaglog_status_enabled;
bool flaglog_story_enabled;

#define FLAGLOG_DIR SD_DIR "/flaglog"
#define FLAGS_PATTERN FLAGLOG_DIR "/flags-%03d.csv"
#define STATUS_FILE FLAGLOG_DIR "/PlayerStatusChanges.csv"
#define STORY_FILE FLAGLOG_DIR "/StoryEvents.csv"
#define STATUS_PREFIX "PlayerStatus."
#define MAX_FILES 999
#define PATH_LEN 64
#define CSV_COLUMNS "date,time,frame,source,map,x,y,z,link_state,type,hash,name,old,new,note"
#define CSV_HEADER CSV_COLUMNS "\n"
/* StoryEvents.csv: the same, then what most likely caused the change */
#define STORY_HEADER CSV_COLUMNS ",cause,event,event_ago,npc,last_area,area_ago,entrance,arrival_event,message_target\n"
#define UI_MESSAGE_TARGET 0x58E02405u /* UI_MessageTargetActorName (String64): who the game's messages name */
#define EVENT_PREFIX "HavePlayedEvent."
#define TALKED_PREFIX "HaveTalked_Special."
#define RECENT_SECONDS 10   /* an event or map change this recent is the cause */
#define NPC_SECONDS 60      /* an NPC's event this recent names the NPC */

/* named fields left out: where Link is and which way he faces, for the map screen (every step) */
static const char *const IGNORED[] = { "Player_Rot_For_Map" };

#define BULK_LIMIT 256           /* more changes than this in one pass: a load or a reset, one row */
#define MUTE_GAP (2 * GAME_FPS)  /* frames: a change this soon after the last one is "quick" */
#define MUTE_BURST 10            /* quick changes in a row before a field is muted */
#define WRITE_GAP GAME_FPS       /* frames between writes to the SD card */
#define BUF_SIZE 16384           /* rows waiting to be written, per file */
#define ROW_MAX 1536             /* a story row: the usual columns and its causes */
#define TEXT_MAX 48              /* map and link state, as written */
#define NOTICE_GAP GAME_FPS      /* frames between on-screen notices of flags written */
#define FLAG_LINES 128           /* the flag changes kept for the log menu (its "event flags" filter) */
#define FLAG_LINE_LEN 96

/* the kinds recorded, in this order in the start row */
static const gd_kind_t kinds[] = { GD_BOOL, GD_INT, GD_ENUM, GD_FLOAT };
#define N_KINDS (sizeof(kinds) / sizeof(*kinds))
static const char *const kind_names[GD_NTYPES] = {
  [GD_INT] = "Int", [GD_BOOL] = "Bool", [GD_FLOAT] = "Float", [GD_ENUM] = "Enum",
};

typedef struct {
  uintptr_t value;      /* address of the value in the game's object */
  uint32_t  hash;
  uint32_t  last;       /* the value as last seen (a Bool in the low byte) */
  uint32_t  last_frame; /* when it last changed */
  uint8_t   kind;       /* gd_kind_t */
  uint8_t   burst;      /* quick changes in a row */
  uint8_t   muted;
  uint8_t   status;     /* a PlayerStatus.* field: its rows go to the player status file */
  uint8_t   story;      /* a quest's stage (data_quests): also to the story file, when the game changes it */
  uint8_t   event;      /* a HavePlayedEvent.* / HaveTalked_Special.* Bool: an event played (for the story file) */
} tracked_t;

/* where each type's objects were when recording started: if they move, the copy is taken again */
typedef struct { uint8_t *objects; uint32_t count; } type_place_t;

/* an output file and the rows waiting for it */
typedef struct {
  char     path[PATH_LEN];
  char     buf[BUF_SIZE];
  size_t   len;
  uint32_t rows, muted;
  bool     open;   /* rows go to it */
  bool     failed; /* it couldn't be made or written: nothing more until its switch is turned off and on */
} sink_t;
static sink_t flags_out, status_out, story_out;
static int file_number; /* the flags-NNN.csv in use; 0 = not looked for yet */

static bool recording; /* the copy is taken (either switch on) */
static tracked_t *fields;
static int n_fields, n_by_kind[GD_NTYPES];
static type_place_t places[GD_NTYPES];
static uint64_t start_tick;
static int frames_since_write;

/* the last FLAG_LINES flag changes, for the log menu: "13/11 15:47:02 <name> 0 -> 1" */
static char flag_lines[FLAG_LINES][FLAG_LINE_LEN];
static int flag_head, flag_count; /* flag_head: where the next one goes */

static int notice_pending;      /* flag rows written since the last notice */
static uint32_t notice_frame;   /* when the last notice was shown */

/* what happened last, for the story file's causes (kept while either switch is on) */
static struct {
  char     event[96];   /* the last HavePlayedEvent.* / HaveTalked_Special.* that went on, without the prefix */
  uint64_t event_tick;
  char     area[2 * GD_STR_LEN + 8], entrance[GD_STR_LEN], arrival[GD_STR_LEN]; /* "old -> new" */
  uint64_t area_tick;
} recent;

/* String64 fields (the named ones), compared as text */
#define MAX_STRINGS 128
typedef struct {
  uint32_t index, hash, last_frame;
  uint8_t  burst, muted, status;
  char     last[GD_STR_LEN];
} tracked_str_t;
static tracked_str_t strings[MAX_STRINGS];
static int n_strings;
static type_place_t string_place;
/* what the last exit reserved (Sequence_Reserved*), kept until the map changes: the game may clear them first */
static char reserved_spawn[GD_STR_LEN], reserved_event[GD_STR_LEN], reserved_starter[GD_STR_LEN];

static bool count_quick(uint32_t *last_frame, uint8_t *burst, uint32_t now);
static void add_story_row(uint32_t hash, const char *old_text, const char *new_text);
static void notice_frame_end(void);
static void keep_flag_line(uint32_t hash, const char *old_text, const char *new_text, bool by_module);

/* the changes of one pass: their fields and old values */
static int changed[BULK_LIMIT];
static uint32_t changed_old[BULK_LIMIT];

static bool starts_with(const char *s, const char *prefix)
{
  while (*prefix)
    if (*s++ != *prefix++)
      return false;
  return true;
}

static bool same_text(const char *a, const char *b)
{
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}

/* --- the files ----------------------------------------------------------------------------------- */

static void flush(sink_t *out)
{
  if (!out->len || !out->path[0])
    return;
  fs_file_t file;
  if (fs_open(&file, out->path, FS_WRITE | FS_ALLOW_APPEND) != 0) {
    log_printf("flag logging: couldn't open %s", out->path);
    out->failed = true;
    out->open = false;
    out->len = 0;
    return;
  }
  int64_t size = 0;
  fs_size(&size, file);
  fs_write_option_t opt = { FS_WRITE_FLUSH };
  if (fs_write(file, size, out->buf, out->len, &opt) != 0) {
    log_printf("flag logging: write failed (%s)", out->path);
    out->failed = true;
    out->open = false;
  }
  fs_close(file);
  out->len = 0;
}

static void flush_all(void)
{
  frames_since_write = 0;
  flush(&flags_out);
  flush(&status_out);
  flush(&story_out);
}

static void append(sink_t *out, const char *text, size_t n)
{
  if (!out->open)
    return;
  if (out->len + n > sizeof(out->buf))
    flush(out);
  if (out->open && out->len + n <= sizeof(out->buf)) {
    memcpy(out->buf + out->len, text, n);
    out->len += n;
  }
}

static bool file_exists(const char *path)
{
  fs_file_t file;
  if (fs_open(&file, path, FS_READ) != 0)
    return false;
  fs_close(file);
  return true;
}

/* numbered: the newest flags-NNN.csv (the highest number before the first gap), added to across sessions and
 * save loads, or flags-001.csv when there's none; else `file` (PlayerStatusChanges.csv, StoryEvents.csv). Each is
 * kept and added to, and made with `header` when new. */
static bool open_sink(sink_t *out, bool numbered, const char *file, const char *header)
{
  fs_create_dir(FLAGLOG_DIR); /* fails harmlessly if it exists */
  out->len = 0;
  out->rows = out->muted = 0;
  if (numbered && !file_number) {
    while (file_number < MAX_FILES) {
      snprintf(out->path, sizeof(out->path), FLAGS_PATTERN, file_number + 1);
      if (!file_exists(out->path))
        break;
      file_number++;
    }
    if (!file_number)
      file_number = 1;
  }
  if (numbered)
    snprintf(out->path, sizeof(out->path), FLAGS_PATTERN, file_number);
  else
    snprintf(out->path, sizeof(out->path), "%s", file);
  bool made = false;
  if (!file_exists(out->path)) {
    if (fs_create_file(out->path, 0) != 0) {
      log_printf("flag logging: couldn't make %s", out->path);
      out->path[0] = 0;
      out->failed = true;
      return false;
    }
    made = true;
  }
  out->open = true;
  if (made)
    append(out, header, strlen(header));
  return true;
}

static void close_sink(sink_t *out, const char *what)
{
  if (out->open) {
    flush(out);
    log_printf("flag logging: %s stopped (%s, %u rows)", what, out->path, (unsigned)out->rows);
  }
  out->open = false;
}

/* `s` with anything that would break a CSV field (commas, quotes, line breaks, control bytes) as '_' */
static void csv_text(char *out, const char *s, int len)
{
  int i = 0;
  for (; s && s[i] && i < len - 1; i++) {
    char c = s[i];
    out[i] = (c == ',' || c == '"' || c < 0x20 || c > 0x7E) ? '_' : c;
  }
  out[i] = 0;
}

/* --- names and the clock --------------------------------------------------------------------- */

const char *flagname(uint32_t hash)
{
  int lo = 0, hi = flagnames_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (flagnames[mid].hash == hash)
      return flagnames[mid].name;
    if (flagnames[mid].hash < hash)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return 0;
}

const char *flagchoice(uint32_t field, uint32_t choice)
{
  int lo = 0, hi = flagchoices_count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    const flagchoice_t *c = &flagchoices[mid];
    if (c->field == field && c->choice == choice)
      return c->name;
    if (c->field < field || (c->field == field && c->choice < choice))
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return 0;
}

/* A field worth watching: it has a known name and isn't one of IGNORED. *status: it's a PlayerStatus.* field. */
static bool watched(uint32_t hash, uint8_t *status)
{
  const char *name = flagname(hash);
  if (!name)
    return false;
  for (size_t i = 0; i < sizeof(IGNORED) / sizeof(*IGNORED); i++)
    if (same_text(name, IGNORED[i]))
      return false;
  *status = starts_with(name, STATUS_PREFIX);
  return true;
}

/* the local date and time now (asked once a frame): "13/11/2026 15:47:02" (full) and "13/11 15:47:02" (short);
 * "" if the console's clock can't be read */
static void local_time(const char **full, const char **brief)
{
  static char full_text[40], brief_text[32]; /* room for any value the fields can hold */
  static uint32_t asked_frame;
  static bool asked;
  uint32_t now = frame_counter();
  if (!asked || now != asked_frame) {
    asked = true;
    asked_frame = now;
    full_text[0] = brief_text[0] = 0;
    nn_posix_time_t posix;
    nn_calendar_time_t cal;
    static _Alignas(8) uint8_t info[CALENDAR_INFO_SIZE];
    if (nn_time_initialized() && nn_time_now(&posix) == 0 && nn_time_to_calendar(&cal, info, &posix) == 0) {
      snprintf(full_text, sizeof(full_text), "%02d/%02d/%04d %02d:%02d:%02d", cal.day, cal.month, cal.year, cal.hour,
               cal.minute, cal.second);
      snprintf(brief_text, sizeof(brief_text), "%02d/%02d %02d:%02d:%02d", cal.day, cal.month, cal.hour, cal.minute,
               cal.second);
    }
  }
  *full = full_text;
  *brief = brief_text;
}

static void format_value(char *out, int len, uint8_t kind, uint32_t hash, uint32_t value)
{
  const char *choice;
  switch (kind) {
    case GD_BOOL:  snprintf(out, len, "%u", (unsigned)(value & 0xFF)); break;
    case GD_INT:   snprintf(out, len, "%d", (int)(int32_t)value); break;
    case GD_ENUM:
      if ((choice = flagchoice(hash, value)))
        snprintf(out, len, "%s", choice);
      else
        snprintf(out, len, "0x%08x", (unsigned)value);
      break;
    case GD_FLOAT: {
      union { uint32_t u; float f; } v = { value };
      snprintf(out, len, "%.3f", (double)v.f);
      break;
    }
    default:       snprintf(out, len, "%u", (unsigned)value);
  }
}

/* --- rows ---------------------------------------------------------------------------------------- */

static bool add_row_extra(sink_t *out, bool by_module, const char *type, uint32_t hash, const char *old_value,
                          const char *new_value, const char *note, const char *extra);

/* One row to `out`: the context (date, time, map, Link) then `type,hash,name,old,new,note` (the name from the
 * hash). True if it went to an open file. */
static bool add_row(sink_t *out, bool by_module, const char *type, uint32_t hash, const char *old_value,
                    const char *new_value, const char *note)
{
  return add_row_extra(out, by_module, type, hash, old_value, new_value, note, 0);
}

/* the same, with `extra` (more columns, already CSV-safe) after the note */
static bool add_row_extra(sink_t *out, bool by_module, const char *type, uint32_t hash, const char *old_value,
                          const char *new_value, const char *note, const char *extra)
{
  if (!out->open)
    return false;
  char row[ROW_MAX], map[TEXT_MAX], state[TEXT_MAX], raw_state[TEXT_MAX];
  csv_text(map, gd_get_str(GD_STR_MAP), sizeof(map));
  savepos_link_state(raw_state, sizeof(raw_state));
  csv_text(state, raw_state, sizeof(state));
  char where[64] = ",,";
  vec3f_t pos;
  if (link_drawn_pos(&pos))
    snprintf(where, sizeof(where), "%.2f,%.2f,%.2f", (double)pos.x, (double)pos.y, (double)pos.z);
  uint64_t ticks = sys_tick() - start_tick, freq = sys_tick_freq();
  uint64_t centis = freq ? ticks * 100 / freq : 0;
  char hash_text[12] = "";
  if (hash)
    snprintf(hash_text, sizeof(hash_text), "0x%08x", (unsigned)hash);
  const char *name = hash ? flagname(hash) : 0, *date, *brief;
  local_time(&date, &brief);
  int n = snprintf(row, sizeof(row), "%s,%u.%02u,%u,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s%s%s\n", date,
                   (unsigned)(centis / 100), (unsigned)(centis % 100), (unsigned)frame_counter(),
                   by_module ? "sloperinaGZ" : "game", map, where, state, type, hash_text, name ? name : "", old_value,
                   new_value, note, extra ? "," : "", extra ? extra : "");
  if (n <= 0)
    return false;
  if (n >= (int)sizeof(row))
    n = (int)sizeof(row) - 1;
  append(out, row, (size_t)n);
  out->rows++;
  return out->open;
}

/* the start of a recording in a file: what's watched (a new flags file; each start in the player status file) */
static void add_start_row(sink_t *out, int watched_fields)
{
  char note[200], clean[200];
  snprintf(note, sizeof(note), "sloperinaGZ %s on game %s: watching %d fields (Bool %d; Int %d; Enum %d; Float %d; String %d)", /* not menu text */
           PACKAGE_VERSION, z64hd_game->version, watched_fields, n_by_kind[GD_BOOL], n_by_kind[GD_INT],
           n_by_kind[GD_ENUM], n_by_kind[GD_FLOAT], n_strings);
  csv_text(clean, note, sizeof(clean));
  add_row(out, false, "start", 0, "", "", clean); /* not menu text */
}

/* --- the copy ------------------------------------------------------------------------------------ */

static uint32_t read_value(const tracked_t *f)
{
  return f->kind == GD_BOOL ? *(const volatile uint8_t *)f->value : *(const volatile uint32_t *)f->value;
}

/* the text of String64 object `index`, at most GD_STR_LEN - 1 characters; false if unreadable */
static bool read_string(uint32_t index, char out[GD_STR_LEN])
{
  const char *text = gd_str_index(index);
  if (!text)
    return false;
  int i = 0;
  for (; i < GD_STR_LEN - 1 && text[i]; i++)
    out[i] = text[i];
  out[i] = 0;
  return true;
}

static bool same_string(const char *a, const char *b)
{
  for (int i = 0; i < GD_STR_LEN; i++) {
    if (a[i] != b[i])
      return false;
    if (!a[i])
      return true;
  }
  return true;
}

static void copy_string(char *to, const char *from)
{
  int i = 0;
  for (; i < GD_STR_LEN - 1 && from[i]; i++)
    to[i] = from[i];
  to[i] = 0;
}

/* Every watched String64 field, and its text now. */
static void take_strings(void)
{
  n_strings = 0;
  reserved_spawn[0] = reserved_event[0] = reserved_starter[0] = 0;
  gd_type_t *type = gd_string_type();
  string_place.objects = type ? type->objects : 0;
  string_place.count = type ? type->count : 0;
  if (!type)
    return;
  for (uint32_t slot = 0; slot < type->table_size && n_strings < MAX_STRINGS; slot++) {
    uint32_t hash = type->table[2 * slot], index = type->table[2 * slot + 1];
    uint8_t status;
    if (!hash || index >= type->count || !watched(hash, &status))
      continue;
    tracked_str_t *t = &strings[n_strings];
    memset(t, 0, sizeof(*t));
    if (!read_string(index, t->last))
      continue;
    t->index = index;
    t->hash = hash;
    t->status = status;
    n_strings++;
  }
}

/* Take the copy: every watched saved progress field of each kind. False if GameData isn't ready. */
static bool take_copy(void)
{
  free(fields);
  fields = 0;
  n_fields = 0;
  int capacity = 0;
  for (size_t k = 0; k < N_KINDS; k++) {
    gd_type_t *type = gd_type(kinds[k]);
    if (!type)
      return false;
    capacity += (int)type->count;
  }
  fields = aligned_alloc(16, (size_t)capacity * sizeof(tracked_t));
  uint32_t *hash_of = 0;
  if (!fields)
    return false;
  for (size_t k = 0; k < N_KINDS; k++) {
    gd_kind_t kind = kinds[k];
    gd_type_t *type = gd_type(kind);
    uint32_t stride, value_off;
    gd_layout(kind, &stride, &value_off);
    if (!type || !sys_mapped((uintptr_t)type->objects, (size_t)type->count * stride, SYS_PERM_R) ||
        !(hash_of = aligned_alloc(16, (size_t)type->count * sizeof(uint32_t))))
      goto fail;
    memset(hash_of, 0, (size_t)type->count * sizeof(uint32_t));
    for (uint32_t slot = 0; slot < type->table_size; slot++) {
      uint32_t hash = type->table[2 * slot], index = type->table[2 * slot + 1];
      if (hash && index < type->count)
        hash_of[index] = hash;
    }
    n_by_kind[kind] = 0;
    for (uint32_t i = 0; i < type->count; i++) {
      uint8_t *object = type->objects + (size_t)i * stride;
      uint32_t reset_bits = *(const uint32_t *)(object + GD_FIELD_RESET_BITS);
      int32_t save_file = *(const int32_t *)(object + GD_FIELD_SAVE_FILE);
      uint8_t status;
      if (!hash_of[i] || save_file != 0 || (reset_bits & 1) || n_fields >= capacity || !watched(hash_of[i], &status))
        continue;
      tracked_t *f = &fields[n_fields++];
      memset(f, 0, sizeof(*f));
      f->value = (uintptr_t)(object + value_off);
      f->hash = hash_of[i];
      f->kind = (uint8_t)kind;
      f->status = status;
      const char *name = flagname(f->hash);
      f->event = kind == GD_BOOL && (starts_with(name, EVENT_PREFIX) || starts_with(name, TALKED_PREFIX));
      for (int q = 0; q < data_n_quests && !f->story; q++)
        f->story = data_quests[q].hash == f->hash;
      f->last = read_value(f);
      n_by_kind[kind]++;
    }
    places[kind].objects = type->objects;
    places[kind].count = type->count;
    free(hash_of);
    hash_of = 0;
  }
  take_strings();
  return n_fields > 0;
fail:
  free(hash_of);
  free(fields);
  fields = 0;
  n_fields = 0;
  return false;
}

/* the types' objects are where the copy found them */
static bool places_same(void)
{
  for (size_t k = 0; k < N_KINDS; k++) {
    gd_type_t *type = gd_type(kinds[k]);
    if (!type || type->objects != places[kinds[k]].objects || type->count != places[kinds[k]].count)
      return false;
  }
  gd_type_t *strings_type = gd_string_type();
  return strings_type && strings_type->objects == string_place.objects && strings_type->count == string_place.count;
}

static void stop(void)
{
  close_sink(&flags_out, "flags");
  close_sink(&status_out, "player status");
  close_sink(&story_out, "story events");
  recording = false;
  free(fields);
  fields = 0;
  n_fields = 0;
}

/* the copy, when either switch is on: false until GameData is ready */
static bool start(void)
{
  if (!settings_sd() || !gd_ready() || !take_copy())
    return false;
  recording = true;
  notice_pending = 0;
  memset(&recent, 0, sizeof(recent));
  start_tick = sys_tick();
  log_printf("flag logging: watching %d fields", n_fields + n_strings);
  return true;
}

/* each file open while its switch is on (and it hasn't failed), closed when it's off */
static void sync_files(void)
{
  if (flaglog_enabled && !flags_out.open && !flags_out.failed && open_sink(&flags_out, true, 0, CSV_HEADER)) {
    add_start_row(&flags_out, n_fields + n_strings);
    log_printf("flag logging: recording flag changes to %s", flags_out.path);
  }
  if (flaglog_status_enabled && !status_out.open && !status_out.failed &&
      open_sink(&status_out, false, STATUS_FILE, CSV_HEADER)) {
    add_start_row(&status_out, n_fields + n_strings);
    log_printf("flag logging: recording player status to %s", status_out.path);
  }
  if (flaglog_story_enabled && !story_out.open && !story_out.failed &&
      open_sink(&story_out, false, STORY_FILE, STORY_HEADER)) {
    add_start_row(&story_out, n_fields + n_strings);
    log_printf("flag logging: recording story events to %s", story_out.path);
  }
}

/* --- per frame ----------------------------------------------------------------------------------- */

/* a "bulk" or "reload" row, to every open file */
static void add_marker_row(bool by_module, const char *type, const char *note)
{
  add_row(&flags_out, by_module, type, 0, "", "", note);
  add_row(&status_out, by_module, type, 0, "", "", note);
  add_row(&story_out, by_module, type, 0, "", "", note);
}

static void compare(bool by_module)
{
  uint32_t now = frame_counter();
  int n_changed = 0;
  for (int i = 0; i < n_fields; i++) {
    tracked_t *f = &fields[i];
    uint32_t value = read_value(f);
    if (value == f->last)
      continue;
    if (n_changed < BULK_LIMIT) {
      changed[n_changed] = i;
      changed_old[n_changed] = f->last;
    }
    n_changed++;
    f->last = value;
  }
  if (n_changed > BULK_LIMIT) {
    char note[96];
    snprintf(note, sizeof(note), "%d fields changed at once (a save loaded or a reset); not listed", /* not menu text */
             n_changed);
    add_marker_row(by_module, "bulk", note); /* not menu text */
    return;
  }
  /* first what happened (events going on), so a stage changed in the same frame is put down to it */
  for (int c = 0; c < n_changed; c++) {
    tracked_t *f = &fields[changed[c]];
    if (f->event && (f->last & 0xFF) && !(changed_old[c] & 0xFF)) {
      const char *name = flagname(f->hash);
      name += starts_with(name, EVENT_PREFIX) ? sizeof(EVENT_PREFIX) - 1 : sizeof(TALKED_PREFIX) - 1;
      snprintf(recent.event, sizeof(recent.event), "%s", name);
      recent.event_tick = sys_tick();
    }
  }
  for (int c = 0; c < n_changed; c++) {
    tracked_t *f = &fields[changed[c]];
    sink_t *out = f->status ? &status_out : &flags_out;
    if (f->story && !by_module && story_out.open) {
      char old_story[24], new_story[24];
      format_value(old_story, sizeof(old_story), f->kind, f->hash, changed_old[c]);
      format_value(new_story, sizeof(new_story), f->kind, f->hash, f->last);
      add_story_row(f->hash, old_story, new_story);
    }
    bool mute = count_quick(&f->last_frame, &f->burst, now);
    if (f->muted || !out->open)
      continue;
    char old_text[24], new_text[24];
    format_value(old_text, sizeof(old_text), f->kind, f->hash, changed_old[c]);
    format_value(new_text, sizeof(new_text), f->kind, f->hash, f->last);
    if (mute) {
      f->muted = 1;
      out->muted++;
      add_row(out, by_module, "muted", f->hash, old_text, new_text, /* not menu text */
              "keeps changing (a timer or a held value?): not logged any more in this file"); /* not menu text */
      continue;
    }
    if (add_row(out, by_module, kind_names[f->kind], f->hash, old_text, new_text, "") && !f->status) {
      keep_flag_line(f->hash, old_text, new_text, by_module);
      notice_pending++;
    }
  }
}

/* quick changes in a row, and whether that's enough to mute (once) */
static bool count_quick(uint32_t *last_frame, uint8_t *burst, uint32_t now)
{
  bool quick = *last_frame && now - *last_frame < MUTE_GAP;
  *burst = quick ? (uint8_t)(*burst < 255 ? *burst + 1 : 255) : 0;
  *last_frame = now ? now : 1;
  return *burst >= MUTE_BURST;
}

static void compare_strings(bool by_module)
{
  uint32_t now = frame_counter();
  char text[GD_STR_LEN];
  for (int i = 0; i < n_strings; i++) {
    tracked_str_t *t = &strings[i];
    if (!read_string(t->index, text) || same_string(text, t->last))
      continue;
    char old_text[GD_STR_LEN], new_text[GD_STR_LEN];
    csv_text(old_text, t->last, sizeof(old_text));
    csv_text(new_text, text, sizeof(new_text));
    copy_string(t->last, text);
    if (text[0]) {
      if (t->hash == GD_STR_SPAWN)
        copy_string(reserved_spawn, text);
      else if (t->hash == GD_STR_EVENT)
        copy_string(reserved_event, text);
      else if (t->hash == GD_STR_STARTER)
        copy_string(reserved_starter, text);
    }
    sink_t *out = t->status ? &status_out : &flags_out;
    if (t->hash == GD_STR_MAP) {
      char note[3 * GD_STR_LEN + 64], clean[sizeof(note)];
      snprintf(note, sizeof(note), "entrance %s; arrival event %s; starter %s", /* not menu text */
               reserved_spawn[0] ? reserved_spawn : "-", reserved_event[0] ? reserved_event : "-",
               reserved_starter[0] ? reserved_starter : "-");
      csv_text(clean, note, sizeof(clean));
      add_row(out, by_module, "area", t->hash, old_text, new_text, clean); /* not menu text */
      snprintf(recent.area, sizeof(recent.area), "%s -> %s", old_text[0] ? old_text : "-", new_text);
      snprintf(recent.entrance, sizeof(recent.entrance), "%s", reserved_spawn);
      snprintf(recent.arrival, sizeof(recent.arrival), "%s", reserved_event);
      recent.area_tick = sys_tick();
      reserved_spawn[0] = reserved_event[0] = reserved_starter[0] = 0;
      continue;
    }
    bool mute = count_quick(&t->last_frame, &t->burst, now);
    if (t->muted || !out->open)
      continue;
    if (mute) {
      t->muted = 1;
      out->muted++;
      add_row(out, by_module, "muted", t->hash, old_text, new_text, /* not menu text */
              "keeps changing: not logged any more in this file"); /* not menu text */
      continue;
    }
    add_row(out, by_module, "String", t->hash, old_text, new_text, ""); /* not menu text */
  }
}

void flaglog_frame(bool end_of_frame)
{
  if (!flaglog_enabled) {
    close_sink(&flags_out, "flags");
    flags_out.failed = false;
  }
  if (!flaglog_status_enabled) {
    close_sink(&status_out, "player status");
    status_out.failed = false;
  }
  if (!flaglog_story_enabled) {
    close_sink(&story_out, "story events");
    story_out.failed = false;
  }
  if (!flaglog_enabled && !flaglog_status_enabled && !flaglog_story_enabled) {
    if (recording)
      stop();
    return;
  }
  if (!recording && (end_of_frame || !start()))
    return;
  if (!end_of_frame)
    sync_files();
  if (!gd_ready())
    return; /* loading: the values may be half made */
  if (!places_same()) {
    if (take_copy()) {
      add_marker_row(end_of_frame, "reload", /* not menu text */
                     "the game's fields moved (a reset?): copied again; not listed"); /* not menu text */
    } else {
      flush_all();
      recording = false; /* start() takes a new copy once the fields are back; the files stay open */
    }
    return;
  }
  compare_strings(end_of_frame);
  compare(end_of_frame);
  if (end_of_frame)
    notice_frame_end();
  if (end_of_frame && (++frames_since_write >= WRITE_GAP || flags_out.len > sizeof(flags_out.buf) / 2 ||
                       status_out.len > sizeof(status_out.buf) / 2 || story_out.len > sizeof(story_out.buf) / 2))
    flush_all();
}

/* --- the story file ------------------------------------------------------------------------------ */

/* seconds since `tick` (0: never), or -1 */
static int seconds_since(uint64_t tick)
{
  uint64_t freq = sys_tick_freq();
  if (!tick || !freq)
    return -1;
  return (int)((sys_tick() - tick) / freq);
}

/* The NPC an event belongs to, from its name: "Npc_Kokiri_Mido_Child_C1_N" -> "Npc_Kokiri_Mido_Child" (cut at the
 * first part after the second that has capitals or digits but no small letters: the event's code). "" if the
 * event isn't an NPC's. */
static void npc_of_event(const char *event, char *out, int len)
{
  out[0] = 0;
  if (!starts_with(event, "Npc_"))
    return;
  int end = 0, part = 0, i = 0;
  while (event[i]) {
    bool lower = false, code = false;
    while (event[i] && event[i] != '_') {
      if (event[i] >= 'a' && event[i] <= 'z')
        lower = true;
      else if (event[i] >= 'A' && event[i] <= 'Z')
        code = true;
      i++;
    }
    if (part >= 2 && code && !lower)
      break;
    end = i;
    part++;
    if (event[i])
      i++;
  }
  snprintf(out, len, "%.*s", end, event);
}

/* A quest stage the game changed, to StoryEvents.csv, with what most likely caused it. */
static void add_story_row(uint32_t hash, const char *old_text, const char *new_text)
{
  char state[TEXT_MAX];
  savepos_link_state(state, sizeof(state));
  bool in_event = starts_with(state, "Evt");
  int event_ago = recent.event[0] ? seconds_since(recent.event_tick) : -1;
  int area_ago = recent.area[0] ? seconds_since(recent.area_tick) : -1;
  char npc[96] = "";
  if (event_ago >= 0 && event_ago <= NPC_SECONDS)
    npc_of_event(recent.event, npc, sizeof(npc));
  char cause[400];
  if (in_event && event_ago >= 0 && event_ago <= NPC_SECONDS)
    snprintf(cause, sizeof(cause), "%s %s (%s)", state[3] == 'T' ? "talking:" : "event:", /* not menu text */
             npc[0] ? npc : recent.event, recent.event);
  else if (in_event)
    snprintf(cause, sizeof(cause), "an event (%s)", state); /* not menu text */
  else if (area_ago >= 0 && area_ago <= RECENT_SECONDS)
    snprintf(cause, sizeof(cause), "entering %s%s%s", recent.area, recent.arrival[0] ? " - arrival event " : "", /* not menu text */
             recent.arrival);
  else if (event_ago >= 0 && event_ago <= RECENT_SECONDS)
    snprintf(cause, sizeof(cause), "after the event %s", recent.event); /* not menu text */
  else
    snprintf(cause, sizeof(cause), "unknown: no event or map change just before"); /* not menu text */
  char event_ago_text[12] = "", area_ago_text[12] = "";
  if (event_ago >= 0)
    snprintf(event_ago_text, sizeof(event_ago_text), "%d", event_ago);
  if (area_ago >= 0)
    snprintf(area_ago_text, sizeof(area_ago_text), "%d", area_ago);
  char c_cause[400], c_event[96], c_npc[96], c_area[sizeof(recent.area)], c_entrance[GD_STR_LEN],
       c_arrival[GD_STR_LEN], c_target[GD_STR_LEN];
  csv_text(c_cause, cause, sizeof(c_cause));
  csv_text(c_event, recent.event, sizeof(c_event));
  csv_text(c_npc, npc, sizeof(c_npc));
  csv_text(c_area, recent.area, sizeof(c_area));
  csv_text(c_entrance, recent.entrance, sizeof(c_entrance));
  csv_text(c_arrival, recent.arrival, sizeof(c_arrival));
  csv_text(c_target, gd_get_str(UI_MESSAGE_TARGET), sizeof(c_target));
  char extra[1024];
  snprintf(extra, sizeof(extra), "%s,%s,%s,%s,%s,%s,%s,%s,%s", c_cause, c_event, event_ago_text, c_npc, c_area,
           area_ago_text, c_entrance, c_arrival, c_target);
  add_row_extra(&story_out, false, "Enum", hash, old_text, new_text, "", extra); /* not menu text */
  log_printf("story event: %s %s -> %s (%s)", flagname(hash), old_text, new_text, cause);
}

/* "Event flag written to CSV" in the log panel (bottom right), at most once every NOTICE_GAP frames: a
 * cutscene can set several flags in a few frames, so the ones in between are counted into the next notice */
static void notice_frame_end(void)
{
  uint32_t now = frame_counter();
  if (!notice_pending || (notice_frame && now - notice_frame < NOTICE_GAP))
    return;
  if (notice_pending == 1)
    gfx_notice("Event flag written to CSV");
  else
    gfx_logf("%d event flags written to CSV", notice_pending);
  notice_pending = 0;
  notice_frame = now ? now : 1;
}

/* A flag change for the log menu's "event flags" filter (its own list, so other messages don't push it out) and
 * the log ("event flag: ...", also in log.txt). */
static void keep_flag_line(uint32_t hash, const char *old_text, const char *new_text, bool by_module)
{
  const char *name = flagname(hash), *date, *brief;
  if (!name)
    return; /* (only named fields are watched) */
  local_time(&date, &brief);
  const char *mark = by_module ? " (sloperinaGZ)" : ""; /* not menu text */
  snprintf(flag_lines[flag_head], FLAG_LINE_LEN, "%s%s%s %s -> %s%s", brief, brief[0] ? " " : "", name, old_text,
           new_text, mark);
  flag_head = (flag_head + 1) % FLAG_LINES;
  if (flag_count < FLAG_LINES)
    flag_count++;
  log_printf("event flag: %s %s -> %s%s", name, old_text, new_text, mark);
}

const char *flaglog_line(int i)
{
  if (i < 0 || i >= flag_count)
    return 0;
  return flag_lines[(flag_head - 1 - i + FLAG_LINES) % FLAG_LINES];
}

/* --- the menu's lines ------------------------------------------------------------------------------- */

static void sink_text(const sink_t *out, bool enabled, const char *what, char *buf, int len)
{
  const char *file = out->path[0] ? out->path + sizeof(FLAGLOG_DIR) : "";
  if (!enabled)
    snprintf(buf, len, TF("%s: off"), T(what));
  else if (!settings_sd())
    snprintf(buf, len, TF("%s: no SD card access"), T(what));
  else if (out->failed)
    snprintf(buf, len, TF("%s: couldn't write the file (see the log)"), T(what));
  else if (out->open)
    snprintf(buf, len, TF("%s: %s, %u rows"), T(what), file, (unsigned)out->rows);
  else
    snprintf(buf, len, TF("%s: waiting for the game's fields"), T(what));
}

void flaglog_file_text(char *buf, int len)
{
  sink_text(&flags_out, flaglog_enabled, "flags", buf, len);
}

void flaglog_status_text(char *buf, int len)
{
  sink_text(&status_out, flaglog_status_enabled, "player status", buf, len);
}

void flaglog_story_text(char *buf, int len)
{
  sink_text(&story_out, flaglog_story_enabled, "story events", buf, len);
}
