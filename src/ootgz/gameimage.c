/* The game's pictures of its controllers (see gameimage.h).
 *
 * content:/UI/Layout2Archive/Common.Product.100.Nin_Ounce_NVN2.bntx.zs is one zstd frame (no dictionary,
 * an 8 MiB window) of a BNTX: its header, then each texture's packaged data. zstd.c's stream decodes it
 * a block at a time; the header is kept until its texture records (BRTI) are read, then only the wanted
 * textures' bytes are copied, into memory gpuui.c makes a pool of, and the decoding stops after the last
 * of them (about half way). The pictures are found by name, as the game finds them. BNTX, little-endian,
 * offsets from the file's start:
 *   +0x20 the platform ("NX  ", or "Ounc" on the Switch 2), +0x24 the texture count, +0x28 where the records' offsets are (u64 each), +0x30 the data
 *     block's offset (everything before it is header)
 *   a record ("BRTI"): +0x1c the image format (0x2d01 / 0x2d06: ASTC 4x4 unorm / sRGB; 0x1d01: BC4), +0x24
 *     its width, +0x28 its height, +0x34 its packaged layout (8 bytes), +0x50 its data's size, +0x58 its
 *     channels' sources (as NVN's swizzle: 0 zero, 1 one, 2..5 r, g, b, a), +0x60 its name's offset (a u16
 *     length, then the characters), +0x70 where its levels' data offsets are (u64 each).
 * A picture that isn't there (another version of the game) is left out; the controllers are needed.
 *
 * The items' icons come after: UI/Tex/Icon/<name>.bntx.zs, each one frame (no dictionary) of a BNTX with
 * one ASTC 4x4 texture of that name (341 to 1366 pixels square, about 18 MB in all). Kept as they are they'd
 * take more of the game's heap than it can spare, so each is decoded as it's read (astc.c), its blocks put
 * in their places (the packaged layout: GOBs of 64 bytes x 8 rows, stacked 2^layout[0] high in each column
 * of them; on the Switch 2 a GOB's 16-byte sectors go row 0-1 / column / row 2 / column 1, found against
 * the GPU's own drawing of one), and shrunk to GPU_ICON_PX square into gpuui's icon atlas (2.5 MB).
 *
 * The game's heap is small. Decoding the archive takes about 15 MB of it for a few seconds (its 8 MiB window
 * above all, taken at the start with what the decoder needs: zstd.c), so it can't run out half way. Each
 * part (the archive's pictures, the icons) goes on without the other, and one that ran out of memory is
 * tried again later. Once made, the pictures are kept on the SD card (cache, below): later starts read them
 * from there, with no more memory than they take anyway. */
#include "gameimage.h"

#include <stddef.h>
#include <stdint.h>

#include "gamefile.h"
#include "gfx.h"
#include "gpuui.h"
#include "sdfs.h"
#include "settings.h"
#include "sys.h"
#include "astc.h"
#include "z64hd.h"
#include "zstd.h"

void *aligned_alloc(size_t align, size_t size); /* from main */
void free(void *p);                             /* from main */

#define ARCHIVE GAME_ROM "/UI/Layout2Archive/Common.Product.100.Nin_Ounce_NVN2.bntx.zs"
#define ICON_DIR GAME_ROM "/UI/Tex/Icon/"
#define HEADER_MAX (4u << 20)    /* the most header kept */
#define IMAGE_MAX (4u << 20)     /* a picture's most data */
#define IMAGE_ALIGN 0x10000      /* each picture's place in the memory: NVN's strictest texture alignment */
#define PAGE 0x1000
#define STEP_BYTES (64u << 10)   /* decoded between looks at the clock */
#define ICON_STEP_BYTES (8u << 10) /* an icon's (its blocks decoded too) */
#define STEP_MS 2                /* a frame's time for it */
/* a decoding tried again leaves the game this much more: it may be in the middle of a scene */
#define RETRY_HEADROOM (4u << 20)
#define RETRY_MAX 8 /* tries after running out of memory, before giving up */
/* NVN's ASTC 4x4 (the data shown as stored, no sRGB decode, as everything else is drawn) and BC4 */
#define NVN_FORMAT_ASTC_4X4 0x79
#define NVN_FORMAT_BC4 0x49
#define BNTX_ASTC_4X4_UNORM 0x2d01
#define BNTX_ASTC_4X4_SRGB 0x2d06
#define BNTX_BC4_UNORM 0x1d01

