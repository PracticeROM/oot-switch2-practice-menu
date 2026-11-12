/* The respawn marker (spawnmark.h). */
#include "spawnmark.h"

#include "gamedata.h"
#include "link.h"
#include "publish.h"
#include "sys.h"
#include "view.h"

float sinf(float x); /* from sdk */
float cosf(float x);

bool spawn_marker_shown;

#define RING_RADIUS 0.5f  /* metres: about Link's reach */
#define RING_SEGMENTS 24
#define POST_HEIGHT 3.0f  /* metres: the post up from the spot, so it's seen over low scenery */
#define LINK_MIDDLE 0.9f  /* metres over Link's feet: where the line to the spot starts */
#define TWO_PI 6.2831853f
#define MAX_MARK_LINES (RING_SEGMENTS + 2)

static sm_line_t lines[PUBLISH_BUFFERS][MAX_MARK_LINES];
static int n_lines[PUBLISH_BUFFERS];
static publish_t published = PUBLISH_INIT;
static int taken = -1;

/* The line a-b (world points) onto the picture, cut at the camera's near plane; left out if it's all behind
 * the camera. Its ends may be off the picture: the GPU's drawing is cut to it. */
static void add_line(sm_line_t *out, int *n, const view_cam_t *cam, const float a[3], const float b[3])
{
  float va[3], vb[3];
  view_point(cam, a, va);
  view_point(cam, b, vb);
  float near = cam->near;
  if ((va[2] < near && vb[2] < near) || *n >= MAX_MARK_LINES)
    return;
  if (va[2] < near || vb[2] < near) { /* cut where it crosses the near plane */
    float t = (near - va[2]) / (vb[2] - va[2]);
    float *behind = va[2] < near ? va : vb;
    float cut[3] = { va[0] + t * (vb[0] - va[0]), va[1] + t * (vb[1] - va[1]), near };
    memcpy(behind, cut, sizeof(cut));
  }
  sm_line_t *line = &out[(*n)++];
  view_screen(cam, va, &line->x0, &line->y0);
  view_screen(cam, vb, &line->x1, &line->y1);
}

void spawnmark_frame(void)
{
  int f = publish_next(&published), n = 0;
  const float *saved = spawn_marker_shown ? gd_vec3(GD_VEC3_PLAYER_POS_FOR_MAP) : 0;
  vec3f_t link;
  view_cam_t cam;
  /* (not over the game's pause screen or its item menus, as the other overlays) */
  if (saved && !gd_game_paused(0, 0) && view_camera(&cam)) {
    float spot[3] = { saved[0], saved[1], saved[2] };
    for (int i = 0; i < RING_SEGMENTS; i++) { /* the ring, level, round the spot */
      float a0 = TWO_PI * i / RING_SEGMENTS, a1 = TWO_PI * (i + 1) / RING_SEGMENTS;
      float p0[3] = { spot[0] + RING_RADIUS * cosf(a0), spot[1], spot[2] + RING_RADIUS * sinf(a0) };
      float p1[3] = { spot[0] + RING_RADIUS * cosf(a1), spot[1], spot[2] + RING_RADIUS * sinf(a1) };
      add_line(lines[f], &n, &cam, p0, p1);
    }
    float top[3] = { spot[0], spot[1] + POST_HEIGHT, spot[2] };
    add_line(lines[f], &n, &cam, spot, top);
    if (link_drawn_pos(&link)) {
      float from[3] = { link.x, link.y + LINK_MIDDLE, link.z };
      add_line(lines[f], &n, &cam, from, spot);
    }
  }
  n_lines[f] = n;
  publish_set(&published, n ? f : -1);
}

void spawnmark_take(void)
{
  taken = publish_take(&published);
}

const sm_line_t *spawnmark_lines(int *n)
{
  *n = taken < 0 ? 0 : n_lines[taken];
  return taken < 0 ? 0 : lines[taken];
}

void spawnmark_release(void)
{
  taken = -1;
  publish_release(&published);
}
