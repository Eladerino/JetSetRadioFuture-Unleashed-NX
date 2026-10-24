/*
 * switch_mem.h -- guest memory on Horizon (Nintendo Switch).
 *
 * Horizon/newlib has no <sys/mman.h>. This is the replacement the platform
 * layer calls instead of mmap/munmap/mprotect on the Switch build, plus the
 * one operation POSIX spells as mmap(MAP_SHARED) of a memfd: mapping another
 * view of a file-mapping object's backing. See switch_mem.c for the design.
 */
#ifndef SWITCH_MEM_H
#define SWITCH_MEM_H

#if defined(__SWITCH__)

#include <stddef.h>
#include <sys/types.h>

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_NORESERVE 0x4000
#define MAP_FAILED ((void *)-1)

/* The process-memory syscalls the backing is built on (0x73, 0x74, 0x75,
 * 0x77, 0x78) are hinted and the loader passed a process handle: true
 * under title takeover, false in applet mode. */
int   sw_mem_available(void);

void *sw_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int   sw_munmap(void *addr, size_t len);
int   sw_mprotect(void *addr, size_t len, int prot);

/* Map [off, off + len) of a file-mapping object's backing at dst, or at an
 * address of the layer's choosing when dst is NULL. *backing is the object's
 * slot: created on the first view, shared by every later one, which is what
 * makes the views aliases of the same pages. Returns the view or NULL. */
void *sw_map_view(void **backing, size_t map_size, size_t off, size_t len,
                  void *dst);

/* One line in the log: views, commits, backed bytes, syscalls issued. */
void  sw_mem_log_summary(void);

#endif /* __SWITCH__ */
#endif /* SWITCH_MEM_H */
