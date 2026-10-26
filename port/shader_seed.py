#!/usr/bin/env python3
"""The engine's list of shaders, port/shader_seed.dat, from states files.

A states file (<save folder>/shaders/states-v1.dat; see
src/nv2a_gl/nv2a_shader_cache.h) holds what each program the game drew with
was translated from: "JVS1" vertex halves, "JPS1" programs and "JLB1" blend
recipes. This puts several together in the order given -- each record once,
the first time it comes -- which is the order the engine builds them in at a
first start, so the files of the parts of the game seen first go first.

    python3 port/shader_seed.py -o port/shader_seed.dat run1.dat run2.dat ...
    python3 port/shader_seed.py --info some-states.dat
"""
import argparse
import struct
import sys

PSH_LEN = 252          # sizeof(Nv2aPshState): another length is another engine's


def records(data):
    """(magic, key, record bytes) for each whole record; then the damaged length."""
    at, out = 0, []
    while at + 4 <= len(data):
        m = data[at:at + 4]
        if m == b'JLB1':
            if at + 12 > len(data):
                break
            out.append((m, data[at + 4:at + 12], data[at:at + 12]))
            at += 12
        elif m in (b'JVS1', b'JPS1'):
            if at + 16 > len(data):
                break
            n = struct.unpack_from('<I', data, at + 12)[0]
            if n > 8192 or at + 16 + n > len(data):
                break
            out.append((m, data[at + 4:at + 8], data[at:at + 16 + n]))
            at += 16 + n
        else:
            break
    return out, len(data) - at


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('files', nargs='+')
    ap.add_argument('-o', '--out')
    ap.add_argument('--info', action='store_true', help='only count what each file holds')
    a = ap.parse_args()
    seen = {b'JVS1': set(), b'JPS1': set(), b'JLB1': set()}
    halves = set()
    out = bytearray()
    for name in a.files:
        data = open(name, 'rb').read()
        recs, bad = records(data)
        new = {b'JVS1': 0, b'JPS1': 0, b'JLB1': 0}
        had = {b'JVS1': 0, b'JPS1': 0, b'JLB1': 0}
        for m, key, rec in recs:
            had[m] += 1
            if m == b'JPS1':
                if len(rec) - 16 != PSH_LEN:
                    continue
                vid = rec[8:12]
                if vid not in halves:      # its vertex half must come first
                    continue
            if key in seen[m]:
                continue
            seen[m].add(key)
            if m == b'JVS1':
                halves.add(key)
            new[m] += 1
            out += rec
        print('%s: %d programs (%d new), %d vertex halves (%d new), %d blend recipes (%d new)%s' % (
            name, had[b'JPS1'], new[b'JPS1'], had[b'JVS1'], new[b'JVS1'], had[b'JLB1'], new[b'JLB1'],
            ' -- %d damaged bytes at the end' % bad if bad else ''))
    print('together: %d programs, %d vertex halves, %d blend recipes, %d bytes' % (
        len(seen[b'JPS1']), len(seen[b'JVS1']), len(seen[b'JLB1']), len(out)))
    if a.out and not a.info:
        open(a.out, 'wb').write(out)
        print('written: %s' % a.out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
