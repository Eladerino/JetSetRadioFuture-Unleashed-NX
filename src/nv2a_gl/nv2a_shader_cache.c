/*
 * The shader cache -- see nv2a_shader_cache.h.
 *
 * Small enough to keep whole in memory: a title has a few thousand programs at
 * most, not hundreds of thousands, each found through an index (Idx, below)
 * rather than a scan -- the engine's list of shaders puts all of the game's in
 * at the first start. The binaries are the only part that can be large (tens
 * of kilobytes each on some drivers), so they stay on disk and are read when
 * asked for.
 */
#include "nv2a_shader_cache.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#  include <direct.h>
#  include <io.h>
#  include <fcntl.h>
#  include <sys/stat.h>
static CRITICAL_SECTION s_lock;
static volatile LONG s_lock_init;
static void lock(void)
{
    if (InterlockedCompareExchange(&s_lock_init, 1, 0) == 0) {
        InitializeCriticalSection(&s_lock);
        InterlockedExchange(&s_lock_init, 2);
    }
    while (s_lock_init != 2) Sleep(0);
    EnterCriticalSection(&s_lock);
}
static void unlock(void) { LeaveCriticalSection(&s_lock); }
static FILE *sc_fopen(const char *path, const char *mode)
{
    wchar_t wp[2048], wm[8];
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, 2048)) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, 0, mode, -1, wm, 8)) return NULL;
    return _wfopen(wp, wm);
}
static void sc_mkdir(const char *path)
{
    wchar_t wp[2048];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, 2048)) _wmkdir(wp);
}
static void sc_truncate(const char *path, long len)
{
    wchar_t wp[2048];
    int fd;
    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, 2048)) return;
    fd = _wopen(wp, _O_RDWR | _O_BINARY);
    if (fd < 0) return;
    _chsize_s(fd, len);
    _close(fd);
}
#else
#  include <pthread.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static void lock(void)   { pthread_mutex_lock(&s_mutex); }
static void unlock(void) { pthread_mutex_unlock(&s_mutex); }
static FILE *sc_fopen(const char *path, const char *mode) { return fopen(path, mode); }
static void sc_mkdir(const char *path) { mkdir(path, 0755); }
static void sc_truncate(const char *path, long len) { if (truncate(path, (off_t)len)) { /* nothing */ } }
#endif

#define MAGIC_SRC  0x3153534Au   /* "JSS1": a source text */
#define MAGIC_PROG 0x3150534Au   /* "JSP1": a program, as two source hashes */
#define MAGIC_RCP  0x3152534Au   /* "JSR1": a pipeline recipe */
#define MAGIC_BIN  0x3242534Au   /* "JSB2": a compiled program, with a hash of its bytes */
#define MAGIC_DROP 0x5842534Au   /* "JSBX": a compiled program to forget */

#define MAX_PROGRAMS_FILE (64L << 20)
#define MAX_BINARIES_FILE (256L << 20)
#define MAX_TEXT          (512u << 10)
#define MAX_BLOB          (8u << 20)

/* ---- an index: 64-bit keys to positions in a table -----------------------
 * Open addressing with linear probing, at most half full; a position of -1 is
 * an empty slot. Removal shifts the entries after it back, so lookups never
 * need markers. Out of memory, an insert fails and the caller goes without. */
typedef struct { uint64_t *k; int *v; int cap, n; } Idx;

static size_t idx_home(uint64_t x, int cap)
{
    x ^= x >> 33; x *= 0xFF51AFD7ED558CCDull;
    x ^= x >> 33; x *= 0xC4CEB9FE1A85EC53ull;
    x ^= x >> 33;
    return (size_t)x & (size_t)(cap - 1);
}

static int idx_get(const Idx *ix, uint64_t key)
{
    size_t i, m;
    if (!ix->cap) return -1;
    m = (size_t)ix->cap - 1;
    for (i = idx_home(key, ix->cap); ix->v[i] >= 0; i = (i + 1) & m)
        if (ix->k[i] == key) return ix->v[i];
    return -1;
}

/* Add the key, or move it to a new position (which never needs memory): 0
 * when out of memory. */
