/* The respawn marker: where the save file has Link (Player_Pos_For_Map, the lead for where a loaded save puts
 * him: FINDINGS §8), marked in the world: a ring on that spot, a post up from it, and a line to it from where
 * Link is now. Projected with the game's camera on the game thread and handed to the render thread as lines
 * (overlayui.c draws them on the GPU, in the game's style; gz's style has no marker yet). */
#ifndef SPAWNMARK_H
#define SPAWNMARK_H

#include <stdbool.h>

extern bool spawn_marker_shown; /* scene > respawn marker; off at each start of the game */

/* a piece of the marker on the picture: its ends 0..1 across and down the visible picture */
typedef struct { float x0, y0, x1, y1; } sm_line_t;

/* game thread, once a frame */
void spawnmark_frame(void);
/* render thread: takes the newest frame's lines (publish.h), then they can be read, then releases them */
void spawnmark_take(void);
const sm_line_t *spawnmark_lines(int *n);
void spawnmark_release(void);

#endif
