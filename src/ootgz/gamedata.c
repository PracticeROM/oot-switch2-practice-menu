#include "gamedata.h"

#include "sys.h"
#include "z64hd.h"

/* --- hash ------------------------------------------------------------------------------ */

static uint32_t rotl32(uint32_t x, int r) { return (x << r) | (x >> (32 - r)); }

/* MurmurHash3_x86_32 with seed 0 (the constants are the algorithm's own). */
uint32_t murmur3(const char *s)
{
  const uint32_t c1 = 0xCC9E2D51, c2 = 0x1B873593;
  uint32_t hash = 0, len = 0;
  while (s[len])
    len++;
  const uint8_t *bytes = (const uint8_t *)s;
  uint32_t i;
  for (i = 0; i + 4 <= len; i += 4) {
    uint32_t block = bytes[i] | bytes[i + 1] << 8 | bytes[i + 2] << 16 | (uint32_t)bytes[i + 3] << 24;
    block = rotl32(block * c1, 15) * c2;
    hash = rotl32(hash ^ block, 13) * 5 + 0xE6546B64;
  }
  uint32_t tail = 0;
  switch (len & 3) {
    case 3: tail ^= bytes[i + 2] << 16; /* fall through */
    case 2: tail ^= bytes[i + 1] << 8;  /* fall through */
    case 1: tail ^= bytes[i];
            hash ^= rotl32(tail * c1, 15) * c2;
  }
  hash ^= len;
  hash = (hash ^ (hash >> 16)) * 0x85EBCA6B;
  hash = (hash ^ (hash >> 13)) * 0xC2B2AE35;
  return hash ^ (hash >> 16);
}

/* --- manager and type entries ---------------------------------------------------------- */

typedef bool (*gd_getint_t)(void *mgr, int32_t *out, uint32_t hash);
typedef void (*gd_setint_t)(void *mgr, int32_t value, uint32_t hash);

/* Where each type's values live in its field objects. */
static const struct {
  uint32_t  stride;     /* object size */
  uint32_t  value_off;  /* value offset in the object; a copy follows it */
} type_layouts[GD_NTYPES] = {
  [GD_INT]   = { 0x28, 0x1C },
  [GD_BOOL]  = { 0x18, 0x16 },
  [GD_FLOAT] = { 0x28, 0x1C },
  [GD_ENUM]  = { 0x38, 0x1C },
};

/* sanity limits for a type entry read from memory */
#define MAX_FIELD_COUNT     0x10000
#define MAX_HASH_TABLE_SIZE 0x100000


static uint32_t type_entry_off[GD_NTYPES]; /* offset of each type entry in the manager; 0 = not found */

void *gd_manager(void)
{
  uintptr_t holder = sys_ptr(MAIN(z64hd_game->gd_mgr_ptr));
  return (void *)(holder ? sys_ptr(holder) : 0);
}

void gd_reset(void)
{
  for (int i = 0; i < GD_NTYPES; i++)
    type_entry_off[i] = 0;
}

static bool type_plausible(const gd_type_t *type)
{
  if (!sys_mapped((uintptr_t)type, sizeof(*type), SYS_PERM_R))
    return false;
  if (type->count == 0 || type->count >= MAX_FIELD_COUNT || type->table_size == 0 ||
      type->table_size >= MAX_HASH_TABLE_SIZE)
    return false;
  return sys_mapped((uintptr_t)type->table, (size_t)type->table_size * 8, SYS_PERM_R) &&
         sys_mapped((uintptr_t)type->objects, 8, SYS_PERM_R | SYS_PERM_W);
}

/* The manager's type entry for `kind`: Int at its fixed offset, the others found by vtable
 * (the offset is cached). */
static gd_type_t *find_type_entry(gd_kind_t kind)
{
  uint8_t *mgr = gd_manager();
  if (!mgr)
    return 0;
  if (kind == GD_INT)
    return type_plausible((gd_type_t *)(mgr + Z64HD_GD_INT_TYPE)) ?
           (gd_type_t *)(mgr + Z64HD_GD_INT_TYPE) : 0;
  const uint32_t vtable_offsets[GD_NTYPES] = {
    [GD_BOOL] = z64hd_game->vt_bool_type,
    [GD_FLOAT] = z64hd_game->vt_float_type,
    [GD_ENUM] = z64hd_game->vt_enum_type,
  };
  uintptr_t vtable = MAIN(vtable_offsets[kind]);
  if (!sys_mapped((uintptr_t)mgr, Z64HD_GD_TYPES_SCAN + sizeof(gd_type_t), SYS_PERM_R))
    return 0;
  if (type_entry_off[kind]) {
    gd_type_t *type = (gd_type_t *)(mgr + type_entry_off[kind]);
    if (type->vtable == vtable && type_plausible(type))
      return type;
    type_entry_off[kind] = 0;
  }
  for (uint32_t offset = 0; offset < Z64HD_GD_TYPES_SCAN; offset += 8) {
    gd_type_t *type = (gd_type_t *)(mgr + offset);
    if (type->vtable == vtable && type_plausible(type)) {
      type_entry_off[kind] = offset;
      return type;
    }
  }
  return 0;
}