static const char *const names[GAMEIMAGES] = {
  [GAMEIMAGE_PRO] = "Controller_Pro", [GAMEIMAGE_GRIP] = "Controller_Grip", [GAMEIMAGE_CONSOLE] = "Controller_Console",
  [GAMEIMAGE_CURSOR_LINE] = "OptionBtnThinCursorLine_00", [GAMEIMAGE_CURSOR_GLOW] = "OptionBtnThinCursorGlow_00",
  [GAMEIMAGE_LINE] = "SystemLine_00", [GAMEIMAGE_SCROLL_BAR] = "ScrollBar_00",
  [GAMEIMAGE_SCROLL_BASE] = "ScrollBarBase_00", [GAMEPIC_HEART] = "CollectHeartBase_00",
  [GAMEIMAGE_GAUGE_BASE] = "MpGaugeBase_00", [GAMEIMAGE_GAUGE_FRAME] = "MpGaugeBaseFrame_00",
  [GAMEIMAGE_GAUGE_FILL] = "MpGaugeCenter_00", [GAMEIMAGE_GAUGE_SHADOW] = "MpGaugeSh_00",
  [GAMEIMAGE_PLATE] = "NumBaseMiniGame_00", [GAMEIMAGE_PLATE_RIM] = "NumBaseMiniGameDeco_00",
  /* the icons: their files' names, and their textures' */
  [GAMEPIC_RUPEE] = "Obj_Rupee_Green", [GAMEPIC_MAGIC] = "Item_FairyBless_MP", [GAMEPIC_STICK] = "Item_DekuStick",
  [GAMEPIC_NUT] = "Item_DekuNut", [GAMEPIC_SEED] = "Item_DekuSeed", [GAMEPIC_BOMB] = "Item_Bomb",
  [GAMEPIC_ARROW] = "Item_Arrow_Normal", [GAMEPIC_BOMBCHU] = "Item_Bombchu", [GAMEPIC_BEAN] = "Item_MagicBean",
  [GAMEPIC_BOOMERANG] = "Item_Boomerang", [GAMEPIC_SLINGSHOT] = "Weapon_Range_SlingShot",
  [GAMEPIC_BOW] = "Weapon_Range_Bow", [GAMEPIC_HAMMER] = "Item_Hammer", [GAMEPIC_ARROW_FIRE] = "Item_Arrow_Fire",
  [GAMEPIC_ARROW_ICE] = "Item_Arrow_Ice", [GAMEPIC_ARROW_LIGHT] = "Item_Arrow_Light",
  [GAMEPIC_HOOKSHOT] = "Item_Hookshot", [GAMEPIC_DIN] = "Item_DinFire", [GAMEPIC_FARORE] = "Item_FaroreWind",
  [GAMEPIC_NAYRU] = "Item_NayruLove", [GAMEPIC_OCARINA_FAIRY] = "Item_Ocarina_Fairy",
  [GAMEPIC_OCARINA_TIME] = "Item_Ocarina_Time", [GAMEPIC_BOTTLE] = "Item_Bottle",
  [GAMEPIC_SWORD_KOKIRI] = "Weapon_Sword_Kokiri", [GAMEPIC_SWORD_MASTER] = "Weapon_Sword_Master",
  [GAMEPIC_SWORD_GIANT] = "Weapon_Sword_Giant", [GAMEPIC_SHIELD_DEKU] = "Weapon_Shield_Deku",
  [GAMEPIC_SHIELD_HYLIAN] = "Weapon_Shield_Hylian", [GAMEPIC_SHIELD_MIRROR] = "Weapon_Shield_Mirror",
  [GAMEPIC_TUNIC_GORON] = "Armor_Adult_Goron", [GAMEPIC_TUNIC_ZORA] = "Armor_Adult_Zora",
  [GAMEPIC_BOOTS_IRON] = "Armor_Adult_HeavyBoot", [GAMEPIC_BOOTS_HOVER] = "Armor_Adult_HoverBoot",
  [GAMEPIC_LENS] = "Armor_GlassTruth", [GAMEPIC_AGONY] = "Item_AgonyStone", [GAMEPIC_GERUDO_CARD] = "Item_Token_Gerudo",
  [GAMEPIC_BRACELET] = "Armor_Child_Gauntlet_Goron", [GAMEPIC_GAUNTLETS] = "Armor_Adult_Gauntlet_Silver",
  [GAMEPIC_SCALE] = "Item_Scale_Silver", [GAMEPIC_SKULLTULA] = "Item_Token_Gold",
  [GAMEPIC_DEFENSE] = "Item_FairyBless_DefenseDouble", [GAMEPIC_WALLET] = "Item_Wallet_Small",
  [GAMEPIC_BOMB_BAG] = "Item_BombBag_Small", [GAMEPIC_QUIVER] = "Item_Quiver_Small",
  [GAMEPIC_BULLET_BAG] = "Item_BulletBag_Small", [GAMEPIC_STICK_BAG] = "Item_StickBag_Small",
  [GAMEPIC_NUT_BAG] = "Item_NutBag_Small", [GAMEPIC_LONGSHOT] = "Item_Longshot",
  [GAMEPIC_SWORD_BROKEN] = "Weapon_Sword_BrokenGiant", [GAMEPIC_SWORD_BIGGORON] = "Weapon_Sword_Biggoron",
  [GAMEPIC_GAUNTLETS_GOLD] = "Armor_Adult_Gauntlet_Gold", [GAMEPIC_SCALE_GOLD] = "Item_Scale_Gold",
  [GAMEPIC_MAGIC_DOUBLE] = "Item_FairyBless_MPDouble", [GAMEPIC_POTION_GREEN] = "Item_Bottle_GreenPotion",
  [GAMEPIC_SCRUB_SEED] = "Item_ScrubBusinessSeed", [GAMEPIC_MASK_KEATON] = "Armor_Child_Mask_Keaton",
  [GAMEPIC_MASK_SKULL] = "Armor_Child_Mask_Skull", [GAMEPIC_MASK_SPOOKY] = "Armor_Child_Mask_Spooky",
  [GAMEPIC_MASK_BUNNY] = "Armor_Child_Mask_Bunny", [GAMEPIC_MASK_GORON] = "Armor_Child_Mask_Goron",
  [GAMEPIC_MASK_ZORA] = "Armor_Child_Mask_Zora", [GAMEPIC_MASK_GERUDO] = "Armor_Child_Mask_Gerudo",
  [GAMEPIC_MASK_TRUTH] = "Armor_Child_Mask_Truth", [GAMEPIC_MASK_FAIRY_KID] = "Armor_Child_Mask_Amiibo0",
  [GAMEPIC_MASK_PRINCESS] = "Armor_Child_Mask_Amiibo1",
  [GAMEPIC_BOTTLE_RED_POTION] = "Item_Bottle_RedPotion", [GAMEPIC_BOTTLE_BLUE_POTION] = "Item_Bottle_BluePotion",
  [GAMEPIC_BOTTLE_BLUE_FIRE] = "Item_Bottle_BlueFire", [GAMEPIC_BOTTLE_FISH] = "Item_Bottle_Fish",
  [GAMEPIC_BOTTLE_LETTER] = "Item_Bottle_Letter", [GAMEPIC_BOTTLE_BIG_POE] = "Item_Bottle_BigPoe",
  [GAMEPIC_BOTTLE_POE] = "Item_Bottle_Poe", [GAMEPIC_BOTTLE_BUG] = "Item_Bottle_Bug",
  [GAMEPIC_BOTTLE_FAIRY] = "Item_Bottle_Fairy", [GAMEPIC_BOTTLE_MILK] = "Item_Bottle_Milk",
  [GAMEPIC_BOTTLE_HALF_MILK] = "Item_Bottle_HalfMilk",
  [GAMEPIC_WEIRD_EGG] = "Item_WeirdEgg", [GAMEPIC_CUCCO] = "Item_AlarmCucco",
  [GAMEPIC_ZELDA_LETTER] = "Item_ZeldaLetter", [GAMEPIC_POCKET_EGG] = "Item_PocketEgg",
  [GAMEPIC_POCKET_CUCCO] = "Item_PocketCucco", [GAMEPIC_COJIRO] = "Item_CojiroCucco",
  [GAMEPIC_ODD_MUSHROOM] = "Item_OddMushroom", [GAMEPIC_ODD_POTION] = "Item_OddPowder",
  [GAMEPIC_POACHER_SAW] = "Item_PoacherSaw", [GAMEPIC_BROKEN_SWORD] = "Item_BrokenSword",
  [GAMEPIC_PRESCRIPTION] = "Item_Prescription", [GAMEPIC_EYEBALL_FROG] = "Item_EyeballFrog",
  [GAMEPIC_EYE_DROPS] = "Item_EyeDrop", [GAMEPIC_CLAIM_CHECK] = "Item_ClaimCheck",
};
_Static_assert(GAMEIMAGES <= GFX_PICTURE_MAX, "each picture a number in the menu's text (gfx.h GFX_PICTURE_ID)");

