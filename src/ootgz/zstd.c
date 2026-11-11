/* zstd decompression for the game's RomFS files: zstd's educational decoder
 * (third_party/zstd_decompress.c, BSD or GPLv2) with the module's libc (third_party/zstd_shim.h).
 *
 * The decoder handles every error by calling ERROR(), which upstream exits on. Here it jumps back to
 * the call that started the work (jump.h), and every block the decoder allocated during that call is
 * freed. The calls run on one thread (the game's), so the jump buffer and the allocation list are plain
 * statics. A stream's decoder allocates from memory the stream took at its start (its arena, below). */
#include "zstd.h"

#include <stdbool.h>

#include "jump.h"
#include "sys.h"

static jump_buf_t jump;

void *aligned_alloc(size_t align, size_t size); /* from main */
void free(void *p);                             /* from main */
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
void *memmove(void *dst, const void *src, size_t n);

#define ALLOC_ALIGN 16
#define MAX_LIVE 256 /* blocks the decoder holds at once: a few per block, plus a dictionary's tables */

const char zstd_no_memory[] = "out of memory"; /* not menu text */

static const char *fail_why;
static bool out_of_memory; /* an allocation failed during this call */
static void *live[MAX_LIVE];
static int live_count;

/* --- a stream's arena ----------------------------------------------------------------------------
 * A stream's decoder allocates a few times each block, hundreds of times in a big file. From the heap,
 * the decoding could run out half way, after the game took the memory meanwhile. So a stream takes what
 * its decoder needs at its start, in two parts after the decoder's pattern:
 *   small: its Huffman and FSE tables, kept from block to block, a slot each. In a frame a real encoder
 *     makes they're at most 4 KiB (12-bit Huffman, 9-bit FSE), each freed before the one replacing it is
 *     made: at most 11 live at once (the literals' Huffman table: 2 blocks; the sequences' three FSE
 *     tables: 3 each), and the small literals and sequences of a block besides (13 seen in the game's
 *     files).
 *   big: a block's literals and its sequences, freed at the block's end. They're put one after the other,
 *     from the start again once neither is left. Literals L bytes and N sequences (12 bytes each) take at
 *     most a block's 128 KiB: each sequence's match is 3 bytes or more, so 3N <= 128 KiB - L, and
 *     L + 12N <= 4 x 128 KiB - 3L.
 * A frame needing more is damaged. What the arena can't hold otherwise comes from the heap. */
#define STREAM_BLOCK_MAX ((size_t)128 * 1024) /* a block's most output, and most input */
#define ARENA_SMALL 4096
#define ARENA_SLOTS 16
#define ARENA_BIG (4 * STREAM_BLOCK_MAX + 2 * ALLOC_ALIGN)
#define ARENA_SIZE (ARENA_SLOTS * ARENA_SMALL + ARENA_BIG)
_Static_assert(ARENA_SLOTS <= 32, "a bit each in small_used");

typedef struct {
  uint8_t *mem; /* the small slots, then the big part */
  uint32_t small_used;
  size_t big_at; /* the big part's first free byte */
  int big_live;  /* blocks in the big part */
} arena_t;
static arena_t *arena; /* the stream's during its calls, else 0 */

__attribute__((noreturn)) void zstd_shim_fail(const char *why);

static void arena_clear(arena_t *a)
{
  a->small_used = 0;
  a->big_at = 0;
  a->big_live = 0;
}

void *zstd_shim_alloc(size_t size)
{
  /* at least one unit: the decoder asks for 0 bytes (a block with no literals), and NULL is an error */
  size_t n = size ? (size + ALLOC_ALIGN - 1) & ~(size_t)(ALLOC_ALIGN - 1) : ALLOC_ALIGN;
  if (arena && n > ARENA_BIG)
    zstd_shim_fail("a block bigger than the format allows"); /* damaged, not out of memory (not menu text) */
  if (arena) {
    if (n <= ARENA_SMALL && ~arena->small_used) {
      int slot = __builtin_ctz(~arena->small_used);
      arena->small_used |= 1u << slot;
      return arena->mem + (size_t)slot * ARENA_SMALL;
    }
    if (n <= ARENA_BIG - arena->big_at) {
      void *p = arena->mem + ARENA_SLOTS * ARENA_SMALL + arena->big_at;
      arena->big_at += n;
      arena->big_live++;
      return p;
    }
  }
  if (live_count == MAX_LIVE) {
    log_printf("zstd: more than %d blocks at once", MAX_LIVE);
    return 0; /* the decoder reports it as an allocation error (more memory wouldn't help) */
  }
  void *p = aligned_alloc(ALLOC_ALIGN, n);
  if (p) {
    live[live_count++] = p;
  } else {
    log_printf("zstd: no memory for %u bytes", (unsigned)size);
    out_of_memory = true;
  }
  return p;
}

