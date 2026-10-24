/*
 * switch_posix.c -- the POSIX calls the engine uses that newlib on Horizon
 * declares but libnx does not provide.
 *
 * These live in the executable rather than in the platform library so that
 * every library linked after it (kernel, platform, the port's own objects)
 * resolves against them, whatever the archive order.
 */
#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

/* pread and pwrite as seek, transfer, seek back. libnx's file descriptors
 * have no positioned I/O of their own. The lock makes the three steps one
 * as far as this process is concerned; it is the same lock for every
 * descriptor, which costs nothing here (the engine's positioned reads are
 * the disc image and the save images, one at a time). A caller mixing these
 * with plain read() on the same descriptor from another thread would still
 * race, as it would against any emulation of them. */
static pthread_mutex_t s_pio_lock = PTHREAD_MUTEX_INITIALIZER;

static ssize_t positioned(int fd, void *buf, size_t n, off_t off, int writing)
{
    ssize_t r;
    pthread_mutex_lock(&s_pio_lock);
    off_t was = lseek(fd, 0, SEEK_CUR);
    if (was < 0 || lseek(fd, off, SEEK_SET) < 0) {
        pthread_mutex_unlock(&s_pio_lock);
        return -1;
    }
    r = writing ? write(fd, buf, n) : read(fd, buf, n);
    int err = errno;
    lseek(fd, was, SEEK_SET);
    errno = err;
    pthread_mutex_unlock(&s_pio_lock);
    return r;
}

ssize_t pread(int fd, void *buf, size_t n, off_t off)
{
    return positioned(fd, buf, n, off, 0);
}

ssize_t pwrite(int fd, const void *buf, size_t n, off_t off)
{
    return positioned(fd, (void *)buf, n, off, 1);
}

long sysconf(int name)
{
    switch (name) {
    case _SC_PAGESIZE:
        return 0x1000;
#ifdef _SC_NPROCESSORS_ONLN
    case _SC_NPROCESSORS_ONLN:
    case _SC_NPROCESSORS_CONF:
        return 3;   /* the cores an application gets */
#endif
    default:
        errno = EINVAL;
        return -1;
    }
}

int posix_memalign(void **out, size_t align, size_t size)
{
    void *p;
    if (align < sizeof(void *) || (align & (align - 1)))
        return EINVAL;
    p = memalign(align, size ? size : 1);
    if (!p) return ENOMEM;
    *out = p;
    return 0;
}

/* Horizon has no signals. The one caller is SuspendThread's opt-in real
 * suspend (RECOMP_SUSPEND_REAL), off by default on every platform; with it
 * off, suspend is bookkeeping only, here as everywhere else. */
int pthread_kill(pthread_t thread, int sig)
{
    (void)thread; (void)sig;
    return ENOSYS;
}
