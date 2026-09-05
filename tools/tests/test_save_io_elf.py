#!/usr/bin/env python3
"""Execute linked production save byte/deferred code and ARM7 acknowledgments.

FatFs call outcomes, nested-IRQ boundaries and CP15 are controlled seams.
This does not emulate SD durability, simultaneous CPUs or power-button timing.
"""
import argparse
import json
import struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3
from unicorn.arm_const import UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7
from unicorn.arm_const import UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_CPSR
from test_hicode_dispatch_elf import load_elf

parser=argparse.ArgumentParser()
parser.add_argument('arm9',type=Path)
parser.add_argument('--arm7',type=Path)
parser.add_argument('--output',type=Path)
args=parser.parse_args()
symbols,sections=load_elf(args.arm9)
results=[]
REGS=(UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)
SAVED=(UC_ARM_REG_R4,UC_ARM_REG_R5,UC_ARM_REG_R6,UC_ARM_REG_R7,
       UC_ARM_REG_R8,UC_ARM_REG_R9,UC_ARM_REG_R10,UC_ARM_REG_R11)

def offset(path,type_name,member):
    with path.open('rb') as stream:
        dwarf=ELFFile(stream).get_dwarf_info()
        for cu in dwarf.iter_CUs():
            for die in cu.iter_DIEs():
                name=die.attributes.get('DW_AT_name')
                if not name or name.value.decode()!=type_name: continue
                if die.tag=='DW_TAG_typedef': die=die.get_DIE_from_attribute('DW_AT_type')
                for child in die.iter_children():
                    name=child.attributes.get('DW_AT_name')
                    if child.tag=='DW_TAG_member' and name and name.value.decode()==member:
                        return child.attributes['DW_AT_data_member_location'].value
    raise AssertionError((type_name,member,'missing DWARF layout'))

def put(cpu,address,value): cpu.mem_write(address,struct.pack('<I',value))
def passed(name): results.append({'case':name,'result':'PASS'})
def machine(image):
    cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    for base,size in ((0,0x8000),(0x02000000,0x1000000),(0x03000000,0x1000000),
                      (0x04000000,0x10000),(0x06800000,0x100000),(0xFFFF8000,0x8000),(0x07000000,0x1000)):
        cpu.mem_map(base,size)
    for address,data in image: cpu.mem_write(address,data)
    return cpu

def call(cpu,address,*values):
    cpu.reg_write(UC_ARM_REG_CPSR,0xD3)
    cpu.reg_write(UC_ARM_REG_SP,0x0300F000)
    cpu.reg_write(UC_ARM_REG_LR,0x07000000)
    for reg,value in zip(REGS,values): cpu.reg_write(reg,value)
    for i,reg in enumerate(SAVED): cpu.reg_write(reg,0xBEEF0000+i)
    cpu.emu_start(address,0x07000000,count=200000)
    assert cpu.reg_read(UC_ARM_REG_PC)==0x07000000,'instruction budget exhausted'
    assert cpu.reg_read(UC_ARM_REG_SP)==0x0300F000,'stack imbalance'
    assert [cpu.reg_read(r) for r in SAVED]==[0xBEEF0000+i for i in range(len(SAVED))],'callee-saved registers'
    return cpu.reg_read(UC_ARM_REG_R0)

file_size_offset=offset(args.arm9,'FIL','obj')+offset(args.arm9,'FFOBJID','objsize')
shared_size_offset=offset(args.arm9,'gba_save_shared_t','saveDataSize')
assert shared_size_offset==8 and offset(args.arm9,'gba_save_shared_t','saveState')==0

def fixture(failure='',nitro=False):
    cpu=machine(sections)
    disk=bytearray([0x35]*32768)
    cursor=0
    events=[]
    put(cpu,symbols['gSaveFile']+file_size_offset,len(disk))
    put(cpu,symbols['_ZN11Environment6_flagsE'],4 if nitro else 0)
    put(cpu,symbols['gGbaSaveShared']+shared_size_offset,32768)
    cpu.mem_write(symbols['gSaveData'],bytes([0x79])*32768)
    names={symbols[n]&~1:n for n in ('f_lseek','f_read','f_write','f_sync','vm_enableNestedIrqs','vm_disableNestedIrqs')}
    def hook(c,pc,size,_):
        nonlocal cursor
        name=names.get(pc)
        if name:
            values=[c.reg_read(r) for r in REGS]
            events.append(name)
            result=0
            if name=='f_lseek':
                if failure=='seek': result=1
                else: cursor=values[1]
            elif name in ('f_read','f_write'):
                count=values[2]
                if failure==('read' if name=='f_read' else 'write'): result=1; count=0
                if failure==('short-read' if name=='f_read' else 'short-write'): count//=2
                assert cursor+count<=len(disk),'out-of-range FatFs request'
                if name=='f_read': c.mem_write(values[1],bytes(disk[cursor:cursor+count]))
                else: disk[cursor:cursor+count]=c.mem_read(values[1],count)
                cursor+=count
                put(c,values[3],count)
            elif name=='f_sync' and failure=='sync': result=1
            c.reg_write(UC_ARM_REG_R0,result)
            c.reg_write(UC_ARM_REG_PC,c.reg_read(UC_ARM_REG_LR))
            return
        if size==4 and not c.reg_read(UC_ARM_REG_CPSR)&32:
            op,=struct.unpack('<I',c.mem_read(pc,4))
            if op&0x0F000010==0x0E000010 and (op>>8)&15==15:
                assert not op&(1<<20),'unexpected CP15 read'
                c.reg_write(UC_ARM_REG_PC,pc+4)
    cpu.hook_add(UC_HOOK_CODE,hook)
    return cpu,disk,events

