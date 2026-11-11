/* On-screen drawing through the game's own NVN (see nvn.c). */
#ifndef NVN_H
#define NVN_H

#include <stdint.h>

/* Point main's nvnBootstrapLoader import at our wrapper (module init); returns 0 or a message. */
const char *nvn_hook_game(uintptr_t main_base);

#endif