/* what's under way, one thing at a time: the shared archive's pictures [0, GAMEIMAGES_SHARED) read from
 * the cache or decoded, the icons read from the cache or decoded one file each, a cache file written */
static enum { NOTHING, READ_SHARED, DECODE_SHARED, READ_ICONS, DECODE_ICON, SAVE } task;
/* the two parts: each waits to be read, is tried again later (after running out of memory), is done, or
 * can't be had */
typedef struct {
  enum { PART_WAITING, PART_LATER, PART_DONE, PART_LOST } state;
  uint64_t at;    /* PART_LATER: when (sys_tick) */
  unsigned tries; /* the times it ran out of memory */
} part_t;
static part_t shared, icons;
static int lo, hi;    /* the pictures the file being read has: [lo, hi) */
static int icon;      /* ICONS: the icon being read */
static fs_file_t file;
static bool file_open;
static zstd_stream_t *stream;
static uint8_t *head; /* the file's start, until the records are read */
static uint64_t head_len, head_need;
static bool parsed;
static const char *sink_why; /* the sink gave up: why */
static struct {
  uint64_t at, got; /* where its data is in the file, how much of it is here */
  gpu_image_t gpu;
} want[GAMEIMAGES];
static uint8_t *mem; /* the shared pictures' */
static size_t mem_size, mem_taken; /* what they need (0: not known yet), what mem holds */
/* a reason a part is tried again later, like zstd_no_memory (compared as a pointer) */
static const char no_headroom[] = "it would leave the game too little memory";
static bool switch2;  /* the file's platform: the Switch 2's ("Ounc") GOB layout */

/* the icon being read: its texture's blocks, a block split between two reads, its shrunk sums */
#define ICON_SLOTS (GPU_ICON_COLS * GPU_ICON_ROWS)
#define ICONS_STAGING ((size_t)GPU_ICON_PX * GPU_ICON_PX * ICON_SLOTS * 4) /* the atlas, as gameimage fills it */
#define ICONS_MEM (2 * ICONS_STAGING + 0x20000)
#define ICONS (GAMEIMAGES - GAMEIMAGES_SHARED)
typedef unsigned __int128 icon_bits_t; /* a bit per icon (and per slot) */
#define ICON_BIT(i) ((icon_bits_t)1 << (i))
#define ALL_ICONS (ICON_BIT(ICONS) - 1)
_Static_assert(ICONS <= ICON_SLOTS && ICON_SLOTS < 128, "an icon slot each, a bit each");
_Static_assert(GAMEIMAGES <= GPU_ICONS_IMAGE, "the icons' atlas after the pictures");
static uint8_t *icons_mem;
static int tex_w, tex_h, blocks_w, blocks_h, gobs_w, gob_h;
static uint8_t carry[16];
static int carried;
static uint32_t sums[GPU_ICON_PX * GPU_ICON_PX][4];
static uint16_t counts[GPU_ICON_PX * GPU_ICON_PX];
static uint8_t boxes[ICON_SLOTS][4]; /* each icon's covered part (gameimage_icon_box), in its slot's pixels */
static icon_bits_t icons_todo = ALL_ICONS; /* a bit per icon: not read yet, or to try again */
static icon_bits_t icons_filled, icons_lost; /* ... in its slot; left out (not in this version, or damaged) */
static icon_bits_t icons_short;              /* ... out of memory in this pass: tried again in the next */
#define COVERED_ALPHA 24             /* a pixel at least this opaque is part of the picture */
static int frames;
static uint64_t ticks, decoded;

static uint64_t rd(const uint8_t *p, int bytes)
{
  uint64_t v = 0;
  for (int i = 0; i < bytes; i++)
    v |= (uint64_t)p[i] << (8 * i);
  return v;
}

static size_t round_up(size_t n, size_t to) { return (n + to - 1) / to * to; }

/* n bytes at `at` are in the header (written so a damaged offset can't wrap around) */
static bool in_head(uint64_t at, uint64_t n)
{
  return at <= head_len && n <= head_len - at;
}

/* the length-prefixed name at `at` of the header is `name` */
static bool name_is(uint64_t at, const char *name)
{
  if (!in_head(at, 2))
    return false;
  uint64_t n = rd(head + at, 2);
  if (!in_head(at + 2, n))
    return false;
  for (uint64_t i = 0; i < n; i++)
    if (!name[i] || head[at + 2 + i] != (uint8_t)name[i])
      return false;
  return !name[n];
}

/* the wanted pictures' places from the header's records (and mem_size, the memory for them); false: not
 * the file this knows (sink_why says why) */
static bool parse(void)
{
  sink_why = "its header isn't the one this module knows"; /* not menu text */
  uint32_t platform = head_len >= 0x48 ? (uint32_t)rd(head + 0x20, 4) : 0;
  if (platform != 0x2020584Eu /* "NX  " */ && platform != 0x636E754Fu /* "Ounc": the Switch 2's */)
    return false;
  uint64_t count = rd(head + 0x24, 4), table = rd(head + 0x28, 8);
  if (!in_head(table, 8 * count))
    return false;
  for (uint64_t i = 0; i < count; i++) {
    uint64_t r = rd(head + table + 8 * i, 8);
    if (!in_head(r, 0x78) || rd(head + r, 4) != 0x49545242u /* "BRTI" */)
      return false;
    for (int k = lo; k < hi; k++) {
      if (want[k].gpu.size || !name_is(rd(head + r + 0x60, 8), names[k]))
        continue;
      uint32_t format = (uint32_t)rd(head + r + 0x1c, 4);
      uint64_t levels = rd(head + r + 0x70, 8);
      int width = (int32_t)rd(head + r + 0x24, 4), height = (int32_t)rd(head + r + 0x28, 4);
      size_t size = (size_t)rd(head + r + 0x50, 4);
      int nvn = format == BNTX_BC4_UNORM ? NVN_FORMAT_BC4
                : format == BNTX_ASTC_4X4_UNORM || format == BNTX_ASTC_4X4_SRGB ? NVN_FORMAT_ASTC_4X4 : 0;
      if (!nvn || !in_head(levels, 8) || width <= 0 || height <= 0 || !size || size > IMAGE_MAX)
        return false;
      want[k].at = rd(head + levels, 8);
      if (want[k].at < head_need || want[k].at > UINT64_MAX - size)
        return false;
      want[k].gpu = (gpu_image_t){ 0, size, width, height, nvn, { 0 }, { 0 } };
      __builtin_memcpy(want[k].gpu.layout, head + r + 0x34, 8);
      for (int c = 0; c < 4; c++) {
        uint8_t source = head[r + 0x58 + c];
        want[k].gpu.swizzle[c] = source <= 5 ? source : (uint8_t)(c + 2);
      }
      if (k == GAMEIMAGE_GAUGE_FILL) { /* the magic meter's green as the health's red */
        uint8_t red = want[k].gpu.swizzle[0];
        want[k].gpu.swizzle[0] = want[k].gpu.swizzle[1];
        want[k].gpu.swizzle[1] = red;
      }
    }
  }
  if (task == DECODE_ICON) { /* an icon: decoded as it comes (icon_bytes) */
    const gpu_image_t *im = &want[icon].gpu;
    if (!im->size || im->format != NVN_FORMAT_ASTC_4X4) {
      sink_why = im->size ? "not ASTC 4x4" : "no picture of that name in it"; /* not menu text */
      return false;
    }
    tex_w = im->width, tex_h = im->height;
    blocks_w = (tex_w + 3) / 4, blocks_h = (tex_h + 3) / 4;
    gobs_w = (blocks_w * 16 + 63) / 64;
    gob_h = 1 << (im->layout[0] & 7);
    switch2 = platform == 0x636E754Fu;
    carried = 0;
    __builtin_memset(sums, 0, sizeof(sums));
    __builtin_memset(counts, 0, sizeof(counts));
    sink_why = 0;
    return true;
  }
  if (!want[GAMEIMAGE_PRO].gpu.size || !want[GAMEIMAGE_GRIP].gpu.size || !want[GAMEIMAGE_CONSOLE].gpu.size) {
    sink_why = "the controllers' pictures aren't in it"; /* not menu text */
    return false;
  }
  for (int k = lo; k < hi; k++)
    if (!want[k].gpu.size)
      log_printf("game pictures: no %s in this version; left out", names[k]);
  size_t at = 0;
  for (int k = lo; k < hi; k++) {
    want[k].gpu.offset = at;
    at = round_up(at + want[k].gpu.size, IMAGE_ALIGN);
  }
  mem_size = round_up(at, PAGE);
  sink_why = 0;
  return true;
}