static int idx_set(Idx *ix, uint64_t key, int pos)
{
    size_t i, m;
    if (ix->cap) {
        m = (size_t)ix->cap - 1;
        for (i = idx_home(key, ix->cap); ix->v[i] >= 0; i = (i + 1) & m)
            if (ix->k[i] == key) { ix->v[i] = pos; return 1; }
    }
    if ((ix->n + 1) * 2 > ix->cap) {
        int nc = ix->cap ? ix->cap * 2 : 512, j;
        uint64_t *nk = (uint64_t *)malloc((size_t)nc * sizeof *nk);
        int *nv = (int *)malloc((size_t)nc * sizeof *nv);
        if (!nk || !nv) { free(nk); free(nv); return 0; }
        for (j = 0; j < nc; j++) nv[j] = -1;
        for (j = 0; j < ix->cap; j++)
            if (ix->v[j] >= 0) {
                size_t q = idx_home(ix->k[j], nc);
                while (nv[q] >= 0) q = (q + 1) & (size_t)(nc - 1);
                nk[q] = ix->k[j]; nv[q] = ix->v[j];
            }
        free(ix->k); free(ix->v);
        ix->k = nk; ix->v = nv; ix->cap = nc;
    }
    m = (size_t)ix->cap - 1;
    for (i = idx_home(key, ix->cap); ix->v[i] >= 0; i = (i + 1) & m) { /* the first empty slot */ }
    ix->k[i] = key; ix->v[i] = pos; ix->n++;
    return 1;
}

static void idx_del(Idx *ix, uint64_t key)
{
    size_t i, j, h, m;
    if (!ix->cap) return;
    m = (size_t)ix->cap - 1;
    for (i = idx_home(key, ix->cap); ix->v[i] >= 0; i = (i + 1) & m)
        if (ix->k[i] == key) break;
    if (ix->v[i] < 0) return;
    for (j = (i + 1) & m; ix->v[j] >= 0; j = (j + 1) & m) {
        /* the entry at j stays unless its probe from home passes i */
        h = idx_home(ix->k[j], ix->cap);
        if (i < j ? (h <= i || h > j) : (h <= i && h > j)) {
            ix->k[i] = ix->k[j]; ix->v[i] = ix->v[j];
            i = j;
        }
    }
    ix->v[i] = -1;
    ix->n--;
}

typedef struct { uint64_t h; uint32_t len; char *text; } Src;
typedef struct { uint64_t key; int vi, fi; } Prog;
typedef struct { uint64_t key; uint32_t recipe; int next; } Rcp;   /* next: the program's next recipe, or -1 */
typedef struct { uint64_t key, hash; uint32_t format, len; long off; } Bin;

static struct {
    int   open, bin_open;
    char  pfile[1200], rfile[1200], bfile[1400];
    FILE *pf, *rf, *bf;            /* appended to */
    long  psize, bsize;
    Src  *src;  int nsrc,  capsrc;
    Prog *prog; int nprog, capprog;
    Rcp  *rcp;  int nrcp,  caprcp;
    Bin  *bin;  int nbin,  capbin;
    Idx   isrc, iprog, ircp, ibin;   /* by text hash, by key, by key (its first recipe), by key */
    int   added;
    int   pbad, rbad, bbad;          /* a write failed (a full disk): no more appends this run */
} C;

static uint64_t fnv64(uint64_t h, const void *p, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;
    while (n--) { h ^= *b++; h *= 0x100000001B3ull; }
    return h;
}
static uint64_t text_hash(const char *s)
{
    return fnv64(0xCBF29CE484222325ull, s, strlen(s));
}

uint64_t nv2a_sc_key(const char *vsrc, const char *fsrc)
{
    uint64_t v = text_hash(vsrc ? vsrc : ""), f = text_hash(fsrc ? fsrc : "");
    uint64_t k = fnv64(0xCBF29CE484222325ull, &v, sizeof v);
    k = fnv64(k, &f, sizeof f);
    return k ? k : 1;
}

static int grow(void **arr, int *cap, int n, size_t each)
{
    void *p;
    int nc;
    if (n < *cap) return 1;
    nc = *cap ? *cap * 2 : 256;
    p = realloc(*arr, (size_t)nc * each);
    if (!p) return 0;
    *arr = p; *cap = nc;
    return 1;
}

