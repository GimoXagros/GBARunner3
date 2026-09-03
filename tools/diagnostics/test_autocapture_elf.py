#!/usr/bin/env python3
"""Execute the linked M ARM code with controlled MMIO/CP15 and a mock FatFs.

This tests real generated records, no-input persistence, fault journaling,
register preservation, and C ABI boundaries. It does not emulate storage latency
or physical cache coherence. Requires unicorn==2.1.4 and pyelftools==0.32.
"""
import argparse
import json
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *
from autocapture_format import read_auto, COMPLETE_SIZE, SCHEMA

REGS = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
        UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
        UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11,
        UC_ARM_REG_R12, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC]

class Machine:
    def __init__(self, path):
        self.u = u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        for addr, size in [(0, 0x8000), (0x02000000, 0x400000), (0x03000000, 0x10000),
                           (0x04000000, 0x10000), (0x06800000, 0x100000), (0xFFFF8000, 0x8000)]:
            u.mem_map(addr, size)
        with path.open('rb') as stream:
            elf = ELFFile(stream)
            self.s = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
            for sec in elf.iter_sections():
                if sec['sh_flags'] & 2 and sec['sh_type'] != 'SHT_NOBITS' and sec['sh_size']:
                    u.mem_write(sec['sh_addr'], sec.data())
        self.files, self.handles, self.calls, self.cp = {}, {}, {}, {}
        self.fail_once = False
        self.fail_all = False
        self.boundary = None
        self.event_stub = None
        self.stopped = False
        self.alignment_checks = 0
        self.cached_op = {}
        self.fs = {self.s[name]: name for name in ('f_open', 'f_write', 'f_sync', 'f_close')}
        u.mem_write(0x04000130, b'\xff\x03')
        u.mem_write(0x03009000, b'/test.a\0/test.b\0')
        u.hook_add(UC_HOOK_CODE, self.hook)

    def word(self, addr): return struct.unpack('<I', self.u.mem_read(addr, 4))[0]
    def put(self, name, value): self.u.mem_write(self.s[name], struct.pack('<I', value))
    def ret(self, value=0):
        self.u.reg_write(UC_ARM_REG_R0, value)
        self.u.reg_write(UC_ARM_REG_PC, self.u.reg_read(UC_ARM_REG_LR))
    def hook(self, u, pc, size, _):
        if pc in [self.s.get(n) for n in ('diag_recordSdLoadAligned', 'diag_recordDmaStartAligned', 'diag_setEnvironmentAligned', 'diag_recordConfigAligned', 'diag_initializeMetadata', 'diag_writeReadyFiles')]:
            assert u.reg_read(UC_ARM_REG_SP) % 8 == 0
            self.alignment_checks += 1
        if pc == self.boundary or pc == 0x0300F000:
            self.stopped = True
            u.emu_stop()
            return
        if pc == self.s['diag_recordLowTarget'] and self.event_stub is not None:
            assert u.reg_read(UC_ARM_REG_SP) % 8 == 0
            self.event_stub.append([u.reg_read(reg) for reg in REGS[:4]])
            self.ret()
            return
        name = self.fs.get(pc)
        if name:
            sp = u.reg_read(UC_ARM_REG_SP)
            assert sp % 8 == 0, (name, hex(sp))
            self.alignment_checks += 1
            self.calls[name] = self.calls.get(name, 0) + 1
            r = [u.reg_read(reg) for reg in REGS[:4]]
            if self.fail_all:
                self.ret(1)
                return
            if name == 'f_open':
                path = bytes(u.mem_read(r[1], 64)).split(b'\0')[0].decode()
                self.handles[r[0]] = path
                self.files[path] = bytearray()
            elif name == 'f_write':
                count = r[2]
                if self.fail_once and count > 256:
                    count -= 1
                    self.fail_once = False
                self.files[self.handles[r[0]]].extend(u.mem_read(r[1], count))
                u.mem_write(r[3], struct.pack('<I', count))
            self.ret()
            return
        if size == 4:
            op = self.cached_op.get(pc)
            if op is None:
                op = self.word(pc)
                self.cached_op[pc] = op
            if op & 0x0F000010 == 0x0E000010 and (op >> 8) & 15 == 15:
                assert op & (1 << 20), f'M diagnostic unexpectedly writes CP15: {op:08X}'
                rd, crn, crm, op2 = (op >> 12) & 15, (op >> 16) & 15, op & 15, (op >> 5) & 7
                vals = {(6, 4, 0): 0x09ED3017, (9, 0, 1): 0x80000001, (1, 0, 0): 0x0005707D}
                key = (crn, crm, op2)
                assert key in vals, key
                self.cp[str(key)] = self.cp.get(str(key), 0) + 1
                u.reg_write(REGS[rd], vals[key])
                u.reg_write(UC_ARM_REG_PC, pc + 4)

    def call(self, name, *args, boot=False):
        u = self.u
        u.reg_write(UC_ARM_REG_CPSR, 0xD2)
        u.reg_write(UC_ARM_REG_SPSR, 0x60000030)
        u.reg_write(UC_ARM_REG_SP, 0x03007FFC if boot else self.s['diag_stackEnd'] - 24)
        for reg, value in zip(REGS, args): u.reg_write(reg, value)
        u.reg_write(UC_ARM_REG_LR, 0x0300F000)
        self.stopped = False
        u.emu_start(self.s[name], 0xFFFFFFFF, count=3000000)
        assert self.stopped, (name, hex(u.reg_read(UC_ARM_REG_PC)))

    def initialize(self):
        before = bytes(self.u.mem_read(0x20, 0x40))
        self.call('diag_initialize', 0x03009000, 0x03009008, 0x4A433842, 0x02000000, boot=True)
        assert bytes(self.u.mem_read(0x20, 0x40)) == before, 'boot metadata damaged the VM vector/dispatcher'
        self.put('vm_irqSavedLR', 0x03000104)
        self.put('memu_inst_addr', 0x03000200)
        self.put('gHicodeState', 0x09ED3000)

