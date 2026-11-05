# regen — guest-code perf passes

These scripts are **skeammer's work**, from his jsrf-nx tree (`regen/`),
used and adapted here with his direct permission. Credit: **skeammer**.
Each vendored `.py` carries a credit header saying so, and what (if
anything) was adapted. His original docstrings are intact in each file
and are the authoritative description of what the pass does and why.

The passes rewrite the generated C in `port/recomp/gen/` in place (they
are idempotent; undo with git). They need the companion header support
in `recomp_types.h` — the `recomp_leaf_ld_*`/`recomp_fp_*` helpers,
`recomp_x87_in`, the no-op `RECOMP_PREEMPT()` anchor definition, the
`RECOMP_ICALL_FAIL_SYNC` marker, and `RECOMP_BLOCK_COPY_ENTER/DONE` —
without it, `jsrf_reg_locals.py --apply` refuses to run.

## Run order

Run in exactly this order, each with `--apply`, over the gen dir:

1. `python3 tools/regen/jsrf_preempt.py port/recomp/gen --entry-only --apply`
   — inserts the `RECOMP_PREEMPT();` entry anchor reg_locals keys on.
   `--entry-only` only: this tree's runtime has no GIL and already has
   `RECOMP_BACK_EDGE` at loop back edges, so the back-edge mode is not
   used. Without `--apply` it only counts.
2. `python3 tools/regen/jsrf_reg_locals.py port/recomp/gen --apply`
   — x86 registers into C locals. Adapted (vocabulary only): SAFE knows
   this translator's `MEMW8/16/32/64/F/D`, `RECOMP_BACK_EDGE`,
   `RECOMP_FP_PC`, `recomp_cvt_si32`, `recomp_rcpss`, `recomp_rsqrtss`,
   `RC_ROT`, `expm1`; WRAP also matches `RECOMP_ICALL_SAFE_CC` /
   `RECOMP_ICALL_SAFE_AT_CC`. Functions using setjmp/SEH are refused by
   design and stay refused.
3. `python3 tools/regen/jsrf_inline_leaves.py port/recomp/gen --apply`
   — inline small leaf functions at their direct call sites.
4. `python3 tools/regen/jsrf_copy_loops.py port/recomp/gen --apply`
   — memcpy front door for the MMX block-copy loop in `sub_001993A0`.
   Adapted: an alternate `BODY_OURS` literal pattern for this tree's
   spelling of the loop. **Must run before simd_locals** (the loop's mm
   registers must still be in global form when the pattern is matched).
5. `python3 tools/regen/jsrf_fp_locals.py port/recomp/gen --apply`
   — x87 stack into C locals.
6. `python3 tools/regen/jsrf_simd_locals.py port/recomp/gen --apply`
   — SSE/MMX registers and the x87 compare state into C locals.

Without `--apply`, each script reports what it would do (counts and
refusal reasons) and changes nothing.

## Tests

`test_jsrf_reg_locals.py`, `test_jsrf_inline_leaves.py` and
`test_jsrf_fp_locals.py` are skeammer's unit tests for the passes (the
last also covers simd_locals). Run them from this directory:

```
python3 test_jsrf_reg_locals.py
python3 test_jsrf_inline_leaves.py
python3 test_jsrf_fp_locals.py
```