static int src_find(uint64_t h) { return idx_get(&C.isrc, h); }
static int src_put(uint64_t h, const char *text, uint32_t len)
{
    int i = src_find(h);
    char *t;
    if (i >= 0) return i;
    if (!grow((void **)&C.src, &C.capsrc, C.nsrc, sizeof *C.src)) return -1;
    t = (char *)malloc((size_t)len + 1);
    if (!t) return -1;
    if (!idx_set(&C.isrc, h, C.nsrc)) { free(t); return -1; }
    memcpy(t, text, len); t[len] = 0;
    C.src[C.nsrc].h = h; C.src[C.nsrc].len = len; C.src[C.nsrc].text = t;
    return C.nsrc++;
}
static int prog_find(uint64_t key) { return idx_get(&C.iprog, key); }
/* A program at the end of the table: 0 when out of memory. */
static int prog_push(uint64_t key, int vi, int fi)
{
    if (!grow((void **)&C.prog, &C.capprog, C.nprog, sizeof *C.prog) || !idx_set(&C.iprog, key, C.nprog))
        return 0;
    C.prog[C.nprog].key = key; C.prog[C.nprog].vi = vi; C.prog[C.nprog].fi = fi;
    C.nprog++;
    return 1;
}
/* A recipe for a program, unless it has it: 1 if it went in. Each program's
 * recipes are a chain, in the order they came, from its entry in C.ircp. */
static int rcp_push(uint64_t key, uint32_t recipe)
{
    int i = idx_get(&C.ircp, key), last = -1;
    for (; i >= 0; i = C.rcp[i].next) {
        if (C.rcp[i].recipe == recipe) return 0;
        last = i;
    }
    if (!grow((void **)&C.rcp, &C.caprcp, C.nrcp, sizeof *C.rcp)) return 0;
    if (last < 0 && !idx_set(&C.ircp, key, C.nrcp)) return 0;
    C.rcp[C.nrcp].key = key; C.rcp[C.nrcp].recipe = recipe; C.rcp[C.nrcp].next = -1;
    if (last >= 0) C.rcp[last].next = C.nrcp;
    C.nrcp++;
    return 1;
}

/* Read the programs file into memory. Returns the length of the part that is
 * whole records; a damaged tail is cut off. */
static long load_programs(FILE *f)
{
    long good = 0;
    char *buf = NULL;
    for (;;) {
        uint32_t magic;
        if (fread(&magic, 4, 1, f) != 1) break;
        if (magic == MAGIC_SRC) {
            uint64_t h; uint32_t len;
            if (fread(&h, 8, 1, f) != 1 || fread(&len, 4, 1, f) != 1 || len > MAX_TEXT) break;
            buf = (char *)realloc(buf, (size_t)len + 1);
            if (!buf || (len && fread(buf, 1, len, f) != len)) break;
            buf[len] = 0;
            if (text_hash(buf) != h || src_put(h, buf, len) < 0) break;
        } else if (magic == MAGIC_PROG) {
            uint64_t key, vh, fh;
            int vi, fi;
            if (fread(&key, 8, 1, f) != 1 || fread(&vh, 8, 1, f) != 1 || fread(&fh, 8, 1, f) != 1) break;
            vi = src_find(vh); fi = src_find(fh);
            if (vi < 0 || fi < 0) break;
            if (prog_find(key) < 0 && !prog_push(key, vi, fi)) break;
        } else {
            break;
        }
        good = ftell(f);
    }
    free(buf);
    return good;
}

static long load_recipes(FILE *f)
{
    long good = 0;
    for (;;) {
        uint32_t magic, recipe; uint64_t key;
        if (fread(&magic, 4, 1, f) != 1 || magic != MAGIC_RCP) break;
        if (fread(&key, 8, 1, f) != 1 || fread(&recipe, 4, 1, f) != 1) break;
        rcp_push(key, recipe);            /* once each */
        good = ftell(f);
    }
    return good;
}

