"""Prints the disassembly of a function from the dsd project.

Usage: python -I tools/recomp/disfunc.py <project_root> <module> <hex addr> [...]
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import capstone  # noqa: E402
from dsdconfig import Project  # noqa: E402


def main():
    p = Project(sys.argv[1])
    m = p.modules[sys.argv[2]]
    for arg in sys.argv[3:]:
        f = p.function_containing(m.name, int(arg, 16))
        if f is None:
            print(f"{arg}: no function")
            continue
        md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB if f.thumb else capstone.CS_MODE_ARM)
        print(f"{f.name} ({'thumb' if f.thumb else 'arm'}, 0x{f.size:x} bytes)")
        o = f.addr - m.base
        for i in md.disasm(m.data[o:o + f.size], f.addr):
            reloc = m.relocs.get(i.address)
            note = f"  ; -> 0x{reloc.to:08x} {','.join(reloc.modules)}" if reloc else ""
            print(f"  {i.address:08x}: {i.bytes.hex():8s} {i.mnemonic} {i.op_str}{note}")


if __name__ == "__main__":
    main()
