/* nv2a_perf_defaults.h -- the default for the walk's measured perf switches.
 *
 * The November perf campaign (perf-campaign/README.md, "The fold-in set")
 * proved these on hardware at stock clocks, each with its own key:
 *
 *   RECOMP_PB_FENCESCAN    B.1 fence pre-scan as a range test   ~1.10 ms/frame
 *   RECOMP_DK_NOTE_TRIM    unhandled-method noting, count only  ~0.97 ms/frame
 *   RECOMP_PB_DATASHADOW   B.3 no shadow for index data ports   ~0.44 ms/frame
 *   RECOMP_PB_NEONIDX      NEON ARRAY_ELEMENT16 unpack          ~0.54 ms/frame
 *   RECOMP_DK_CAPTURE_FAST S-2 capture-chain pre-test           ~0.28 ms/frame
 *   RECOMP_PB_VPBATCH      vertex-program upload in rows        (desk only)
 *
 * but every key stayed off unless options.txt named it, and neither the
 * release options.txt nor the built-in defaults did. So they are on by
 * default now. Each key still works on its own (=0 off, =1 on), and
 * JSRF_PERF_DEFAULTS=0 puts every key that options.txt does not name
 * back to the old default (off), for one-line A/B runs.
 *
 * Read once per switch, like the switches themselves: getenv walks the
 * whole environment. */
#ifndef NV2A_PERF_DEFAULTS_H
#define NV2A_PERF_DEFAULTS_H

#include <stdlib.h>

static inline int nv2a_perf_default(void)
{
    static int d = -1;

    if (d < 0) {
        const char *e = getenv("JSRF_PERF_DEFAULTS");
        d = !(e && e[0] == '0');
    }
    return d;
}

/* A key that is on with "1"; absent, the default above. */
static inline int nv2a_perf_switch(const char *key)
{
    const char *e = getenv(key);

    return e ? e[0] == '1' : nv2a_perf_default();
}

#endif /* NV2A_PERF_DEFAULTS_H */