/* The (hash, index) pair for `hash`, probing like GetInt does. */
static uint32_t *gd_lookup(const gd_type_t *type, uint32_t hash)
{
  if (!hash || !type->table_size)
    return 0;
  uint32_t start = hash % type->table_size;
  uint32_t slot = start;
  do {
    uint32_t *pair = type->table + 2 * slot;
    if (pair[0] == hash)
      return pair;
    if (pair[0] == 0)
      return 0;
    slot = slot + 1 >= type->table_size ? 0 : slot + 1;
  } while (slot != start);
  return 0;
}

/* The value of field `hash` of type `kind` (its copy follows it), or 0. */
static uint8_t *gd_value(gd_kind_t kind, uint32_t hash)
{
  gd_type_t *type = find_type_entry(kind);
  if (!type)
    return 0;
  uint32_t *pair = gd_lookup(type, hash);
  if (!pair || pair[1] >= type->count)
    return 0;
  return type->objects + (size_t)pair[1] * type_layouts[kind].stride + type_layouts[kind].value_off;
}

bool gd_ready(void)
{
  gd_type_t *type = find_type_entry(GD_INT);
  return type && sys_ptr((uintptr_t)type->objects) == MAIN(z64hd_game->vt_int_field);
}

/* --- Int ------------------------------------------------------------------------------- */

bool gd_get_int(uint32_t hash, int32_t *out)
{
  void *mgr = gd_manager();
  if (!mgr || !gd_ready())
    return false;
  *out = 0;
  return MAIN_FN(gd_getint_t, z64hd_game->gd_getint)(mgr, out, hash);
}

void gd_set_int(uint32_t hash, int32_t value)
{
  void *mgr = gd_manager();
  if (mgr && gd_ready())
    MAIN_FN(gd_setint_t, z64hd_game->gd_setint)(mgr, value, hash);
}

bool gd_poke_int(uint32_t hash, int32_t value)
{
  int32_t *v = (int32_t *)gd_value(GD_INT, hash);
  if (!v)
    return false;
  v[0] = v[1] = value;
  return true;
}

/* --- other types (direct, value + copy, as game.py) -------------------------------------- */

bool gd_get_bool(uint32_t hash, bool *out)
{
  uint8_t *v = gd_value(GD_BOOL, hash);
  if (v)
    *out = v[0] != 0;
  return v != 0;
}

bool gd_set_bool(uint32_t hash, bool value)
{
  uint8_t *v = gd_value(GD_BOOL, hash);
  if (v)
    v[0] = v[1] = value;
  return v != 0;
}

bool gd_get_float(uint32_t hash, float *out)
{
  float *v = (float *)gd_value(GD_FLOAT, hash);
  if (v)
    *out = v[0];
  return v != 0;
}

bool gd_set_float(uint32_t hash, float value)
{
  float *v = (float *)gd_value(GD_FLOAT, hash);
  if (v)
    v[0] = v[1] = value;
  return v != 0;
}

bool gd_get_enum(uint32_t hash, uint32_t *out)
{
  uint32_t *v = (uint32_t *)gd_value(GD_ENUM, hash);
  if (v)
    *out = v[0];
  return v != 0;
}

bool gd_set_enum(uint32_t hash, uint32_t value)
{
  uint32_t *v = (uint32_t *)gd_value(GD_ENUM, hash);
  if (v)
    v[0] = v[1] = value;
  return v != 0;
}

/* --- Vector3 ----------------------------------------------------------------------------- */

/* Nothing of the Vector3 fields' layout is known from the game's code (no build's vtable for their type, no
 * object size), so it's worked out from what's there, and nothing is shown until every step agrees:
 *   the type entry   the one of the manager's whose hash table holds two fields known to be Vector3s;
 *   the object size  the objects all start with the same vtable (in main): how far apart those are;
 *   the value        the fields this far keep their value twice, the copy right after it (Int: +0x1C and
 *                    +0x20). So an object's value is where 12 bytes are followed by the same 12 again, and
 *                    aren't all zero; where a default comes before them and is the same too, the later
 *                    place. Taken from the objects that have such a place, when they agree.
 * Found once and kept; looked for again when the manager's entry stops looking right (another save). What was
 * found goes to the log, with the first object's words, so it can be checked. Not yet checked in game. */