/* every picture of the file found is here (one not found isn't waited for) */
static bool all_here(void)
{
  for (int k = lo; k < hi; k++)
    if (want[k].got < want[k].gpu.size)
      return false;
  return parsed && (task == DECODE_ICON || want[GAMEIMAGE_PRO].gpu.size);
}

/* one of the icon's blocks, `off` bytes into its texture: in its place, its texels added to the sums of the
 * shrunk icon's pixels they fall in */
static void icon_block(const uint8_t *block, uint64_t off)
{
  uint64_t per = 512ull * (uint64_t)gob_h, column = off / per % (uint64_t)gobs_w, row = off / per / (uint64_t)gobs_w;
  uint64_t gob = off % per / 512, k = off % 512 / 16; /* the GOB in the column, the 16-byte sector in it */
  int x16 = switch2 ? (int)((k >> 2 & 1) | (k >> 4 & 1) << 1) : (int)((k >> 1 & 1) | (k >> 4 & 1) << 1);
  int y = switch2 ? (int)((k & 3) | (k >> 3 & 1) << 2) : (int)((k & 1) | (k >> 2 & 3) << 1);
  int bx = (int)column * 4 + x16, by = (int)(row * (uint64_t)gob_h + gob) * 8 + y;
  if (bx >= blocks_w || by >= blocks_h)
    return; /* padding */
  uint8_t texels[16][4];
  astc_block(block, texels);
  for (int i = 0; i < 16; i++) {
    int x = bx * 4 + i % 4, yy = by * 4 + i / 4;
    if (x >= tex_w || yy >= tex_h)
      continue;
    int at = yy * GPU_ICON_PX / tex_h * GPU_ICON_PX + x * GPU_ICON_PX / tex_w;
    uint32_t a = texels[i][3];
    sums[at][0] += texels[i][0] * a, sums[at][1] += texels[i][1] * a, sums[at][2] += texels[i][2] * a;
    sums[at][3] += a;
    counts[at]++;
  }
}

/* n bytes of the icon's texture from `off`, in order */
static void icon_bytes(const uint8_t *p, uint64_t off, size_t n)
{
  while (n) {
    if (carried || n < 16) { /* a block split between reads */
      size_t take = 16 - (size_t)carried < n ? 16 - (size_t)carried : n;
      __builtin_memcpy(carry + carried, p, take);
      carried += (int)take, p += take, off += take, n -= take;
      if (carried == 16) {
        icon_block(carry, off - 16);
        carried = 0;
      }
      continue;
    }
    icon_block(p, off);
    p += 16, off += 16, n -= 16;
  }
}

/* the shrunk icon into its slot of the atlas (its colour the average of its texels', weighted by their
 * alpha), and the part of it the picture covers */
static void icon_finish(int slot)
{
  int ax = slot % GPU_ICON_COLS * GPU_ICON_PX, ay = slot / GPU_ICON_COLS * GPU_ICON_PX;
  int x0 = GPU_ICON_PX, y0 = GPU_ICON_PX, x1 = 0, y1 = 0;
  for (int i = 0; i < GPU_ICON_PX * GPU_ICON_PX; i++) {
    uint8_t *px = icons_mem + ((size_t)(ay + i / GPU_ICON_PX) * GPU_ICON_PX * GPU_ICON_COLS + ax + i % GPU_ICON_PX) * 4;
    uint32_t a = sums[i][3];
    for (int c = 0; c < 3; c++)
      px[c] = a ? (uint8_t)(sums[i][c] / a) : 0;
    px[3] = counts[i] ? (uint8_t)(a / counts[i]) : 0;
    if (px[3] >= COVERED_ALPHA) {
      int x = i % GPU_ICON_PX, y = i / GPU_ICON_PX;
      x0 = x < x0 ? x : x0, y0 = y < y0 ? y : y0;
      x1 = x + 1 > x1 ? x + 1 : x1, y1 = y + 1 > y1 ? y + 1 : y1;
    }
  }
  if (x1 <= x0 || y1 <= y0) /* nothing covered: all of it */
    x0 = 0, y0 = 0, x1 = GPU_ICON_PX, y1 = GPU_ICON_PX;
  boxes[slot][0] = (uint8_t)x0, boxes[slot][1] = (uint8_t)y0, boxes[slot][2] = (uint8_t)x1, boxes[slot][3] = (uint8_t)y1;
}

void gameimage_icon_box(int slot, float box[4])
{
  for (int i = 0; i < 4; i++)
    box[i] = slot >= 0 && slot < ICON_SLOTS ? (float)boxes[slot][i] / GPU_ICON_PX : (float)(i >= 2);
}

/* the shared pictures' memory, mem_size bytes, zeroed; false: out of memory */
static bool take_mem(void)
{
  if (mem && mem_taken == mem_size)
    return true;
  free(mem);
  mem_taken = 0;
  if (!(mem = aligned_alloc(PAGE, mem_size)))
    return false;
  __builtin_memset(mem, 0, mem_size);
  mem_taken = mem_size;
  return true;
}