for failure in ('','seek','read','short-read'):
    for address in (0,32767,32768,0xFFFFFFFF):
        cpu,disk,events=fixture(failure)
        value=call(cpu,symbols['sav_readSaveByteFromFile'],address)
        assert value==(0x35 if not failure and address<32768 else 255)
        assert events[0]=='vm_enableNestedIrqs' and events[-1]=='vm_disableNestedIrqs'
        if address>=32768: assert 'f_lseek' not in events
        passed(f'byte-read-{failure or "normal"}-{address}')

for failure in ('','seek','write','short-write'):
    for address in (0,32767,32768):
        cpu,disk,events=fixture(failure)
        call(cpu,symbols['sav_writeSaveByteToFile'],address,0x79)
        succeeds=not failure and address<32768
        assert disk==(bytearray([0x35])*address+bytearray([0x79])+bytearray([0x35])*(32767-address) if succeeds else bytearray([0x35])*32768)
        assert cpu.mem_read(symbols['gGbaSaveShared'],1)==bytes([0 if succeeds else 4])
        if not succeeds:
            # A successful sync must not erase evidence of the missing byte.
            call(cpu,symbols['sav_flushSaveFile'])
            assert cpu.mem_read(symbols['gGbaSaveShared'],1)==b'\4'
        passed(f'byte-write-{failure or "normal"}-{address}')

for failure in ('','seek','write','short-write','sync'):
    cpu,disk,events=fixture(failure)
    call(cpu,symbols['sav_initializeFileWriteScheduler'])
    before=bytes(cpu.mem_read(symbols['emu_vblankIrqSkipSaveCheckInstruction'],4))
    cpu.mem_write(symbols['gGbaSaveShared'],b'\3')
    call(cpu,symbols['sav_writeSaveToFile'])
    assert cpu.mem_read(symbols['gGbaSaveShared'],1)==bytes([4 if failure else 0])
    assert bytes(cpu.mem_read(symbols['emu_vblankIrqSkipSaveCheckInstruction'],4))==before
    if failure=='seek': assert 'f_write' not in events and 'f_sync' not in events
    if failure in ('write','short-write'): assert 'f_sync' not in events
    if not failure: assert disk==bytearray([0x79])*32768
    passed(f'deferred-{failure or "normal"}')

cpu,disk,events=fixture('sync')
call(cpu,symbols['sav_flushSaveFile'])
assert cpu.mem_read(symbols['gGbaSaveShared'],1)==b'\4'
passed('standalone-sync-error')
for address in (0,131071,131072,0xFFFFFFFF):
    cpu,disk,events=fixture(nitro=True)
    call(cpu,symbols['sav_writeSaveByteToFile'],address,0x79)
    value=call(cpu,symbols['sav_readSaveByteFromFile'],address)
    assert value==(0x79 if address<131072 else 255)
    assert not any(e.startswith('f_') for e in events)
    passed(f'nitro-byte-bounds-{address}')

if args.arm7:
    arm7_symbols,arm7_sections=load_elf(args.arm7)
    def symbol(fragment):
        matches=[v for k,v in arm7_symbols.items() if 'GbaSaveIpcService' in k and fragment in k]
        assert len(matches)==1,(fragment,matches)
        return matches[0]
    shared_offset=offset(args.arm7,'GbaSaveIpcService','_saveShared')
    cpu=machine(arm7_sections)
    obj,shared=0x02300000,0x02301000
    put(cpu,obj+shared_offset,shared)
    put(cpu,shared+8,32768)
    for state,expected in ((0,0),(1,1),(2,1),(3,1),(4,2)):
        cpu.mem_write(shared,bytes([state]))
        assert call(cpu,symbol('FlushSaveIfDirty'),obj)==expected
        passed(f'arm7-flush-state-{state}')
    for size in (0,32768):
        put(cpu,shared+8,size); cpu.mem_write(shared,b'\4')
        for _ in range(120): call(cpu,symbol('6Update'),obj)
        assert cpu.mem_read(shared,1)==b'\4'
        assert call(cpu,symbol('FlushSaveIfDirty'),obj)==2
        passed(f'arm7-error-latched-{size}')

report={'tests':results,'scope':__doc__}
if args.output: args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(f'PASS: {len(results)} linked save/ARM7 cases; no physical SD/concurrency claim')
