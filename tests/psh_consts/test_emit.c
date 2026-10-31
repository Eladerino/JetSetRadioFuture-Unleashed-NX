/* Run with: python tests/psh_consts/run.py
 *
 * The combiner constants as uniforms (nv2a_psh_consts.h), from the shaders'
 * side: src/nv2a_gl/nv2a_psh.c as it is, for GLSL and for Metal.
 * - Two states that differ only in their constants and the alpha test's
 *   reference give the same source, both languages: one program for a fade.
 * - The GLSL declares pshC[] once, with room for every constant, and the
 *   Metal struct has it; no constant is written into the source any more.
 * - Each stage's C0 and C1 read pshC[] where nv2a_psh_consts.h puts them:
 *   the stage's own, or stage 0's when the combiner control says every stage
 *   shares them; the final combiner its own pair, or under RECOMP_GL_OLD_OCW
 *   (argument "old") the last stage's, as before.
 * run.py also hands the GLSL written here (argv[2]) to glslangValidator
 * where there is one.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nv2a_psh.h"
#include "nv2a_psh_consts.h"

static int s_fails;
static char s_a[96 * 1024], s_b[96 * 1024];

static void expect(int ok, const char *what, int a, int b)
{
    if (!ok) { s_fails++; printf("FAIL %s (%d, %d)\n", what, a, b); }
}

static uint32_t s_seed = 777u;
static uint32_t rnd(void) { s_seed = s_seed * 1103515245u + 12345u; return s_seed >> 8; }

/* Every stage reads C0 and C1 (A and B of its colour half, A of its alpha
 * half) into r0; the final combiner reads C0 and C1 as its A and B. */
static void reading(Nv2aPshState *ps, int stages, int each0, int each1)
{
    int i;
    memset(ps, 0, sizeof *ps);
    ps->control = (uint32_t)stages | (each0 ? 1u << 12 : 0) | (each1 ? 1u << 16 : 0);
    for (i = 0; i < stages; i++) {
        ps->rgb_icw[i]   = (uint32_t)NV2A_PSH_REG_C0 << 24 | (uint32_t)NV2A_PSH_REG_C1 << 16;
        ps->rgb_ocw[i]   = (uint32_t)NV2A_PSH_REG_R0 << 4;
        ps->alpha_icw[i] = (uint32_t)NV2A_PSH_REG_C0 << 24 | (uint32_t)NV2A_PSH_REG_C1 << 16
                         | 1u << 28 | 1u << 20;     /* their alpha */
        ps->alpha_ocw[i] = (uint32_t)NV2A_PSH_REG_R0 << 4;
    }
    ps->final_abcd = (uint32_t)NV2A_PSH_REG_C0 << 24 | (uint32_t)NV2A_PSH_REG_C1 << 16
                   | (uint32_t)NV2A_PSH_REG_R0 << 8;
    ps->final_efg  = (uint32_t)NV2A_PSH_REG_R0 << 8;   /* G: the alpha */
}

static void constants(Nv2aPshState *ps)
{
    int i;
    for (i = 0; i < NV2A_PSH_STAGES; i++) { ps->c0[i] = rnd() ^ rnd() << 16; ps->c1[i] = rnd() ^ rnd() << 16; }
    ps->final_c0 = rnd() ^ rnd() << 16;
    ps->final_c1 = rnd() ^ rnd() << 16;
    ps->alpha_ref = (float)(rnd() % 256) / 255.0f;
}

/* Whether the shader's body reads pshC[k]: after main(, past the
 * declarations, which name pshC[] with its size. */
static int has(const char *src, int k)
{
    char want[32];
    const char *body = strstr(src, "main(");
    snprintf(want, sizeof want, "pshC[%d]", k);
    return body && strstr(body, want) != NULL;
}