/* with everything taken: tried again (the game may be in the middle of a scene), it leaves the game
 * RETRY_HEADROOM more */
static bool headroom(void)
{
  if (!shared.tries)
    return true;
  void *room = aligned_alloc(PAGE, RETRY_HEADROOM);
  free(room);
  return room != 0;
}

/* the file's bytes [at, at + n), in order: the header kept, then the pictures' data copied (the shared
 * archive's) or decoded (an icon's) */
static bool sink(void *user, uint64_t at, const uint8_t *data, size_t n)
{
  (void)user;
  decoded = at + n;
  if (!parsed) {
    if (!head) { /* the first block: the header's size is in it */
      head_need = at == 0 && n >= 0x38 ? rd(data + 0x30, 8) : 0;
      if (head_need < 0x48 || head_need > HEADER_MAX) {
        sink_why = "its header isn't the one this module knows"; /* not menu text */
        return false;
      }
      if (!(head = aligned_alloc(16, (size_t)head_need))) {
        sink_why = zstd_no_memory;
        return false;
      }
    }
    if (at < head_need) {
      size_t k = head_need - at < n ? (size_t)(head_need - at) : n;
      __builtin_memcpy(head + at, data, k);
      head_len = at + k;
    }
    if (head_len < head_need)
      return true;
    parsed = parse();
    free(head); /* the records are read: the pictures' memory instead */
    head = 0;
    if (!parsed)
      return false;
    if (task == DECODE_SHARED) {
      bool had = mem && mem_taken == mem_size; /* taken with the stream (a try again), and checked then */
      if (!take_mem()) {
        sink_why = zstd_no_memory;
        return false;
      }
      if (!had && !headroom()) {
        sink_why = no_headroom;
        return false;
      }
    }
  }
  for (int k = lo; k < hi; k++) {
    uint64_t from = at > want[k].at ? at : want[k].at;
    uint64_t to = at + n < want[k].at + want[k].gpu.size ? at + n : want[k].at + want[k].gpu.size;
    if (from < to) {
      if (task == DECODE_ICON)
        icon_bytes(data + (from - at), from - want[k].at, (size_t)(to - from));
      else
        __builtin_memcpy(mem + want[k].gpu.offset + (from - want[k].at), data + (from - at), (size_t)(to - from));
      want[k].got += to - from;
    }
  }
  return !all_here();
}

static size_t read_file(void *user, uint64_t at, void *buf, size_t n)
{
  (void)user;
  size_t got = 0;
  return fs_read(&got, file, (int64_t)at, buf, n) == 0 ? got : 0;
}

static void close_all(void)
{
  zstd_stream_free(stream);
  stream = 0;
  if (file_open)
    fs_close(file);
  file_open = false;
  free(head);
  head = 0;
  head_len = head_need = 0;
  parsed = false;
}

/* parts[0] + parts[1] + ... into buf */
static void join(char *buf, size_t len, const char *const *parts, int n)
{
  size_t at = 0;
  for (int p = 0; p < n; p++)
    for (const char *c = parts[p]; *c && at + 1 < len; c++)
      buf[at++] = *c;
  buf[at] = 0;
}

/* an icon's file: content:/UI/Tex/Icon/<name>.bntx.zs */
static void icon_path(int k, char *buf, size_t len)
{
  const char *parts[3] = { ICON_DIR, names[k], ".bntx.zs" };
  join(buf, len, parts, 3);
}

/* a frame's time is up */
static bool time_up(uint64_t start)
{
  return sys_tick() - start >= sys_tick_freq() * STEP_MS / 1000;
}

/* --- trying again: after running out of memory, a little later, then less and less often --------- */

static const uint16_t retry_seconds[] = { 10, 30, 60, 120, 300 };
#define RETRIES (sizeof(retry_seconds) / sizeof(*retry_seconds))

/* why (zstd_no_memory or no_headroom): later, or lost after RETRY_MAX tries; false: lost */
static bool try_later(part_t *part, const char *what, const char *why)
{
  if (++part->tries > RETRY_MAX) {
    log_printf("game pictures: %s: %s; given up after %d tries", what, why, RETRY_MAX);
    part->state = PART_LOST;
    return false;
  }
  unsigned secs = retry_seconds[part->tries <= RETRIES ? part->tries - 1 : RETRIES - 1];
  part->at = sys_tick() + sys_tick_freq() * secs;
  part->state = PART_LATER;
  log_printf("game pictures: %s: %s; trying again in %u s", what, why, secs);
  return true;
}

static bool retryable(const char *why)
{
  return why == zstd_no_memory || why == no_headroom;
}

static bool due(const part_t *part)
{
  return part->state == PART_WAITING || (part->state == PART_LATER && sys_tick() >= part->at);
}

/* --- the cache: the pictures as they end up, on the SD card -----------------------------------------
 * One file each for the shared pictures (4.6 MB, as they're drawn) and for the icons' atlas (1.2 MB),
 * named after the game's version: a header, then the data. The header says what the file was made from
 * (key: this module's version and layout, the pictures' names, and the archive's size or the icon files'
 * sizes) and holds a checksum of it all; it's written last, so a file cut short is never taken. A file
 * that isn't right is deleted and made afresh, and not read again this session.
 * The SD card's data goes through `bounce`: memory the file system uses for a call is locked meanwhile, and
 * the pictures' memory may be made into the GPU's at any moment (gpuui.c, on the render thread). */
#define CACHE_DIR SD_DIR "/cache"
#define CACHE_MAGIC 0x31434950u /* "PIC1" */
#define CACHE_FORMAT 1          /* one more whenever the layout or what's in it changes */
#define CACHE_MAX (64u << 20)
#define CACHE_STEP (64u << 10) /* read or written between looks at the clock */
#define HASH_START 0xcbf29ce484222325ull
#define HASH_PRIME 0x100000001b3ull

typedef struct {
  uint32_t magic, format;
  uint64_t key;
  uint64_t size; /* the data's, after the header */
  uint64_t sum;  /* of the header (its magic and sum 0) and the data */
  gpu_image_t images[GAMEIMAGES_SHARED]; /* the shared pictures' places in the data */
  uint8_t boxes[ICON_SLOTS][4];          /* the icons' covered parts */
  icon_bits_t filled;                    /* the icons' slots that have one */
} cache_head_t;

