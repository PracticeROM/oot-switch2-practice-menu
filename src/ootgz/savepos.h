/* Where a load puts Link (FINDINGS §9.7): AutoSaveMgr's two slots, which the game copies to
 * PlayerStatus.SavePos every frame, drawn as markers in the world (with the air walls' lines, airwall.c). */
#ifndef SAVEPOS_H
#define SAVEPOS_H

#include <stdbool.h>
#include <stdint.h>

extern bool savepos_shown; /* draw the slots */
extern bool savepos_load;  /* and where a load would put Link (the game's search round SavePos) */

enum { SAVEPOS_A, SAVEPOS_B, SAVEPOS_SLOTS };

typedef struct {
  float yaw;    /* radians, 0 = +z (as atan2(x, z)) */
  float pos[3]; /* Link's position (his feet) */
  bool valid;
} savepos_slot_t;

/* game thread: the slots now; false where AutoSaveMgr can't be found (1.0.0, the title screen, loading) */
bool savepos_slots(savepos_slot_t slots[SAVEPOS_SLOTS]);

/* game thread: AutoSaveMgr's name for what Link is doing ("Move", "Wait", "EvtTalk", ...), FINDINGS §9.3;
 * false (and "") where it can't be read */
bool savepos_link_state(char *buf, int len);

/* game thread: the markers as world lines, (from, to) and a colour (airwall.h AW_SLOT_*); how many */
#define SAVEPOS_MAX_LINES 64
int savepos_lines(float ends[][2][3], uint8_t colours[], int max);

/* the menu's line for a slot: "a  -6.78  121.32  -17.84  yaw 90  *" (*: the one SavePos takes) or "a  not set" */
void savepos_text(int slot, char *buf, int len);
/* the menu's line for the load: "load  12.95  124.87  -34.31  moved 5.0" (or "stays", "not searched") */
void savepos_load_text(char *buf, int len);

#endif
