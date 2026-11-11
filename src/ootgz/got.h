/* Redirect a module's imports by rewriting its GOT entries (after rtld has linked it).
 * Used at module init on main: main's calls to e.g. nvnBootstrapLoader go through these
 * slots, so this works whatever order rtld searches modules in. */
#ifndef GOT_H
#define GOT_H

#include <stdint.h>

/* Address of `module`'s GOT slot for imported symbol `name` (via its MOD0 and dynamic
 * section), or 0. */
uintptr_t *got_slot(uintptr_t module, const char *name);

/* Points the slot at `replacement`, first setting *next to what the replacement must call: the slot's
 * old value when that's code in another module (the function, or another add-on's wrapper), else
 * `function`, the import's definition as linked into this module. main binds its imports lazily:
 * until the game's first call, a slot points at main's own PLT, which only works when entered from
 * that import's stub (x16 = the slot), so a wrapper must never call it.
 * Returns 0, or a message (the slot is then left alone). */
const char *got_hook(uintptr_t module, const char *name, void *replacement, void *function, void **next);

#endif
