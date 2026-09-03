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

    def test_m_sizes_and_separate_event_stack(self):
        import json
        schema = json.loads((ROOT / 'tools/diagnostics/autocapture_schema.json').read_text())
        source = (ROOT / 'code/core/arm9/source/Diagnostics/ControlFlowDiagnosticsAsm.s').read_text()
        self.assertIn(f".space {schema['stack_size']}", source)
        self.assertIn(f".space {schema['event_stack_size']}", source)
        event = (ROOT / 'code/core/arm9/source/Diagnostics/AutoCaptureDiagnosticsAsm.s').read_text()
        call = event.split('ldr sp,= diag_eventStackEnd')[1].split('bl diag_recordLowTarget')[0]
        self.assertIn('push {r0,r1}', call)
        self.assertNotIn('push {r0-r3,lr}', event)
        self.assertIn('str sp, diag_prefetchSavedSp', event)
        cpp = (ROOT / 'code/core/arm9/source/Diagnostics/AutoCaptureDiagnostics.cpp').read_text()
        self.assertIn('diag_prefetchScratch[0] == Canary', cpp)
        self.assertIn('diag_eventStack[0] == Canary', cpp)
        self.assertNotRegex(event, r'(?m)^\s*mcr\s')


if __name__ == "__main__":
    unittest.main()