enum { CACHE_SHARED, CACHE_ICONS };
static const char *const cache_kinds[2] = { "pictures", "icons" };
static struct {
  char path[96];
  fs_file_t file;
  bool open;
  cache_head_t head;
  uint8_t *data;
  size_t done;
  uint64_t sum;
} cache;
static bool cache_off[2];  /* a file that wasn't right was dropped: not read again this session */
static uint64_t keys[2];   /* what each kind is made from */
static int icon_sizes_at = GAMEIMAGES_SHARED; /* the icons' key: the icon files' sizes hashed up to here */
static uint8_t bounce[CACHE_STEP] __attribute__((aligned(64)));

/* FNV-1a, 8 bytes at a time, each step's high bits folded back so a change anywhere reaches all of them */
static uint64_t hash(uint64_t h, const void *data, size_t n)
{
  const uint8_t *p = data;
  for (; n >= 8; p += 8, n -= 8) {
    uint64_t w;
    __builtin_memcpy(&w, p, 8);
    h = (h ^ w) * HASH_PRIME;
    h ^= h >> 32;
  }
  for (; n; p++, n--)
    h = (h ^ *p) * HASH_PRIME;
  return h;
}

/* the start of a key: this module's version and how the pictures are kept, and the names [from, to) */
static uint64_t key_start(int from, int to)
{
  const uint32_t layout[] = { CACHE_FORMAT, (uint32_t)sizeof(cache_head_t), (uint32_t)sizeof(gpu_image_t), IMAGE_ALIGN,
                              GPU_ICON_PX, GPU_ICON_COLS, GPU_ICON_ROWS, COVERED_ALPHA };
  uint64_t h = hash(HASH_START, layout, sizeof(layout));
  h = hash(h, PACKAGE_VERSION, sizeof(PACKAGE_VERSION));
  for (int k = from; k < to; k++)
    h = hash(h, names[k], strlen(names[k]) + 1);
  return h;
}

/* the cache can be used: the SD card is there, and this kind's file hasn't been dropped */
static bool cache_usable(int kind)
{
  return settings_sd() && !cache_off[kind];
}

static uint64_t head_sum(const cache_head_t *head_in)
{
  cache_head_t h;
  __builtin_memcpy(&h, head_in, sizeof(h));
  h.magic = 0;
  h.sum = 0;
  return hash(HASH_START, &h, sizeof(h));
}

static void cache_path(int kind)
{
  const char *parts[5] = { CACHE_DIR "/", cache_kinds[kind], "-", z64hd_game ? z64hd_game->version : "game", ".bin" };
  join(cache.path, sizeof(cache.path), parts, 5);
}

static void cache_close(void)
{
  if (cache.open)
    fs_close(cache.file);
  cache.open = false;
}

/* the file isn't right: deleted, to be made afresh, and not read again this session */
static void cache_drop(int kind, const char *why)
{
  cache_close();
  log_printf("game pictures: %s: %s; made afresh", cache.path, why);
  fs_delete_file(cache.path);
  cache_off[kind] = true;
}

/* opens a cache file and reads its header: true if it's this game's, made from these files */
static bool cache_open(int kind)
{
  if (!cache_usable(kind))
    return false;
  cache_path(kind);
  if (fs_open(&cache.file, cache.path, FS_READ) != 0)
    return false; /* not made yet */
  cache.open = true;
  int64_t size = 0;
  size_t got = 0;
  __builtin_memset(&cache.head, 0, sizeof(cache.head));
  if (fs_size(&size, cache.file) != 0 || fs_read(&got, cache.file, 0, &cache.head, sizeof(cache.head)) != 0 ||
      got != sizeof(cache.head) || cache.head.magic != CACHE_MAGIC || cache.head.format != CACHE_FORMAT ||
      cache.head.key != keys[kind] || cache.head.size > CACHE_MAX ||
      (uint64_t)size != sizeof(cache.head) + cache.head.size) {
    cache_drop(kind, "it's from another version, or it was cut short");
    return false;
  }
  cache.done = 0;
  cache.sum = head_sum(&cache.head);
  return true;
}

/* reads more of the data into cache.data: 1 more to come, 0 all of it (and right), -1 not right (dropped) */
static int cache_read(int kind, uint64_t start)
{
  while (cache.done < cache.head.size && !time_up(start)) {
    size_t n = cache.head.size - cache.done < CACHE_STEP ? (size_t)(cache.head.size - cache.done) : CACHE_STEP, got = 0;
    if (fs_read(&got, cache.file, (int64_t)(sizeof(cache.head) + cache.done), bounce, n) != 0 || got != n) {
      cache_drop(kind, "it can't be read");
      return -1;
    }
    __builtin_memcpy(cache.data + cache.done, bounce, n);
    cache.sum = hash(cache.sum, bounce, n);
    cache.done += n;
  }
  if (cache.done < cache.head.size)
    return 1;
  cache_close();
  if (cache.sum != cache.head.sum) {
    cache_drop(kind, "it's damaged");
    return -1;
  }
  return 0;
}

/* starts writing a cache file of `size` bytes of `data`, cache.head's pictures filled in: false if it can't.
 * The file grows as it's written (made at its full size, a console's SD card may clear all of it at once). */
static bool cache_start(int kind, uint8_t *data, size_t size)
{
  if (!settings_sd())
    return false;
  cache_path(kind);
  cache.head.magic = 0;
  cache.head.format = CACHE_FORMAT;
  cache.head.key = keys[kind];
  cache.head.size = size;
  cache.head.sum = 0;
  static const fs_write_option_t no_flush = { 0 };
  fs_create_dir(SD_DIR); /* these fail harmlessly if they're there */
  fs_create_dir(CACHE_DIR);
  fs_delete_file(cache.path);
  if (fs_create_file(cache.path, 0) != 0 || fs_open(&cache.file, cache.path, FS_WRITE | FS_ALLOW_APPEND) != 0) {
    log_printf("game pictures: couldn't write %s", cache.path);
    return false;
  }
  cache.open = true;
  if (fs_write(cache.file, 0, &cache.head, sizeof(cache.head), &no_flush) != 0) { /* no magic yet */
    cache_close();
    fs_delete_file(cache.path);
    log_printf("game pictures: couldn't write %s", cache.path);
    return false;
  }
  cache.data = data;
  cache.done = 0;
  cache.sum = head_sum(&cache.head);
  return true;
}

/* writes more of it, the header last: 1 more to come, 0 done, -1 it can't be written (deleted) */
static int cache_write(uint64_t start)
{
  static const fs_write_option_t no_flush = { 0 }, flush = { FS_WRITE_FLUSH };
  while (cache.done < cache.head.size && !time_up(start)) {
    size_t n = cache.head.size - cache.done < CACHE_STEP ? (size_t)(cache.head.size - cache.done) : CACHE_STEP;
    __builtin_memcpy(bounce, cache.data + cache.done, n);
    if (fs_write(cache.file, (int64_t)(sizeof(cache.head) + cache.done), bounce, n, &no_flush) != 0)
      goto failed;
    cache.sum = hash(cache.sum, bounce, n);
    cache.done += n;
  }
  if (cache.done < cache.head.size)
    return 1;
  cache.head.sum = cache.sum;
  cache.head.magic = CACHE_MAGIC;
  if (fs_write(cache.file, 0, &cache.head, sizeof(cache.head), &flush) != 0)
    goto failed;
  cache_close();
  log_printf("game pictures: kept in %s for the next start", cache.path);
  return 0;
failed:
  cache_close();
  fs_delete_file(cache.path);
  log_printf("game pictures: couldn't write %s", cache.path);
  return -1;
}