#define VEC3_PROBE_A GD_VEC3_PLAYER_POS_FOR_MAP
#define VEC3_PROBE_B 0x52046B85u /* Sequence_ReservedNextPosition */
#define VEC3_STRIDE_MIN 0x28
#define VEC3_STRIDE_MAX 0x80
#define VEC3_HEADER 0x14 /* the fields' common header: vtable, reset bits, state, save file index */
#define VEC3_BYTES 12
#define VEC3_SEARCH_FRAMES 120 /* between tries while it isn't found */
#define MAIN_SIZE_MAX 0x5000000 /* main's image is smaller than this */

static uint32_t vec3_type_off, vec3_stride, vec3_value_off;
static uint32_t vec3_wait;

static bool vec3_type_ok(const gd_type_t *type)
{
  if (!type_plausible(type))
    return false;
  const uint32_t *a = gd_lookup(type, VEC3_PROBE_A), *b = gd_lookup(type, VEC3_PROBE_B);
  return a && b && a[1] < type->count && b[1] < type->count;
}

static bool same_bytes(const uint8_t *a, const uint8_t *b, int n)
{
  for (int i = 0; i < n; i++)
    if (a[i] != b[i])
      return false;
  return true;
}

/* where an object keeps its value, by the copy after it; 0 if it has no such place (a value of all zeros) */
static uint32_t vec3_value_place(const uint8_t *object, uint32_t stride)
{
  static const uint8_t zeros[VEC3_BYTES];
  uint32_t place = 0;
  for (uint32_t at = VEC3_HEADER; at + 2 * VEC3_BYTES <= stride; at += 4)
    if (same_bytes(object + at, object + at + VEC3_BYTES, VEC3_BYTES) && !same_bytes(object + at, zeros, VEC3_BYTES))
      place = at; /* the last one: after a default that's the same */
  return place;
}

static bool vec3_find_layout(const gd_type_t *type)
{
  const uint8_t *objects = type->objects;
  if (type->count < 3 || !sys_mapped((uintptr_t)objects, 8, SYS_PERM_R))
    return false;
  uintptr_t vtable = *(const uintptr_t *)objects;
  if (vtable < z64hd_main || vtable >= z64hd_main + MAIN_SIZE_MAX)
    return false;
  uint32_t stride = 0;
  for (uint32_t s = VEC3_STRIDE_MIN; s <= VEC3_STRIDE_MAX && !stride; s += 8)
    if (sys_mapped((uintptr_t)objects, 2 * (size_t)s + 8, SYS_PERM_R) && *(const uintptr_t *)(objects + s) == vtable &&
        *(const uintptr_t *)(objects + 2 * s) == vtable)
      stride = s;
  if (!stride || !sys_mapped((uintptr_t)objects, (size_t)type->count * stride, SYS_PERM_R))
    return false;
  uint32_t place = 0;
  int agree = 0;
  for (uint32_t i = 0; i < type->count; i++) {
    uint32_t here = vec3_value_place(objects + (size_t)i * stride, stride);
    if (!here)
      continue;
    if (place && here != place)
      return false; /* they don't agree: the layout isn't what's assumed */
    place = here;
    agree++;
  }
  if (!place) { /* every value is zero for now (the title screen), or they aren't kept as assumed: later */
    static bool logged;
    const uint32_t *w = (const uint32_t *)objects;
    if (!logged)
      log_printf("gamedata: Vector3 fields: 0x%x bytes each, no value found yet; first: %08x %08x %08x %08x %08x %08x "
                 "%08x %08x %08x %08x", (unsigned)stride, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9]);
    logged = true;
    return false;
  }
  vec3_stride = stride;
  vec3_value_off = place;
  const uint32_t *words = (const uint32_t *)objects;
  log_printf("gamedata: Vector3 fields: %u of 0x%x bytes, value at +0x%x (%d agree); first: %08x %08x %08x %08x %08x "
             "%08x %08x %08x %08x %08x",
             (unsigned)type->count, (unsigned)stride, (unsigned)place, agree, words[0], words[1], words[2], words[3],
             words[4], words[5], words[6], words[7], words[8], words[9]);
  return true;
}

