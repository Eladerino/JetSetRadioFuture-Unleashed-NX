/* nv2a_ae16_neon.h -- NEON form of the ARRAY_ELEMENT16 index unpack.
 *
 * Shared by nv2a_pb_exec_dk.c (behind RECOMP_PB_NEONIDX) and the desk
 * harness that gates it, so the code the harness proves bit-exact is
 * the code that ships -- not a copy of it that can drift.
 *
 * Semantics are the scalar loop's, exactly: p[0..k) guest words are
 * unpacked to d[0..2k) as (low half, high half) pairs in guest order,
 * and the running range seeded by lo/hi is widened over every index
 * written. k is the run length AFTER the caller's capacity clamp
 * ((NV_MAX_INDICES - idx_count) / 2); this function writes nothing
 * past d[2k) and reads nothing past p[k).
 *
 * The widening needs no shuffle: guest word j holds index 2j in its
 * low half and 2j+1 in its high half, so a u32 vector of four guest
 * words, read as eight u16 lanes, IS the index sequence in order.
 * ushll on the low and high u16 halves widens it to the two u32
 * vectors to store. Range accumulation is vector min/max seeded
 * with the batch's running lo/hi, reduced horizontally once per
 * vector stretch; the tail (k % 4 words) runs the scalar ops
 * verbatim, so short and clamped runs take the same path the
 * shipped loop does, word for word.
 */
#ifndef NV2A_AE16_NEON_H
#define NV2A_AE16_NEON_H

#include <stdint.h>

#if defined(__aarch64__)

#include <arm_neon.h>

static inline void nv2a_ae16_unpack_neon(const uint32_t *p, uint32_t k,
                                         uint32_t *d,
                                         uint32_t lo, uint32_t hi,
                                         uint32_t *lo_out, uint32_t *hi_out)
{
    uint32_t i = 0;

    if (k >= 4) {
        uint32x4_t acc_lo = vdupq_n_u32(lo);
        uint32x4_t acc_hi = vdupq_n_u32(hi);
        for (; i + 4 <= k; i += 4) {
            uint32x4_t w    = vld1q_u32(p + i);
            uint16x8_t h    = vreinterpretq_u16_u32(w);
            uint32x4_t lo4  = vmovl_u16(vget_low_u16(h));
            uint32x4_t hi4  = vmovl_high_u16(h);
            vst1q_u32(d + 2 * i, lo4);
            vst1q_u32(d + 2 * i + 4, hi4);
            acc_lo = vminq_u32(acc_lo, vminq_u32(lo4, hi4));
            acc_hi = vmaxq_u32(acc_hi, vmaxq_u32(lo4, hi4));
        }
        {
            uint32x2_t rlo = vmin_u32(vget_low_u32(acc_lo),
                                      vget_high_u32(acc_lo));
            uint32x2_t rhi = vmax_u32(vget_low_u32(acc_hi),
                                      vget_high_u32(acc_hi));
            rlo = vpmin_u32(rlo, rlo);
            rhi = vpmax_u32(rhi, rhi);
            lo = vget_lane_u32(rlo, 0);
            hi = vget_lane_u32(rhi, 0);
        }
    }
    for (; i < k; i++) {
        uint32_t a = p[i] & 0xFFFFu, b = p[i] >> 16;
        uint32_t mn = a < b ? a : b, mx = a < b ? b : a;
        d[2 * i]     = a;
        d[2 * i + 1] = b;
        lo = mn < lo ? mn : lo;
        hi = mx > hi ? mx : hi;
    }
    *lo_out = lo;
    *hi_out = hi;
}

#endif /* __aarch64__ */
#endif /* NV2A_AE16_NEON_H */