/* One entry a key at most: a newer binary for it replaces the older one. */
static void bin_forget(uint64_t key)
{
    int i = idx_get(&C.ibin, key);
    if (i < 0) return;
    idx_del(&C.ibin, key);
    if (i != --C.nbin) {
        C.bin[i] = C.bin[C.nbin];
        idx_set(&C.ibin, C.bin[i].key, i);   /* a key already in: never needs memory */
    }
}
/* At the end of the table, after bin_forget(key): 0 when out of memory. */
static int bin_push(uint64_t key, uint64_t hash, uint32_t format, uint32_t len, long off)
{
    if (!grow((void **)&C.bin, &C.capbin, C.nbin, sizeof *C.bin) || !idx_set(&C.ibin, key, C.nbin)) return 0;
    C.bin[C.nbin].key = key; C.bin[C.nbin].hash = hash; C.bin[C.nbin].format = format;
    C.bin[C.nbin].len = len; C.bin[C.nbin].off = off;
    C.nbin++;
    return 1;
}

/* The binaries stay on disk: only where each one is, and a hash of it, which
 * nv2a_sc_bin_get checks -- a driver handed a damaged binary is not something
 * to count on catching it. */
static long load_binaries(FILE *f)
{
    long good = 0, end;
    if (fseek(f, 0, SEEK_END)) return 0;
    end = ftell(f);
    rewind(f);
    for (;;) {
        uint32_t magic, format, len; uint64_t key, hash; long off;
        if (fread(&magic, 4, 1, f) != 1) break;
        if (magic == MAGIC_DROP) {
            if (fread(&key, 8, 1, f) != 1) break;
            bin_forget(key);
        } else if (magic == MAGIC_BIN) {
            if (fread(&key, 8, 1, f) != 1 || fread(&format, 4, 1, f) != 1 || fread(&len, 4, 1, f) != 1 ||
                fread(&hash, 8, 1, f) != 1) break;
            if (len == 0 || len > MAX_BLOB) break;
            off = ftell(f);
            /* the file must hold all of it: a short last record ends the read */
            if (off < 0 || off + (long)len > end || fseek(f, (long)len, SEEK_CUR)) break;
            bin_forget(key);
            if (!bin_push(key, hash, format, len, off)) break;
        } else {
            break;
        }
        good = ftell(f);
    }
    return good;
}

/* Open `path` for reading, load it with `load`, cut off a damaged tail, and
 * open it again for appending. The size goes to *size. */
static FILE *open_log(const char *path, long (*load)(FILE *), long *size)
{
    FILE *f = sc_fopen(path, "rb");
    long good = 0, end = 0;
    if (f) {
        good = load(f);
        if (!fseek(f, 0, SEEK_END)) end = ftell(f);
        fclose(f);
        if (end > good) {
            fprintf(stderr, "  [SHADERS] %s: %ld damaged bytes at the end dropped\n", path, end - good);
            sc_truncate(path, good);
        }
    }
    *size = good;
    return sc_fopen(path, "ab");
}

int nv2a_sc_open(const char *dir, const char *tag)
{
    int ok;
    lock();
    if (C.open) { unlock(); return 1; }
    sc_mkdir(dir);
    snprintf(C.pfile, sizeof C.pfile, "%s/programs-%s.dat", dir, tag);
    snprintf(C.rfile, sizeof C.rfile, "%s/recipes-%s.dat", dir, tag);
    C.pf = open_log(C.pfile, load_programs, &C.psize);
    { long rs; C.rf = open_log(C.rfile, load_recipes, &rs); }
    ok = C.pf != NULL;
    if (ok) C.open = 1;
    else fprintf(stderr, "  [SHADERS] cannot write %s: %s -- no shader cache\n", C.pfile, strerror(errno));
    unlock();
    if (ok)
        fprintf(stderr, "  [SHADERS] cache %s: %d programs from earlier runs (%d sources)\n",
                C.pfile, C.nprog, C.nsrc);
    return ok;
}

int nv2a_sc_is_open(void) { return C.open; }

int nv2a_sc_count(void)
{
    int n;
    lock(); n = C.nprog; unlock();
    return n;
}

int nv2a_sc_entry(int i, uint64_t *key, const char **vsrc, const char **fsrc)
{
    int ok = 0;
    lock();
    if (i >= 0 && i < C.nprog) {
        *key = C.prog[i].key;
        *vsrc = C.src[C.prog[i].vi].text;
        *fsrc = C.src[C.prog[i].fi].text;
        ok = 1;
    }
    unlock();
    return ok;
}