const float *gd_vec3(uint32_t hash)
{
  uint8_t *mgr = gd_manager();
  if (!mgr)
    return 0;
  gd_type_t *type = vec3_type_off ? (gd_type_t *)(mgr + vec3_type_off) : 0;
  if (type && !vec3_type_ok(type)) {
    type = 0;
    vec3_type_off = vec3_stride = vec3_value_off = 0;
  }
  if (!type || !vec3_value_off) {
    if (vec3_wait) { /* not each frame: a miss walks the manager and every object */
      vec3_wait--;
      return 0;
    }
    vec3_wait = VEC3_SEARCH_FRAMES;
    if (!type) {
      if (!sys_mapped((uintptr_t)mgr, Z64HD_GD_TYPES_SCAN + sizeof(gd_type_t), SYS_PERM_R))
        return 0;
      for (uint32_t offset = 0; offset < Z64HD_GD_TYPES_SCAN && !type; offset += 8)
        if (vec3_type_ok((gd_type_t *)(mgr + offset))) {
          type = (gd_type_t *)(mgr + offset);
          vec3_type_off = offset;
        }
      if (!type)
        return 0;
    }
    if (!vec3_find_layout(type))
      return 0;
    vec3_wait = 0;
  }
  const uint32_t *pair = gd_lookup(type, hash);
  if (!pair || pair[1] >= type->count)
    return 0;
  uintptr_t value = (uintptr_t)type->objects + (size_t)pair[1] * vec3_stride + vec3_value_off;
  return sys_mapped(value, VEC3_BYTES, SYS_PERM_R) ? (const float *)value : 0;
}

/* --- String64 ---------------------------------------------------------------------------- */

/* Follow the same hash table and value pointer as the game's GetString64. */
static char *string_value(uint32_t hash)
{
  uint8_t *mgr = gd_manager();
  if (!mgr)
    return 0;
  const gd_type_t *t = (const gd_type_t *)(mgr + Z64HD_GD_STRING_TYPE);
  if (!type_plausible(t))
    return 0;
  uint32_t index = 0;
  if (hash) {
    const uint32_t *e = gd_lookup(t, hash);
    if (!e || e[1] >= t->count)
      return 0;
    index = e[1];
  }
  uintptr_t field = (uintptr_t)t->objects + (size_t)index * Z64HD_STR_STRIDE + Z64HD_STR_VALUE;
  uintptr_t text = sys_ptr(field);
  uint32_t capacity;
  if (!text || !sys_read32(field + sizeof(uintptr_t), &capacity) || capacity < GD_STR_LEN ||
      !sys_mapped(text, GD_STR_LEN, SYS_PERM_R | SYS_PERM_W))
    return 0;
  return (char *)text;
}

const char *gd_get_str(uint32_t hash)
{
  return string_value(hash);
}

bool gd_set_str(uint32_t hash, const char *s)
{
  char *d = hash ? string_value(hash) : 0;
  if (!d)
    return false;
  int i = 0;
  for (; i < GD_STR_LEN - 1 && s[i]; i++)
    d[i] = s[i];
  for (; i < GD_STR_LEN; i++)
    d[i] = 0;
  return true;
}

/* --- the game's pause ---------------------------------------------------------------------------- */

#define GD_SEQUENCE_IS_PAUSE 0x604AEBE1u                   /* Sequence_IsPause (bool) */
#define GD_SEQUENCE_GAME_PAUSE_REQUEST_COUNT 0x64A61098u   /* Sequence_GamePauseRequestCount (int) */
#define GD_SEQUENCE_IS_PRE_PAUSE 0xBF5BF25Fu               /* Sequence_IsPreGamePause (bool) */
#define GD_SEQUENCE_PRE_PAUSE_REQUEST_COUNT 0xEF935C2Du    /* Sequence_PreGamePauseRequestCount (int) */

bool gd_game_paused(bool *is_pause, int32_t *pause_requests)
{
  bool pause = false;
  int32_t requests = 0;
  gd_get_bool(GD_SEQUENCE_IS_PAUSE, &pause);
  gd_get_int(GD_SEQUENCE_GAME_PAUSE_REQUEST_COUNT, &requests);
  if (is_pause)
    *is_pause = pause;
  if (pause_requests)
    *pause_requests = requests;
  return pause || requests > 0;
}

bool gd_game_pausing(void)
{
  bool pre = false;
  int32_t requests = 0;
  gd_get_bool(GD_SEQUENCE_IS_PRE_PAUSE, &pre);
  gd_get_int(GD_SEQUENCE_PRE_PAUSE_REQUEST_COUNT, &requests);
  return pre || requests > 0;
}