/* --- the shared archive's pictures ------------------------------------------------------------------- */

static void publish_shared(void)
{
  gpu_image_t images[GAMEIMAGES_SHARED];
  for (int k = 0; k < GAMEIMAGES_SHARED; k++)
    images[k] = want[k].gpu;
  gpuui_images_publish(0, 0, mem, mem_size, images, GAMEIMAGES_SHARED);
  shared.state = PART_DONE;
}

/* the cache's pictures fit in its data, and the controllers are there */
static bool cached_shared_ok(void)
{
  const cache_head_t *h = &cache.head;
  if (!h->size || h->size % PAGE)
    return false;
  for (int k = 0; k < GAMEIMAGES_SHARED; k++) {
    const gpu_image_t *im = &h->images[k];
    if (!im->size)
      continue;
    if (im->offset % IMAGE_ALIGN || im->size > IMAGE_MAX || im->size > h->size || im->offset > h->size - im->size ||
        im->width <= 0 || im->height <= 0 || (im->format != NVN_FORMAT_ASTC_4X4 && im->format != NVN_FORMAT_BC4))
      return false;
  }
  return h->images[GAMEIMAGE_PRO].size && h->images[GAMEIMAGE_GRIP].size && h->images[GAMEIMAGE_CONSOLE].size;
}

#define SHARED_WHAT "the menus' and controllers' pictures"

static void shared_failed(const char *why)
{
  close_all();
  free(mem);
  mem = 0;
  mem_taken = 0;
  if (retryable(why)) {
    if (why == zstd_no_memory && decoded)
      log_printf("game pictures: %s: out of memory after %u KiB", ARCHIVE, (unsigned)(decoded >> 10));
    if (!try_later(&shared, SHARED_WHAT, why))
      log_printf("game pictures: the input display keeps its glyphs");
    return;
  }
  log_printf("game pictures: %s: %s (after %u KiB); the input display keeps its glyphs", ARCHIVE,
             why ? why : "error", (unsigned)(decoded >> 10));
  shared.state = PART_LOST;
}

static void start_shared(void)
{
  lo = 0, hi = GAMEIMAGES_SHARED;
  __builtin_memset(want, 0, sizeof(*want) * GAMEIMAGES_SHARED);
  decoded = 0, ticks = 0, frames = 0;
  sink_why = 0;
  if (fs_open(&file, ARCHIVE, FS_READ) != 0) {
    shared_failed("can't open it"); /* not menu text */
    return;
  }
  file_open = true;
  int64_t size = 0;
  fs_size(&size, file);
  keys[CACHE_SHARED] = hash(key_start(0, GAMEIMAGES_SHARED), &size, sizeof(size));
  if (cache_open(CACHE_SHARED)) {
    if (!cached_shared_ok()) {
      cache_drop(CACHE_SHARED, "its pictures aren't right");
    } else {
      close_all(); /* the archive isn't needed */
      mem_size = (size_t)cache.head.size;
      if (!take_mem()) {
        cache_close();
        shared_failed(zstd_no_memory);
        return;
      }
      cache.data = mem;
      task = READ_SHARED;
      return;
    }
  }
  /* tried again with its size known: the pictures' memory first, all of it at once with the stream */
  if (shared.tries && mem_size && !take_mem()) {
    shared_failed(zstd_no_memory);
    return;
  }
  const char *why = 0;
  stream = zstd_stream_new(read_file, 0, &why);
  if (!stream) {
    shared_failed(why);
    return;
  }
  if (mem && !headroom()) {
    shared_failed(no_headroom);
    return;
  }
  task = DECODE_SHARED;
}

/* --- the icons -------------------------------------------------------------------------------------- */

#define ICONS_WHAT "the items' icons"

/* the icons' atlas, handed to gpuui.c; false: out of memory */
static bool icons_atlas(void)
{
  if (icons_mem)
    return true;
  if (!(icons_mem = aligned_alloc(PAGE, round_up(ICONS_MEM, PAGE))))
    return false;
  __builtin_memset(icons_mem, 0, round_up(ICONS_MEM, PAGE));
  gpuui_icons_publish(icons_mem, round_up(ICONS_MEM, PAGE));
  return true;
}

/* the icons' key, a few files a frame: the sizes of their files; false: not all of them yet */
static bool icons_key(uint64_t start)
{
  if (icon_sizes_at == GAMEIMAGES_SHARED)
    keys[CACHE_ICONS] = key_start(GAMEIMAGES_SHARED, GAMEIMAGES);
  char path[128];
  for (; icon_sizes_at < GAMEIMAGES && !time_up(start); icon_sizes_at++) {
    int64_t size = -1;
    fs_file_t f;
    icon_path(icon_sizes_at, path, sizeof(path));
    if (fs_open(&f, path, FS_READ) == 0) {
      fs_size(&size, f);
      fs_close(f);
    }
    keys[CACHE_ICONS] = hash(keys[CACHE_ICONS], &size, sizeof(size));
  }
  return icon_sizes_at == GAMEIMAGES;
}

/* opens icon k's file with a stream: 1 open, 0 left out (not in this version, or not readable), -1 out of
 * memory */
static int open_icon(int k)
{
  char path[128];
  icon_path(k, path, sizeof(path));
  if (fs_open(&file, path, FS_READ) != 0) {
    log_printf("game pictures: no icon %s in this version; left out", names[k]);
    return 0;
  }
  file_open = true;
  const char *why = 0;
  stream = zstd_stream_new(read_file, 0, &why);
  if (stream) {
    lo = k, hi = k + 1;
    __builtin_memset(&want[k], 0, sizeof(want[k]));
    sink_why = 0;
    return 1;
  }
  close_all();
  if (why == zstd_no_memory)
    return -1;
  log_printf("game pictures: %s: %s; left out", path, why ? why : "can't read it");
  return 0;
}

static void icons_done(void)
{
  icons.state = PART_DONE;
  log_printf("game pictures: %d icons in %u ms over %d frames", __builtin_popcountll((uint64_t)icons_filled) +
             __builtin_popcountll((uint64_t)(icons_filled >> 64)),
             (unsigned)(ticks * 1000 / sys_tick_freq()), frames);
}