static int write_src(uint64_t h, const char *text, uint32_t len)
{
    uint32_t magic = MAGIC_SRC;
    if (fwrite(&magic, 4, 1, C.pf) != 1 || fwrite(&h, 8, 1, C.pf) != 1 ||
        fwrite(&len, 4, 1, C.pf) != 1 || (len && fwrite(text, 1, len, C.pf) != len)) return 0;
    C.psize += 16 + (long)len;
    return 1;
}

void nv2a_sc_add(uint64_t key, const char *vsrc, const char *fsrc)
{
    uint64_t vh, fh;
    uint32_t vl, fl, magic = MAGIC_PROG;
    int vi, fi, newv, newf;
    if (!C.open || !vsrc || !fsrc) return;
    vl = (uint32_t)strlen(vsrc); fl = (uint32_t)strlen(fsrc);
    if (vl > MAX_TEXT || fl > MAX_TEXT) return;
    vh = text_hash(vsrc); fh = text_hash(fsrc);
    lock();
    if (prog_find(key) >= 0 || !C.pf || C.pbad || C.psize + (long)vl + (long)fl + 64 > MAX_PROGRAMS_FILE) { unlock(); return; }
    newv = src_find(vh) < 0; newf = src_find(fh) < 0;
    vi = src_put(vh, vsrc, vl); fi = src_put(fh, fsrc, fl);
    if (vi < 0 || fi < 0 || !prog_push(key, vi, fi)) { unlock(); return; }
    C.added++;
    /* A record half written would end the file for the next run's read, and
     * everything after it with it: so after a failed write, nothing more. */
    if ((!newv || write_src(vh, vsrc, vl)) && (!newf || write_src(fh, fsrc, fl)) &&
        fwrite(&magic, 4, 1, C.pf) == 1 && fwrite(&key, 8, 1, C.pf) == 1 &&
        fwrite(&vh, 8, 1, C.pf) == 1 && fwrite(&fh, 8, 1, C.pf) == 1)
        C.psize += 28;
    else
        C.pbad = 1;
    if (fflush(C.pf)) C.pbad = 1;
    unlock();
}

void nv2a_sc_recipe_add(uint64_t key, uint32_t recipe)
{
    uint32_t magic = MAGIC_RCP;
    if (!C.open) return;
    lock();
    if (rcp_push(key, recipe)) {
        if (C.rf && !C.rbad) {
            if (fwrite(&magic, 4, 1, C.rf) != 1 || fwrite(&key, 8, 1, C.rf) != 1 ||
                fwrite(&recipe, 4, 1, C.rf) != 1 || fflush(C.rf)) C.rbad = 1;
        }
    }
    unlock();
}

int nv2a_sc_recipes(uint64_t key, uint32_t *out, int max)
{
    int i, n = 0;
    lock();
    for (i = idx_get(&C.ircp, key); i >= 0 && n < max; i = C.rcp[i].next) out[n++] = C.rcp[i].recipe;
    unlock();
    return n;
}

int nv2a_sc_bin_open(const char *driver)
{
    char *p;
    uint64_t h;
    if (!C.open || !driver) return 0;
    h = text_hash(driver);
    lock();
    if (C.bin_open) { unlock(); return 1; }
    /* the programs file's name, with the driver's hash in place of the kind */
    snprintf(C.bfile, sizeof C.bfile, "%s", C.pfile);
    p = strstr(C.bfile, "/programs-");
    if (p) {
        char tail[600];
        snprintf(tail, sizeof tail, "%s", p + 10);                  /* "<tag>.dat" */
        if (strlen(tail) > 4) tail[strlen(tail) - 4] = 0;
        snprintf(p, sizeof C.bfile - (size_t)(p - C.bfile), "/binaries-%s-%08X.dat",
                 tail, (unsigned)(h ^ (h >> 32)));
    }
    C.bf = open_log(C.bfile, load_binaries, &C.bsize);
    C.bin_open = C.bf != NULL;
    unlock();
    if (C.bin_open)
        fprintf(stderr, "  [SHADERS] compiled programs for this driver: %d (%s)\n", C.nbin, C.bfile);
    return C.bin_open;
}

int nv2a_sc_bin_has(uint64_t key)
{
    int has;
    if (!C.bin_open) return 0;
    lock();
    has = idx_get(&C.ibin, key) >= 0;
    unlock();
    return has;
}