def test_runtime(elf):
    m = Machine(elf)
    m.initialize()
    assert all(read_auto(Path(k), v).metadata['stage_names'] == ['READY'] for k, v in m.files.items())
    m.call('diag_sampleVBlank')
    first = read_auto(Path('a'), m.files['/test.a'])
    assert first.metadata['header_only'] and first.metadata['total_samples'] == 1
    assert first.metadata['stages'] & 7 == 7
    m.call('diag_sampleVBlank')
    c = read_auto(Path('b'), m.files['/test.b'])
    assert len(m.files['/test.b']) == COMPLETE_SIZE and len(c.rows) == 2
    assert c.metadata['first_a_sample'] == 0xFFFFFFFF and c.metadata['a_count'] == 0
    assert c.metadata['stack_flags'] == 0
    m.call('diag_recordLowTarget', 8, 0x03000200, 0x0157539E, 0x60000030)
    m.call('diag_recordLowTarget', 8, 0x03000200, 0x0157539E, 0x60000030)
    row = dict(zip(c.columns, c.rows[-1]))
    assert row['native_spsr'] == 0x60000030 and row['mpu_region4'] == 0x09ED3017
    assert row['ds_ie'] == 0
    # Edge marker is independent of capture arming.
    m.u.mem_write(0x04000130, b'\xfe\x03')
    m.call('diag_sampleVBlank')
    m.u.mem_write(0x04000130, b'\xff\x03')
    for _ in range(57): m.call('diag_sampleVBlank')
    c = read_auto(Path('a'), m.files['/test.a'])
    assert c.metadata['a_count'] == 1 and c.metadata['first_a_sample'] == 2
    assert c.metadata['event_count'] == 1 and c.events[0]['target'] == 0x0157539E
    assert c.metadata['last_success_sample'] == 2 and c.metadata['stages'] & 16
    assert c.metadata['stack_flags'] == 0
    good = bytes(m.files['/test.a'])
    m.fail_once = True
    for _ in range(60): m.call('diag_sampleVBlank')
    failed = read_auto(Path('b'), m.files['/test.b'])
    assert failed.metadata['header_only'] and failed.metadata['status'] == 4
    assert failed.metadata['file_result'] == 1 and failed.metadata['fs_failures'] == 1
    assert bytes(m.files['/test.a']) == good, 'failure overwrote the other good checkpoint'
    durable = {k: bytes(v) for k, v in m.files.items()}
    m.fail_all = True
    for _ in range(60): m.call('diag_sampleVBlank')
    assert all(bytes(m.files[k]) == v for k, v in durable.items())
    return dict(result='PASS', complete_size=COMPLETE_SIZE, first_full_sample=2,
                no_input_capture=True, short_write_preserves_other_slot=True,
                total_write_failure_preserves_last_durable_files=True,
                fs_call_alignment_checks=m.alignment_checks, stack_high_water=c.metadata['stack_used'], cp15_reads=m.cp)

