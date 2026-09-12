#!/usr/bin/env python3
"""Compare linked display/IRQ code without treating relocations as code fixes.

For changed words, accept only a branch with the same named target+offset or
an address with the same named symbol+offset. Everything else stays unexpected.
This is a bounded audit, not a general semantic equivalence proof.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile

WANTED = ["vm_irq", "emu_hblankIrq", "emu_arm7Irq", "emu_vblankIrq", "dma_dmaTransfer",
          "dma_dma0Transfer", "dma_dma1Transfer", "dma_dma2Transfer", "dma_dma3Transfer", "dma_dmaSound",
          "emu_regDispCntStore", "displayModeChange.isra.0",
          "_ZN30GbaDisplayConfigurationService14SetupGbaScreenERK15DisplaySettings",
          "_ZN30GbaDisplayConfigurationService18SetupCenterAndMaskERK15DisplaySettings",
          "vm_enableNestedIrqs", "vm_disableNestedIrqs", "vm_yieldGbaIrqs",
          "fs_waitForCompletion", "sdc_loadRomBlockDirect"]


class Binary:
    def __init__(self, path):
        with path.open("rb") as stream:
            elf = ELFFile(stream)
            self.symbols = {s.name: s.entry for s in elf.get_section_by_name(".symtab").iter_symbols()
                            if s.name and not s.name.startswith(("$", "."))}
            self.sections = {i: (s["sh_addr"], s.data()) for i, s in enumerate(elf.iter_sections())
                             if s["sh_flags"] & 2 and s["sh_type"] != "SHT_NOBITS"}
            self.layout = {s.name: {"address": hex(s["sh_addr"]), "size": s["sh_size"]}
                           for s in elf.iter_sections() if s["sh_flags"] & 2}
            rodata = elf.get_section_by_name(".rodata")
            start = rodata["sh_addr"]
            end = start + rodata["sh_size"]
            boundaries = sorted({s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()
                                 if s.name.startswith("$d") and start <= s["st_value"] < end} | {end})
            self.constants = {left: rodata.data()[left-start:right-start]
                              for left, right in zip(boundaries, boundaries[1:])}

    def code(self, name):
        s = self.symbols[name]
        start = s["st_value"] & ~1
        size = s["st_size"]
        if not size:
            # GAS labels sometimes have no size. End at the next function,
            # excluding local labels and literal-pool mapping symbols.
            next_functions = [x["st_value"] & ~1 for x in self.symbols.values()
                              if x["st_shndx"] == s["st_shndx"] and
                              x["st_info"]["type"] == "STT_FUNC" and (x["st_value"] & ~1) > start]
            size = min(next_functions) - start
        base, section = self.sections[s["st_shndx"]]
        return start, section[start - base:start - base + size]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("stable", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    a, b = Binary(args.stable), Binary(args.candidate)

    def same_address(x, y):
        for name, sym in a.symbols.items():
            other = b.symbols.get(name)
            if not other or not isinstance(sym["st_shndx"], int):
                continue
            offset = x - sym["st_value"]
            if (offset == 0 or 0 <= offset < sym["st_size"]) and y == other["st_value"] + offset:
                return f"{name}+{offset}"
        # GCC switch tables may be anonymous. Accept only the complete, equal
        # read-only block delimited by ELF data mapping symbols, not a prefix.
        if x in a.constants and y in b.constants and a.constants[x] == b.constants[y]:
            return f"byte-identical anonymous .rodata block ({len(a.constants[x])} bytes)"
        return None

    def branch_target(pc, word):
        delta = word & 0xFFFFFF
        if delta & 0x800000:
            delta -= 0x1000000
        return (pc + 8 + delta * 4) & 0xFFFFFFFF

    rows = []
    for name in WANTED:
        if name not in a.symbols or name not in b.symbols:
            rows.append({"symbol": name, "classification": "missing; manual source audit required"})
            continue
        pa, ca = a.code(name)
        pb, cb = b.code(name)
        row = {"symbol": name, "stable_address": hex(pa), "candidate_address": hex(pb),
               "stable_size": len(ca), "candidate_size": len(cb),
               "stable_sha256": hashlib.sha256(ca).hexdigest(),
               "candidate_sha256": hashlib.sha256(cb).hexdigest()}
        changed = []
        if ca == cb:
            classification = "byte-identical"
        elif len(ca) != len(cb) or len(ca) % 4:
            classification = "unexpected difference"
        else:
            classification = "address-only relocation"
            for offset in range(0, len(ca), 4):
                wa, = struct.unpack_from("<I", ca, offset)
                wb, = struct.unpack_from("<I", cb, offset)
                if wa == wb:
                    continue
                reason = same_address(wa, wb)
                if wa & 0xFF000000 == wb & 0xFF000000 and wa & 0x0E000000 == 0x0A000000:
                    reason = same_address(branch_target(pa + offset, wa), branch_target(pb + offset, wb))
                changed.append({"offset": hex(offset), "stable_word": hex(wa), "candidate_word": hex(wb),
                                "same_symbol_target": reason})
                if not reason:
                    classification = "unexpected difference"
            row["changed_words"] = changed
        row["classification"] = classification
        rows.append(row)
    report = {"scope": "listed function extents, not every instruction in the ELF",
              "stable_sections": a.layout, "candidate_sections": b.layout, "functions": rows,
              "sdc_getRomBlock": "static inline; audit unchanged SdCache.h and linked sdc_loadRomBlockDirect instead"}
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for row in rows:
        print(row["symbol"], row["classification"])


if __name__ == "__main__":
    main()