void *nv2a_sc_bin_get(uint64_t key, uint32_t *len, uint32_t *format)
{
    void *data = NULL;
    int i;
    if (!C.bin_open) return NULL;
    lock();
    if ((i = idx_get(&C.ibin, key)) >= 0) {
        FILE *f = sc_fopen(C.bfile, "rb");
        if (f && !fseek(f, C.bin[i].off, SEEK_SET) && (data = malloc(C.bin[i].len)) != NULL &&
            fread(data, 1, C.bin[i].len, f) == C.bin[i].len &&
            fnv64(0xCBF29CE484222325ull, data, C.bin[i].len) == C.bin[i].hash) {
            *len = C.bin[i].len; *format = C.bin[i].format;
        } else {
            free(data); data = NULL;
        }
        if (f) fclose(f);
    }
    unlock();
    return data;
}

void nv2a_sc_bin_put(uint64_t key, const void *data, uint32_t len, uint32_t format)
{
    uint32_t magic = MAGIC_BIN;
    uint64_t hash;
    long off;
    if (!C.bin_open || !data || !len || len > MAX_BLOB) return;
    hash = fnv64(0xCBF29CE484222325ull, data, len);
    lock();
    if (C.bbad || C.bsize + (long)len + 28 > MAX_BINARIES_FILE) { unlock(); return; }
    off = C.bsize + 28;
    if (fwrite(&magic, 4, 1, C.bf) == 1 && fwrite(&key, 8, 1, C.bf) == 1 &&
        fwrite(&format, 4, 1, C.bf) == 1 && fwrite(&len, 4, 1, C.bf) == 1 &&
        fwrite(&hash, 8, 1, C.bf) == 1 && fwrite(data, 1, len, C.bf) == len) {
        bin_forget(key);
        bin_push(key, hash, format, len, off);   /* without memory: on disk, found next run */
        C.bsize += 28 + (long)len;
    } else {
        C.bbad = 1;                  /* the offsets after it would be wrong */
    }
    if (fflush(C.bf)) C.bbad = 1;
    unlock();
}

void nv2a_sc_bin_drop(uint64_t key)
{
    uint32_t magic = MAGIC_DROP;
    if (!C.bin_open) return;
    lock();
    bin_forget(key);
    if (!C.bbad) {
        if (fwrite(&magic, 4, 1, C.bf) == 1 && fwrite(&key, 8, 1, C.bf) == 1) C.bsize += 12;
        else C.bbad = 1;
        if (fflush(C.bf)) C.bbad = 1;
    }
    unlock();
}

void nv2a_sc_stats(int *programs, int *binaries, int *added)
{
    lock();
    if (programs) *programs = C.nprog;
    if (binaries) *binaries = C.nbin;
    if (added) *added = C.added;
    unlock();
}

/* ---- states: what each program was translated from ---------------------------
 *
 * See nv2a_shader_cache.h. One file, states-v1.dat, beside the programs files
 * and shared by every backend, of three kinds of record:
 *
 *   "JVS1" vid, kind, len, bytes   a vertex half: the program's microcode (kind 1)
 *                                  or the fixed-function description (kind 2),
 *                                  once however many programs use it;
 *   "JPS1" id, vid, len, bytes     a program: its key in the renderer's table, its
 *                                  vertex half, and its combiner state;
 *   "JLB1" id, recipe              a blend recipe it was drawn with (Metal's).
 *
 * The list carried in the engine (nv2a_sc_states_add_blob) is the same
 * records, read but never written. */
#define MAGIC_VS  0x3153564Au   /* "JVS1" */
#define MAGIC_PS  0x3153504Au   /* "JPS1" */
#define MAGIC_BL  0x31424C4Au   /* "JLB1" */
#define MAX_STATE (8u << 10)

typedef struct { uint32_t vid, len; int kind; unsigned char *data; } SVs;
typedef struct { uint32_t id, vid, len; unsigned char *psh; } SPs;
typedef struct { uint32_t id, recipe; int next; } SBl;   /* next: the program's next recipe, or -1 */