void *zstd_shim_calloc(size_t count, size_t size)
{
  if (size && count > (size_t)-1 / size)
    return 0;
  void *p = zstd_shim_alloc(count * size);
  if (p)
    memset(p, 0, count * size);
  return p;
}

void zstd_shim_free(void *p)
{
  if (!p)
    return;
  uintptr_t at = (uintptr_t)p - (uintptr_t)(arena ? arena->mem : 0);
  if (arena && at < ARENA_SIZE) {
    if (at < ARENA_SLOTS * ARENA_SMALL)
      arena->small_used &= ~(1u << (at / ARENA_SMALL));
    else if (--arena->big_live == 0)
      arena->big_at = 0;
    return;
  }
  for (int i = live_count - 1; i >= 0; i--) {
    if (live[i] == p) {
      live[i] = live[--live_count];
      break;
    }
  }
  free(p);
}

__attribute__((noreturn)) void zstd_shim_fail(const char *why)
{
  fail_why = why;
  JUMP_BACK(jump);
}

/* one call into the decoder: on an error, what it allocated is freed */
static void free_live(void)
{
  while (live_count)
    free(live[--live_count]);
}

/* an error's reason, for *why */
static const char *failed_why(void)
{
  return out_of_memory ? zstd_no_memory : fail_why ? fail_why : "error";
}

/* the decoder itself; its own names are static to this file except the four it exports */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "third_party/zstd_decompress.c"
#pragma GCC diagnostic pop
#undef malloc /* zstd_shim.h's, for the decoder only */
#undef calloc
#undef free

#define ZSTD_FRAME_MAGIC 0xFD2FB528u

size_t zstd_content_size(const void *src, size_t len)
{
  const uint8_t *s = src;
  if (len < 6 || (s[0] | s[1] << 8 | s[2] << 16 | (uint32_t)s[3] << 24) != ZSTD_FRAME_MAGIC)
    return 0;
  live_count = 0;
  if (JUMP_SET(jump)) {
    free_live();
    return 0;
  }
  size_t n = ZSTD_get_decompressed_size(src, len);
  live_count = 0;
  return n == (size_t)-1 ? 0 : n;
}

uint32_t zstd_frame_dict_id(const void *src, size_t len)
{
  /* frame header: magic, descriptor, the window byte unless single segment, then 0/1/2/4 bytes */
  const uint8_t *s = src;
  if (len < 5 || (s[0] | s[1] << 8 | s[2] << 16 | (uint32_t)s[3] << 24) != ZSTD_FRAME_MAGIC)
    return 0;
  uint8_t descriptor = s[4];
  static const int id_bytes[4] = { 0, 1, 2, 4 };
  int n = id_bytes[descriptor & 3];
  size_t at = 5 + ((descriptor & 0x20) ? 0 : 1);
  if (!n || at + n > len)
    return 0;
  uint32_t id = 0;
  for (int i = 0; i < n; i++)
    id |= (uint32_t)s[at + i] << (8 * i);
  return id;
}

void zstd_dict_free(zstd_dict_t *dict)
{
  if (dict)
    free_dictionary((dictionary_t *)dict); /* its tables, its content and itself, through zstd_shim_free */
}

zstd_dict_t *zstd_dict_load(const void *data, size_t len)
{
  live_count = 0;
  if (JUMP_SET(jump)) {
    free_live();
    return 0;
  }
  dictionary_t *dict = create_dictionary();
  parse_dictionary(dict, data, len);
  live_count = 0; /* what it allocated is the dictionary's now */
  return (zstd_dict_t *)dict;
}

