#!/usr/bin/env python3
"""Structural regression guard for the dedicated VBlank diagnostic stack.

This checks the assembly call boundary, not runtime or hardware behavior.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class DiagnosticStackTest(unittest.TestCase):
    def test_aligned_c_call_and_balanced_guest_registers(self):
        source = (ROOT / "code/core/arm9/source/Emulator/VBlankIrq.s").read_text()
        hook = source.split("ldr sp,= diag_stackEnd", 1)[1].split(
            "mov r13, #0x04000000", 1
        )[0]
        self.assertEqual(re.findall(r"push\s+\{([^}]+)\}", hook), ["r0-r3,r12,lr"])
        self.assertEqual(re.findall(r"pop\s+\{([^}]+)\}", hook), ["r0-r3,r12,lr"])
        self.assertEqual((6 * 4) % 8, 0)
        self.assertIn("bl diag_sampleVBlank", hook)
        self.assertIn("bl cfdiag_sampleVBlank", hook)
        stack = (ROOT / "code/core/arm9/source/Diagnostics/ControlFlowDiagnosticsAsm.s").read_text()
        self.assertIn(".balign 8\n.global diag_stack", stack)
        self.assertIn(".space 2048", stack)


if __name__ == "__main__":
    unittest.main()
