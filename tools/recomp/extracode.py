"""Modules for code the game copies to a fixed address at run time (see extra_code.txt).

Function starts come from the declared entries and offset tables, then from a recursive walk of the
code (BL targets) and `push {..., lr}` prologues. Each function extends to the next start.
"""

import struct

from dsdconfig import Function, Module

EXTRA_OVERLAY_ID_BASE = 1000  # treated as an overlay: the dispatcher checks the copy is resident


def _sext(v, bits):
    m = 1 << (bits - 1)
    return (v & (m - 1)) - (v & m)


def _walk(m, start, entries, seen):
    """Recursive descent over ARM code from start, collecting BL/BLX targets inside the module."""
    work = [start]
    while work:
        a = work.pop()
        while m.contains(a) and a not in seen:
            seen.add(a)
            w = m.u32(a)
            cond, top = w >> 28, (w >> 25) & 7
            if top == 5:
                target = a + 8 + (_sext(w & 0xFFFFFF, 24) << 2)
                if cond == 0xF:  # BLX imm: Thumb target, not expected in this code
                    a += 4
                    continue
                if w & (1 << 24):  # BL
                    if m.contains(target):
                        entries.add(target)
                else:
                    if m.contains(target):
                        work.append(target)
                    if cond == 0xE:
                        break
            # Returns: bx lr, pop {..., pc}, ldm ...{pc}, mov pc, lr.
            if cond == 0xE and ((w & 0x0FFFFFFF) == 0x012FFF1E or (w & 0x0E108000) == 0x08108000
                                or (w & 0x0FFFFFFF) == 0x01A0F00E):
                break
            a += 4


def load_extra_modules(project, path):
    modules = []
    with open(path) as f:
        for line in f:
            fields = line.split("#", 1)[0].split()
            if not fields:
                continue
            name, src_mod, src, size, load = fields[0], fields[1], int(fields[2], 16), int(fields[3], 16), int(fields[4], 16)
            sm = project.modules[src_mod]
            o = src - sm.base
            m = Module(name, load, sm.data[o:o + size], overlay_id=EXTRA_OVERLAY_ID_BASE + len(modules), whole=True)
            entries = set()
            for opt in fields[5:]:
                key, val = opt.split("=", 1)
                if key == "entry":
                    entries.add(int(val, 16))
                elif key == "table":
                    taddr, count = val.split(":")
                    taddr, count = int(taddr, 16), int(count)
                    for i in range(count):
                        entries.add(taddr + struct.unpack_from("<I", m.data, taddr - load + 4 * i)[0])
            if load == src:  # same address as the source: dsd's functions there are entries too
                entries |= {f.addr for f in sm.functions.values() if load <= f.addr < load + size and not f.thumb}
            for a in range(load, load + size - 3, 4):
                if (m.u32(a) & 0xFFFF4000) == 0xE92D4000:  # push {..., lr}
                    entries.add(a)
            seen = set()
            done = set()
            while entries - done:
                for e in sorted(entries - done):
                    done.add(e)
                    _walk(m, e, entries, seen)
            starts = sorted(e for e in entries if m.contains(e))
            for i, a in enumerate(starts):
                end = starts[i + 1] if i + 1 < len(starts) else m.end
                m.functions[a] = Function(m, f"func_{name}_{a:08x}", a, end - a, False, False)
            modules.append(m)
    return modules