/* the icons not done yet after a pass: tried again later, or given up (then left out) */
static void icons_later(void)
{
  if (!try_later(&icons, ICONS_WHAT, zstd_no_memory)) {
    icons_lost |= icons_todo;
    icons_todo = 0;
  }
}

/* the next icon still to read from k on, open to read; at the end of the pass, the icons are done (and kept
 * in the cache), or the ones that ran out of memory are tried again later */
static void next_icon(int k)
{
  task = NOTHING;
  for (; k < GAMEIMAGES; k++) {
    icon_bits_t bit = ICON_BIT(k - GAMEIMAGES_SHARED);
    if (!(icons_todo & bit) || (icons_short & bit))
      continue;
    int open = open_icon(k);
    if (open > 0) {
      icon = k;
      task = DECODE_ICON;
      return;
    }
    if (open < 0) { /* this one later, the others now */
      icons_short |= bit;
      continue;
    }
    icons_todo &= ~bit;
    icons_lost |= bit;
  }
  icons_short = 0;
  if (icons_todo) {
    icons_later();
    return;
  }
  icons_done();
  __builtin_memset(&cache.head, 0, sizeof(cache.head));
  __builtin_memcpy(cache.head.boxes, boxes, sizeof(boxes));
  cache.head.filled = icons_filled;
  if (icons_filled && cache_start(CACHE_ICONS, icons_mem, ICONS_STAGING))
    task = SAVE;
}

static void start_icons(uint64_t start)
{
  if (!icons_atlas()) {
    icons_later();
    return;
  }
  if (icons_todo == ALL_ICONS && cache_usable(CACHE_ICONS)) { /* none read yet: all of them from the cache */
    if (!icons_key(start))
      return; /* the rest of the key next frame */
    if (cache_open(CACHE_ICONS)) {
      if (cache.head.size != ICONS_STAGING || cache.head.filled & ~ALL_ICONS) {
        cache_drop(CACHE_ICONS, "its icons aren't right");
      } else {
        ticks = 0, frames = 0;
        cache.data = icons_mem;
        task = READ_ICONS;
        return;
      }
    }
  }
  ticks = 0, frames = 0, decoded = 0;
  next_icon(GAMEIMAGES_SHARED);
}

/* an icon's file read: into its slot, handed over at once (or left out, or tried again later) */
static void icon_read(int more, const char *why)
{
  icon_bits_t bit = ICON_BIT(icon - GAMEIMAGES_SHARED);
  bool no_memory = (more < 0 && why == zstd_no_memory) || sink_why == zstd_no_memory;
  if (no_memory) {
    icons_short |= bit;
  } else if (more < 0 || !all_here() || !want[icon].gpu.size) {
    log_printf("game pictures: icon %s: %s; left out", names[icon],
               more < 0 && why ? why : sink_why ? sink_why : "no picture of that name in it");
    icons_lost |= bit;
    icons_todo &= ~bit;
  } else {
    icon_finish(icon - GAMEIMAGES_SHARED);
    gpuui_icon_filled(icon - GAMEIMAGES_SHARED);
    icons_filled |= bit;
    icons_todo &= ~bit;
  }
  close_all();
  next_icon(icon + 1);
}

/* --- each frame -------------------------------------------------------------------------------------- */

bool gameimage_lost(int picture)
{
  if (picture < 0 || picture >= GAMEIMAGES)
    return true;
  if (picture < GAMEIMAGES_SHARED)
    return shared.state == PART_LOST || (shared.state == PART_DONE && !want[picture].gpu.size);
  return icons_lost >> (picture - GAMEIMAGES_SHARED) & 1;
}

void gameimage_frame(void)
{
  if (!gpuui_ready())
    return;
  uint64_t start = sys_tick();
  if (task == NOTHING) {
    if (gfx_style != GFX_STYLE_GAME)
      return; /* nothing new in gz's style (what's under way is finished, so its memory is given back) */
    if (due(&shared))
      start_shared(); /* first: the controllers and the menus' parts */
    else if (due(&icons))
      start_icons(start);
    if (task == NOTHING)
      return;
  }
  const char *why = 0;
  int more = 1;
  switch (task) {
    case READ_SHARED:
      more = cache_read(CACHE_SHARED, start);
      break;
    case READ_ICONS:
      more = cache_read(CACHE_ICONS, start);
      break;
    case SAVE:
      more = cache_write(start);
      break;
    default:
      while (more == 1 && !time_up(start))
        more = zstd_stream_step(stream, task == DECODE_ICON ? ICON_STEP_BYTES : STEP_BYTES, sink, 0, &why);
      break;
  }
  ticks += sys_tick() - start;
  frames++;
  if (more == 1)
    return;
  switch (task) {
    case READ_SHARED:
      task = NOTHING;
      if (more < 0) { /* decoded instead, next frame */
        free(mem);
        mem = 0;
        mem_taken = 0;
        return;
      }
      for (int k = 0; k < GAMEIMAGES_SHARED; k++)
        want[k].gpu = cache.head.images[k];
      publish_shared();
      log_printf("game pictures: read from %s in %u ms over %d frames", cache.path,
                 (unsigned)(ticks * 1000 / sys_tick_freq()), frames);
      return;
    case DECODE_SHARED:
      task = NOTHING;
      if (more < 0 || !all_here()) {
        shared_failed(more < 0 ? why : sink_why ? sink_why : "it ended before the pictures"); /* not menu text */
        return;
      }
      close_all();
      publish_shared();
      log_printf("game pictures: loaded in %u ms over %d frames", (unsigned)(ticks * 1000 / sys_tick_freq()), frames);
      __builtin_memset(&cache.head, 0, sizeof(cache.head));
      for (int k = 0; k < GAMEIMAGES_SHARED; k++)
        cache.head.images[k] = want[k].gpu;
      if (cache_start(CACHE_SHARED, mem, mem_size))
        task = SAVE;
      return;
    case READ_ICONS:
      task = NOTHING;
      if (more < 0) { /* decoded instead, next frame: no slot is filled, and the atlas starts clean */
        __builtin_memset(icons_mem, 0, ICONS_STAGING);
        return;
      }
      __builtin_memcpy(boxes, cache.head.boxes, sizeof(boxes));
      icons_filled = cache.head.filled;
      icons_lost = ALL_ICONS & ~icons_filled;
      icons_todo = 0;
      for (int slot = 0; slot < ICONS; slot++)
        if (icons_filled >> slot & 1)
          gpuui_icon_filled(slot);
      icons_done();
      return;
    case DECODE_ICON:
      icon_read(more, why);
      return;
    default: /* SAVE: done, or it couldn't be (nothing lost but the next start's time) */
      task = NOTHING;
      return;
  }
}