size_t zstd_decompress(void *dst, size_t cap, const void *src, size_t len, const zstd_dict_t *dict,
                       const char **why)
{
  live_count = 0;
  fail_why = 0;
  out_of_memory = false;
  if (JUMP_SET(jump)) {
    free_live();
    if (why)
      *why = failed_why();
    return 0;
  }
  size_t n = dict ? ZSTD_decompress_with_dict(dst, cap, src, len, (dictionary_t *)dict)
                  : ZSTD_decompress(dst, cap, src, len);
  free_live(); /* the decoder frees its blocks itself; this only catches a leak */
  return n;
}

/* --- a frame a block at a time (zstd_stream_t) ------------------------------------------------ */

#define STREAM_IN_SIZE ((size_t)256 * 1024) /* read at a time (a block, compressed, fits) */
/* output past the window before it slides back: less memory, against more copying (the whole window is
 * copied back each time: for the game's Common archive, 8 MiB for every MiB of output) */
#define STREAM_SLIDE_ROOM ((size_t)1024 * 1024)
#define STREAM_SLIDE_PIECE STREAM_SLIDE_ROOM /* the slide is done this much at a time, a step each */
#define STREAM_MAX_WINDOW ((size_t)16 * 1024 * 1024)

struct zstd_stream {
  zstd_read_t read;
  void *user;
  uint64_t read_at;      /* the file offset of the next byte to read */
  uint64_t total;        /* bytes output so far */
  uint8_t *in, *out;
  size_t in_pos, in_len; /* in[in_pos, in_len): read, not decoded yet */
  size_t out_pos, out_cap, window;
  size_t slide_from, slide_done, slide_len; /* a slide of out[slide_from...] back to out[0...] under way */
  bool in_end, done, failed;
  frame_context_t ctx;
  arena_t arena;
  void *live[MAX_LIVE];  /* what the decoder took from the heap: its tables live from step to step */
  int live_count;
};

/* the stream's allocations become the ones zstd.c tracks (an error frees them all), and back */
static void stream_enter(zstd_stream_t *s)
{
  memcpy(live, s->live, sizeof(live));
  live_count = s->live_count;
  arena = s->arena.mem ? &s->arena : 0;
  fail_why = 0;
  out_of_memory = false;
}

static void stream_leave(zstd_stream_t *s)
{
  memcpy(s->live, live, sizeof(live));
  s->live_count = live_count;
  live_count = 0;
  arena = 0;
}

/* at least `need` bytes in in[in_pos...], unless the file ends first */
static void stream_fill(zstd_stream_t *s, size_t need)
{
  if (s->in_len - s->in_pos >= need || s->in_end)
    return;
  size_t keep = s->in_len - s->in_pos;
  memmove(s->in, s->in + s->in_pos, keep);
  s->in_pos = 0;
  s->in_len = keep;
  while (s->in_len < STREAM_IN_SIZE && !s->in_end) {
    size_t n = s->read(s->user, s->read_at, s->in + s->in_len, STREAM_IN_SIZE - s->in_len);
    if (!n)
      s->in_end = true;
    s->read_at += n;
    s->in_len += n;
  }
}

static void stream_release(zstd_stream_t *s)
{
  stream_enter(s);
  free_live(); /* what the decoder took from the heap */
  stream_leave(s);
  free(s->arena.mem);
  free(s->in);
  free(s->out);
  free(s);
}

zstd_stream_t *zstd_stream_new(zstd_read_t read, void *user, const char **why)
{
  zstd_stream_t *s = aligned_alloc(ALLOC_ALIGN, sizeof(*s));
  if (s) {
    memset(s, 0, sizeof(*s));
    s->in = aligned_alloc(ALLOC_ALIGN, STREAM_IN_SIZE);
  }
  if (!s || !s->in) {
    free(s);
    *why = zstd_no_memory;
    return 0;
  }
  s->read = read;
  s->user = user;
  stream_fill(s, 32); /* the magic number and the frame header (at most 14 bytes) */
  stream_enter(s);
  if (JUMP_SET(jump)) {
    const char *reason = failed_why();
    stream_leave(s); /* what the header's decoding allocated, freed with the rest */
    stream_release(s);
    *why = reason;
    return 0;
  }
  istream_t in = IO_make_istream(s->in, s->in_len);
  if ((uint32_t)IO_read_bits(&in, 32) != ZSTD_FRAME_MAGIC)
    ERROR("not a zstd frame"); /* not menu text */
  init_frame_context(&s->ctx, &in, NULL);
  if (s->ctx.header.dictionary_id)
    ERROR("it needs a dictionary"); /* not menu text */
  s->window = s->ctx.header.window_size;
  if (!s->window || s->window > STREAM_MAX_WINDOW)
    ERROR("its window is too big to stream"); /* not menu text */
  s->out_cap = s->window + STREAM_SLIDE_ROOM + STREAM_BLOCK_MAX;
  size_t content = s->ctx.header.frame_content_size; /* 0: the header doesn't say */
  if (content && content < s->out_cap - STREAM_BLOCK_MAX)
    s->out_cap = content + STREAM_BLOCK_MAX; /* all of it fits: it never slides */
  /* all the memory at the start: the decoding can't run out half way */
  s->out = aligned_alloc(ALLOC_ALIGN, s->out_cap);
  s->arena.mem = s->out ? aligned_alloc(ALLOC_ALIGN, ARENA_SIZE) : 0;
  if (!s->arena.mem) {
    out_of_memory = true;
    ERROR("out of memory"); /* not menu text */
  }
  s->in_pos = (size_t)(in.ptr - s->in);
  stream_leave(s);
  return s;
}