int main(int argc, char **argv)
{
    int old = argc > 1 && !strcmp(argv[1], "old");
    FILE *out = argc > 2 ? fopen(argv[2], "w") : NULL;
    Nv2aPshState a, b;
    int stages, each, msl, r, k, n = 0;

    /* A fade: the same source through new constants, both languages, many
     * combiner set-ups. */
    for (r = 0; r < 2000; r++) {
        int i;
        memset(&a, 0, sizeof a);
        a.control = (1 + rnd() % 8) | (rnd() & 1) << 12 | (rnd() & 1) << 16;
        for (i = 0; i < NV2A_PSH_STAGES; i++) {
            a.rgb_icw[i] = rnd() ^ rnd() << 16; a.alpha_icw[i] = rnd() ^ rnd() << 16;
            a.rgb_ocw[i] = rnd() & 0x3FFFF;     a.alpha_ocw[i] = rnd() & 0x3FFFF;
        }
        a.final_abcd = rnd() ^ rnd() << 16; a.final_efg = (rnd() ^ rnd() << 16) & 0xFFFFFF00u;
        a.tex_bound[0] = (uint8_t)(rnd() & 1); a.tex_mode[0] = 1;
        a.alpha_test = (uint8_t)(rnd() & 1); a.alpha_func = 0x0200 + rnd() % 8;
        constants(&a);
        b = a;
        constants(&b);
        msl = r & 1;
        if (!(msl ? nv2a_psh_emit_msl(&a, s_a, sizeof s_a) : nv2a_psh_emit_glsl(&a, s_a, sizeof s_a))
            || !(msl ? nv2a_psh_emit_msl(&b, s_b, sizeof s_b) : nv2a_psh_emit_glsl(&b, s_b, sizeof s_b))) {
            expect(0, "emitted", r, msl);
            continue;
        }
        expect(!strcmp(s_a, s_b), "the same source with other constants", r, msl);
        { Nv2aPshState ka, kb;
          nv2a_psh_key(&ka, &a); nv2a_psh_key(&kb, &b);
          expect(!memcmp(&ka, &kb, sizeof ka), "the same key with other constants", r, msl); }
        expect(strstr(s_a, "c0_") == NULL && strstr(s_a, "c1_") == NULL,
               "no constant written into the source", r, msl);
        n++;
    }
    printf("psh_consts emit: %d combiner set-ups, the same source and key through new constants\n", n);

    /* Where each constant is read. */
    for (msl = 0; msl < 2; msl++)
        for (stages = 1; stages <= NV2A_PSH_STAGES; stages++)
            for (each = 0; each < 4; each++) {
                int each0 = each & 1, each1 = each >> 1, i;
                reading(&a, stages, each0, each1);
                constants(&a);
                if (!(msl ? nv2a_psh_emit_msl(&a, s_a, sizeof s_a) : nv2a_psh_emit_glsl(&a, s_a, sizeof s_a))) {
                    expect(0, "emitted", stages, each);
                    continue;
                }
                if (!msl) {
                    char decl[48];
                    const char *p = strstr(s_a, "uniform vec4 pshC[");
                    snprintf(decl, sizeof decl, "uniform vec4 pshC[%d];", NV2A_PSH_NCONSTS);
                    expect(p && !strncmp(p, decl, strlen(decl)) && !strstr(p + 1, "uniform vec4 pshC["),
                           "GLSL: pshC[] declared once, for every constant", stages, each);
                    if (out && stages == NV2A_PSH_STAGES && each == 3) fputs(s_a, out);
                } else {
                    char decl[48];
                    snprintf(decl, sizeof decl, "float4 pshC[%d];", NV2A_PSH_NCONSTS);
                    expect(strstr(s_a, decl) != NULL, "Metal: pshC[] in the struct", stages, each);
                }
                for (i = 0; i < NV2A_PSH_STAGES; i++) {
                    int w0 = i < stages && (each0 || old ? 1 : i == 0);
                    int w1 = i < stages && (each1 || old ? 1 : i == 0);
                    if (old && i == stages - 1) w0 = w1 = 1;      /* the final combiner's too */
                    expect(has(s_a, NV2A_PSH_C0(i)) == w0, "a stage's C0 where it should be", stages, i);
                    expect(has(s_a, NV2A_PSH_C1(i)) == w1, "a stage's C1 where it should be", stages, i);
                }
                expect(has(s_a, NV2A_PSH_FINAL_C0) == !old, "the final combiner's C0", stages, each);
                expect(has(s_a, NV2A_PSH_FINAL_C1) == !old, "the final combiner's C1", stages, each);
                for (k = NV2A_PSH_NCONSTS; k < NV2A_PSH_NCONSTS + 4; k++)
                    expect(!has(s_a, k), "nothing past the end of pshC[]", stages, k);
            }
    if (out) fclose(out);
    if (s_fails) { printf("psh_consts emit%s: %d case(s) FAIL\n", old ? " (old)" : "", s_fails); return 1; }
    printf("psh_consts emit%s: every constant read where nv2a_psh_consts.h puts it, GLSL and Metal\n",
           old ? " (RECOMP_GL_OLD_OCW)" : "");
    return 0;
}
