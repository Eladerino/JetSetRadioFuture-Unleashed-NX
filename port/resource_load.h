/* The game's resource loads, checked (resource_load.c).
 *
 * One resource manager loads every kind of resource, one file at a time.
 * Its character loader (resource type 2, sub_00032C70) reads a character's
 * .dat into the manager's file buffer and releases it, but keeps the
 * pointer when the character's voice bank is loaded already. Both
 * character unloads release that pointer again. By then the heap may have
 * handed the address to another file, so the second release frees a file
 * in use. After each step of the loader, the pointer is forgotten once
 * the buffer it names has been released, as the loader's own other
 * releases of it do.
 *
 * The graffiti cache builder (resource type 25, sub_0002DBE0) does the
 * same with its own file buffer: it releases each texture after copying
 * it into a sheet and keeps the pointer, then releases it again as it
 * writes the sheet, a step later, when the heap may have handed the
 * address out in between. After each of its releases, the pointer is
 * forgotten once released.
 *
 * Both are the game's own code: its instructions release and keep the
 * pointer, and the generated code does the same. The runtime cannot tell
 * a pointer kept after its release from a block the heap handed out
 * again, so the fix has to live in the game's callers, not the heap.
 *
 * The hooks in port/recomp_manual.c call these. The analysis and the
 * shape of the fix are spriteice's (JSRF-PC, port/resource_load.c);
 * this is an independent implementation of it for this tree.
 */
#ifndef JSRF_RESOURCE_LOAD_H
#define JSRF_RESOURCE_LOAD_H

#include <stdint.h>

/* Nonzero when no live allocation holds guest address `va`: it was released. */
typedef int (*resource_load_released_fn)(uint32_t va);

/* After a step of the character loader on the resource manager at `mgr`.
 * `mem` is guest memory (host pointer to guest VA 0), `t` the game's time
 * in seconds, for the log. */
void resource_load_char_step(uint8_t *mem, uint32_t mgr, resource_load_released_fn released,
                             double t);

/* After one of the graffiti cache builder's functions that release its file
 * buffer (sub_0002DA70, sub_0002D8D0, sub_0002D950, sub_0002D9D0), on the
 * resource manager at `mgr`. */
void resource_load_graffiti_release(uint8_t *mem, uint32_t mgr, resource_load_released_fn released);

/* Before the builder writes sheet `sheet` (0 for the first sheet file,
 * sub_0002D8D0; 1 for the second, sub_0002D950): logs what was forgotten
 * since the last sheet, which the write would have released again. */
void resource_load_graffiti_sheet(int sheet, double t);

#endif