int zstd_stream_step(zstd_stream_t *s, size_t budget, zstd_sink_t sink, void *user, const char **why)
{
  if (s->failed)
    return -1;
  if (s->done)
    return 0;
  stream_enter(s);
  if (JUMP_SET(jump)) {
    *why = failed_why();
    free_live(); /* the decoder's tables too: the stream is over */
    arena_clear(&s->arena);
    memset(&s->ctx, 0, sizeof(s->ctx));
    stream_leave(s);
    s->failed = true;
    return -1;
  }
  size_t produced = 0;
  while (produced < budget && !s->done) {
    if (s->slide_len) { /* the window back to the start, a piece a step (the pieces don't overlap) */
      size_t n = s->slide_len - s->slide_done < STREAM_SLIDE_PIECE ? s->slide_len - s->slide_done : STREAM_SLIDE_PIECE;
      memmove(s->out + s->slide_done, s->out + s->slide_from + s->slide_done, n);
      s->slide_done += n;
      if (s->slide_done == s->slide_len) {
        s->out_pos = s->slide_len;
        s->slide_len = 0;
      }
      produced += n; /* counted as work done */
      break;
    }
    stream_fill(s, 3);
    if (s->in_len - s->in_pos < 3)
      ERROR("it ends early"); /* not menu text */
    const uint8_t *head = s->in + s->in_pos;
    uint32_t block = head[0] | head[1] << 8 | (uint32_t)head[2] << 16;
    bool last = block & 1;
    int type = (int)(block >> 1 & 3);
    size_t size = block >> 3;
    if (type == 3 || size > STREAM_BLOCK_MAX)
      ERROR("a damaged block header"); /* not menu text */
    size_t need = 3 + (type == 1 ? 1 : size);
    stream_fill(s, need);
    if (s->in_len - s->in_pos < need)
      ERROR("it ends early"); /* not menu text */
    head = s->in + s->in_pos;
    if (s->out_pos + STREAM_BLOCK_MAX > s->out_cap) { /* back to the start, keeping the window */
      if (s->out_pos <= s->window)
        ERROR("it's longer than its header says"); /* out_cap was made for its size; not menu text */
      size_t keep = s->window;
      s->slide_from = s->out_pos - keep; /* (memmove: each piece lands before what's still to move) */
      s->slide_done = 0;
      s->slide_len = keep;
      continue;
    }
    uint8_t *dst = s->out + s->out_pos;
    size_t n = size;
    if (type == 0) {
      memcpy(dst, head + 3, size);
      s->ctx.current_total_output += size;
    } else if (type == 1) {
      memset(dst, head[3], size);
      s->ctx.current_total_output += size;
    } else {
      istream_t in = IO_make_istream(head + 3, size);
      ostream_t out = IO_make_ostream(dst, s->out_cap - s->out_pos);
      decompress_block(&s->ctx, &out, &in);
      n = (size_t)(out.ptr - dst);
    }
    s->in_pos += need;
    s->out_pos += n;
    produced += n;
    if (n && !sink(user, s->total, dst, n))
      s->done = true; /* the caller has what it wanted */
    s->total += n;
    if (last)
      s->done = true;
  }
  stream_leave(s);
  return s->done ? 0 : 1;
}

void zstd_stream_free(zstd_stream_t *s)
{
  if (s)
    stream_release(s);
}