def test_trampolines(elf):
    results = []
    specs = [('diag_thumbTarget', 'diag_thumbTargetReturn', 1, 0xD1),
             ('diag_armBxTarget', 'cfdiag_armBxTargetReturn', 2, 0xD1),
             ('diag_irqReturnTarget', 'diag_irqReturnTargetReturn', 4, 0xD1),
             ('diag_prefetchEntry', 'diag_prefetchEntryReturn', 8, 0xD7)]
    for name, endpoint, kind, cpsr in specs:
        for target in (0x0157539E, 0x01200000, 0x02000000):
            m = Machine(elf)
            for s, size in [('diag_stack', SCHEMA['stack_size']), ('diag_eventStack', SCHEMA['event_stack_size'])]:
                assert m.s[s] % 8 == 0 and m.s[s+'End'] - m.s[s] == size
            u = m.u
            u.reg_write(UC_ARM_REG_CPSR, cpsr)
            u.reg_write(UC_ARM_REG_SPSR, 0x60000030)
            initial = [0x12340000+i for i in range(13)]
            initial[8], initial[10], initial[11] = target, 0x60000030, 0x03001004
            for reg, value in zip(REGS, initial): u.reg_write(reg, value)
            old_sp = 0x17 if kind == 8 else 0x03007FFC  # unmapped ABT SP and 4-mod-8 FIQ SP
            old_lr = target + 4 if kind == 8 else 0x0300E008
            u.reg_write(UC_ARM_REG_SP, old_sp)
            u.reg_write(UC_ARM_REG_LR, old_lr)
            m.put('memu_inst_addr', 0x03000200)
            m.put('vm_undefinedSpsr', 0x60000030)
            m.put('vm_undefinedInstructionAddr', 0x03003004)
            m.event_stub = []
            m.boundary = m.s[endpoint]
            u.emu_start(m.s[name], 0xFFFFFFFF, count=500)
            assert m.stopped
            expected = list(initial)
            if kind == 2: expected[9] = (target - 0x08000000) & 0xFFFFFFFF
            assert [u.reg_read(r) for r in REGS[:13]] == expected, (name, target)
            assert u.reg_read(UC_ARM_REG_SP) == old_sp
            expected_lr = (target - 0x08000000) & 0xFFFFFFFF if kind == 1 else (target if kind == 8 else old_lr)
            assert u.reg_read(UC_ARM_REG_LR) == expected_lr
            assert bool(m.event_stub) == (target == 0x0157539E)
            if m.event_stub:
                event = m.event_stub[0]
                assert event[0] == kind and event[2] == target and event[3] == 0x60000030
            assert m.word(m.s['diag_prefetchScratch']) == 0xA93D57AC
            results.append(dict(path=name, target=hex(target), result='PASS', incoming_sp=hex(old_sp)))
    m = Machine(elf)
    if 'diag_recordSdLoadAligned' in m.s:
        u = m.u
        u.reg_write(UC_ARM_REG_CPSR, 0xD1)
        u.reg_write(UC_ARM_REG_SP, 0x03007FFC)
        u.reg_write(UC_ARM_REG_LR, 0x0300F000)
        u.reg_write(UC_ARM_REG_R4, 0x12345678)
        u.emu_start(m.s['diag_recordSdLoad'], 0xFFFFFFFF, count=100)
        assert m.stopped and m.alignment_checks == 1
        assert u.reg_read(UC_ARM_REG_SP) == 0x03007FFC and u.reg_read(UC_ARM_REG_R4) == 0x12345678
        results.append(dict(path='diag_recordSdLoad', result='PASS', incoming_sp='0x03007ffc'))
    for name in ('diag_recordConfig', 'diag_setEnvironment', 'diag_recordDmaStart'):
        m = Machine(elf)
        u = m.u
        u.reg_write(UC_ARM_REG_CPSR, 0xDF)
        u.reg_write(UC_ARM_REG_SP, 0x03007FFC)
        u.reg_write(UC_ARM_REG_LR, 0x0300F000)
        u.reg_write(UC_ARM_REG_R0, 0x03009000 if name == 'diag_recordConfig' else 1)
        u.reg_write(UC_ARM_REG_R1, 0)
        u.reg_write(UC_ARM_REG_R4, 0x12345678)
        u.mem_write(0x03007FFC, struct.pack('<I', 0x31415926))
        u.emu_start(m.s[name], 0xFFFFFFFF, count=30000)
        assert m.stopped and m.alignment_checks >= 1
        assert u.reg_read(UC_ARM_REG_SP) == 0x03007FFC and u.reg_read(UC_ARM_REG_R4) == 0x12345678
        if name == 'diag_setEnvironment':
            haddr = next(v for k, v in m.s.items() if k.endswith('_1hE'))
            assert m.word(haddr + SCHEMA['header'].index('clock_control') * 4) == 0x31415926
        results.append(dict(path=name, result='PASS', incoming_sp='0x03007ffc'))
    return results

def main():
    p = argparse.ArgumentParser()
    p.add_argument('elf', type=Path)
    p.add_argument('--output', type=Path)
    a = p.parse_args()
    r = dict(runtime=test_runtime(a.elf), trampolines=test_trampolines(a.elf),
             scope='actual linked ARM code; mock FatFs/MMIO/CP15; physical latency, MPU permissions and cache coherence are NOT VERIFIED')
    text = json.dumps(r, indent=2) + '\n'
    if a.output: a.output.write_text(text, encoding='utf-8')
    print(text)

if __name__ == '__main__':
    main()