static struct {
    int   open, bad, local;            /* local: programs that came from this machine's file */
    char  file[1200];
    FILE *f;
    SVs  *vs; int nvs, capvs;
    SPs  *ps; int nps, capps;
    SBl  *bl; int nbl, capbl;
    Idx   ivs, ips, ibl;               /* by vid, by id, by id (its first recipe) */
} ST;

static int st_vs_find(uint32_t vid) { return idx_get(&ST.ivs, vid); }
static int st_ps_find(uint32_t id)  { return idx_get(&ST.ips, id); }
static int st_bl_find(uint32_t id, uint32_t recipe)
{
    int i;
    for (i = idx_get(&ST.ibl, id); i >= 0; i = ST.bl[i].next) if (ST.bl[i].recipe == recipe) return i;
    return -1;
}
static int st_vs_put(uint32_t vid, int kind, const void *data, uint32_t len)
{
    unsigned char *d;
    int i = st_vs_find(vid);
    if (i >= 0) return i;
    if (!grow((void **)&ST.vs, &ST.capvs, ST.nvs, sizeof *ST.vs) || !(d = (unsigned char *)malloc(len ? len : 1)))
        return -1;
    if (!idx_set(&ST.ivs, vid, ST.nvs)) { free(d); return -1; }
    memcpy(d, data, len);
    ST.vs[ST.nvs].vid = vid; ST.vs[ST.nvs].kind = kind; ST.vs[ST.nvs].len = len; ST.vs[ST.nvs].data = d;
    return ST.nvs++;
}
static int st_ps_put(uint32_t id, uint32_t vid, const void *psh, uint32_t len)
{
    unsigned char *d;
    int i = st_ps_find(id);
    if (i >= 0) return i;
    if (!grow((void **)&ST.ps, &ST.capps, ST.nps, sizeof *ST.ps) || !(d = (unsigned char *)malloc(len ? len : 1)))
        return -1;
    if (!idx_set(&ST.ips, id, ST.nps)) { free(d); return -1; }
    memcpy(d, psh, len);
    ST.ps[ST.nps].id = id; ST.ps[ST.nps].vid = vid; ST.ps[ST.nps].len = len; ST.ps[ST.nps].psh = d;
    return ST.nps++;
}
static int st_bl_put(uint32_t id, uint32_t recipe)
{
    int i, last = -1;
    for (i = idx_get(&ST.ibl, id); i >= 0; i = ST.bl[i].next) {
        if (ST.bl[i].recipe == recipe) return i;
        last = i;
    }
    if (!grow((void **)&ST.bl, &ST.capbl, ST.nbl, sizeof *ST.bl)) return -1;
    if (last < 0 && !idx_set(&ST.ibl, id, ST.nbl)) return -1;
    ST.bl[ST.nbl].id = id; ST.bl[ST.nbl].recipe = recipe; ST.bl[ST.nbl].next = -1;
    if (last >= 0) ST.bl[last].next = ST.nbl;
    return ST.nbl++;
}

/* Records from p[0..n): the length that was whole records. */
static size_t st_parse(const unsigned char *p, size_t n)
{
    size_t at = 0;
    for (;;) {
        uint32_t magic, a, b, len;
        if (n - at < 4) break;
        memcpy(&magic, p + at, 4);
        if (magic == MAGIC_BL) {
            if (n - at < 12) break;
            memcpy(&a, p + at + 4, 4); memcpy(&b, p + at + 8, 4);
            if (st_bl_put(a, b) < 0) break;
            at += 12;
        } else if (magic == MAGIC_VS || magic == MAGIC_PS) {
            if (n - at < 16) break;
            memcpy(&a, p + at + 4, 4); memcpy(&b, p + at + 8, 4); memcpy(&len, p + at + 12, 4);
            if (len > MAX_STATE || n - at - 16 < len) break;
            if (magic == MAGIC_VS ? st_vs_put(a, (int)b, p + at + 16, len) < 0
                                  : st_ps_put(a, b, p + at + 16, len) < 0) break;
            at += 16 + len;
        } else {
            break;
        }
    }
    return at;
}

static void st_write(uint32_t magic, uint32_t a, uint32_t b, const void *data, uint32_t len, int with_len)
{
    if (!ST.f || ST.bad) return;
    if (fwrite(&magic, 4, 1, ST.f) != 1 || fwrite(&a, 4, 1, ST.f) != 1 || fwrite(&b, 4, 1, ST.f) != 1 ||
        (with_len && (fwrite(&len, 4, 1, ST.f) != 1 || (len && fwrite(data, 1, len, ST.f) != len))) ||
        fflush(ST.f))
        ST.bad = 1;
}

int nv2a_sc_states_open(const char *dir)
{
    FILE *f;
    unsigned char *buf = NULL;
    long end = 0;
    size_t good = 0;
    lock();
    if (ST.open) { unlock(); return 1; }
    sc_mkdir(dir);
    snprintf(ST.file, sizeof ST.file, "%s/states-v1.dat", dir);
    if ((f = sc_fopen(ST.file, "rb")) != NULL) {
        if (!fseek(f, 0, SEEK_END)) end = ftell(f);
        rewind(f);
        if (end > 0 && end < (64L << 20) && (buf = (unsigned char *)malloc((size_t)end)) != NULL &&
            fread(buf, 1, (size_t)end, f) == (size_t)end)
            good = st_parse(buf, (size_t)end);
        fclose(f);
        free(buf);
        if (end > (long)good) {
            fprintf(stderr, "  [SHADERS] %s: %ld damaged bytes at the end dropped\n", ST.file, end - (long)good);
            sc_truncate(ST.file, (long)good);
        }
    }
    ST.local = ST.nps;
    ST.f = sc_fopen(ST.file, "ab");
    ST.open = 1;
    unlock();
    return ST.f != NULL;
}

void nv2a_sc_states_add_blob(const void *data, size_t len)
{
    size_t good;
    if (!data || !len) return;
    lock();
    good = st_parse((const unsigned char *)data, len);
    unlock();
    if (good != len)
        fprintf(stderr, "  [SHADERS] the engine's list of shaders is damaged after %lu of %lu bytes\n",
                (unsigned long)good, (unsigned long)len);
}

int nv2a_sc_states_count(void) { int n; lock(); n = ST.nps; unlock(); return n; }
int nv2a_sc_states_local(void) { int n; lock(); n = ST.local; unlock(); return n; }

int nv2a_sc_state_get(int i, uint32_t *id, int *kind, const void **vdata, uint32_t *vlen,
                      const void **psh, uint32_t *pshlen)
{
    int ok = 0, v;
    lock();
    if (i >= 0 && i < ST.nps && (v = st_vs_find(ST.ps[i].vid)) >= 0) {
        *id = ST.ps[i].id; *kind = ST.vs[v].kind;
        *vdata = ST.vs[v].data; *vlen = ST.vs[v].len;
        *psh = ST.ps[i].psh; *pshlen = ST.ps[i].len;
        ok = 1;
    }
    unlock();
    return ok;
}

int nv2a_sc_state_recipes(uint32_t id, uint32_t *out, int max)
{
    int i, n = 0;
    lock();
    for (i = idx_get(&ST.ibl, id); i >= 0 && n < max; i = ST.bl[i].next) out[n++] = ST.bl[i].recipe;
    unlock();
    return n;
}

void nv2a_sc_state_note(uint32_t id, int kind, const void *vdata, uint32_t vlen, const void *psh, uint32_t pshlen)
{
    uint32_t vid;
    if (!ST.open || !id || !vdata || !psh || vlen > MAX_STATE || pshlen > MAX_STATE) return;
    vid = (uint32_t)fnv64(0xCBF29CE484222325ull ^ (uint64_t)kind, vdata, vlen);
    lock();
    if (st_ps_find(id) >= 0) { unlock(); return; }
    if (st_vs_find(vid) < 0) {
        if (st_vs_put(vid, kind, vdata, vlen) < 0) { unlock(); return; }
        st_write(MAGIC_VS, vid, (uint32_t)kind, vdata, vlen, 1);
    }
    if (st_ps_put(id, vid, psh, pshlen) >= 0) st_write(MAGIC_PS, id, vid, psh, pshlen, 1);
    unlock();
}

void nv2a_sc_state_recipe(uint32_t id, uint32_t recipe)
{
    if (!ST.open || !id) return;
    lock();
    if (st_bl_find(id, recipe) < 0 && st_bl_put(id, recipe) >= 0) st_write(MAGIC_BL, id, recipe, NULL, 0, 0);
    unlock();
}
