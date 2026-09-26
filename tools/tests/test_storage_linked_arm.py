#!/usr/bin/env python3
"""Run linked ARM9 diskio/FsIpc and ARM7 handlers with a synthetic IPC bridge.

Real CPU binaries on both sides; fake driver and immediate IPC delivery.
CP15 cache effects and physical ARM7/ARM9 concurrency are NOT emulated.
"""
import argparse
import json
import struct
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_PC, UC_ARM_REG_LR, UC_ARM_REG_SP, UC_ARM_REG_CPSR
from test_hicode_dispatch_elf import load_elf

p=argparse.ArgumentParser()
p.add_argument('arm9',type=Path)
p.add_argument('arm7',type=Path)
p.add_argument('--output',type=Path)
args=p.parse_args()
s9,sections9=load_elf(args.arm9)
s7,sections7=load_elf(args.arm7)
results=[]
regs=(UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3)
def machine(sections):
    cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    for base,size in [(0,0x8000),(0x02000000,0x400000),(0x03000000,0x10000),
        (0x037c0000,0x4000),(0x03800000,0x10000),(0x04000000,0x2000),
        (0x06800000,0x100000),(0xffff8000,0x8000)]:
        cpu.mem_map(base,size)
    for address,data in sections: cpu.mem_write(address,data)
    return cpu
def put(cpu,address,value): cpu.mem_write(address,struct.pack('<I',value))
def word(cpu,address): return struct.unpack('<I',cpu.mem_read(address,4))[0]
def call(cpu,address,values,stack):
    cpu.reg_write(UC_ARM_REG_CPSR,0xd3)
    cpu.reg_write(UC_ARM_REG_SP,stack)
    cpu.reg_write(UC_ARM_REG_LR,0x0300f000)
    for reg,value in zip(regs,values): cpu.reg_write(reg,value)
    cpu.emu_start(address,0x0300f000,count=400000)
    assert cpu.reg_read(UC_ARM_REG_PC)==0x0300f000, 'instruction budget exhausted'
    return cpu.reg_read(UC_ARM_REG_R0)

for device in (0,1):
    for write in (False,True):
        for failed in (False,True):
            for aligned in (False,True):
                arm9,arm7=machine(sections9),machine(sections7)
                backing=bytearray([0x5a]*4096)
                drivers=[]
                command=s9['_ZL11sIpcCommand']
                assert command%32==0 and 0x02000000<=command<0x02400000
                def driver(cpu,pc,size,_):
                    dldi=pc==0x0300e000
                    if not dldi and pc not in (s7['SDMMC_readSectors'],s7['SDMMC_writeSectors']): return
                    values=[cpu.reg_read(r) for r in regs]
                    sector,count,buffer=(values[0],values[1],values[2]) if dldi else (values[1],values[3],values[2])
                    drivers.append((sector,count))
                    if not failed:
                        if write: backing[sector*512:(sector+count)*512]=cpu.mem_read(buffer,count*512)
                        else: cpu.mem_write(buffer,bytes(backing[sector*512:(sector+count)*512]))
                    cpu.reg_write(UC_ARM_REG_R0,int(not failed) if dldi else int(failed))
                    cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
                arm7.hook_add(UC_HOOK_CODE,driver)
                put(arm7,s7['_DLDI_writeSectors_ptr' if write else '_DLDI_readSectors_ptr'],0x0300e000)
                def cp15(cpu,pc,size,_):
                    if size==4 and not cpu.reg_read(UC_ARM_REG_CPSR)&32:
                        ins=word(cpu,pc)
                        if ins&0x0f000010==0x0e000010 and (ins>>8)&15==15:
                            # Cache maintenance only; no architectural data is synthesized.
                            cpu.reg_write(UC_ARM_REG_PC,pc+4)
                arm9.hook_add(UC_HOOK_CODE,cp15)
                def ipc(cpu,access,address,size,value,_):
                    if address!=0x04000188: return
                    cmd_data=bytes(cpu.mem_read(command,64))
                    cmd,buffer,sector,count,sequence=struct.unpack_from('<5I',cmd_data)
                    assert cmd==(2 if write else 1)+(2 if device else 0)
                    assert sequence!=0
                    arm7.mem_write(command,cmd_data)
                    arm7.mem_write(buffer,bytes(cpu.mem_read(buffer,count*512)))
                    method=('DsiSd' if device else 'Dldi')+('WriteSectors' if write else 'ReadSectors')
                    entry=next(v for k,v in s7.items() if method in k and k.startswith('_ZNK12FsIpcService'))
                    call(arm7,entry,(0,command),0x0300d000)
                    result,completed=struct.unpack('<2I',arm7.mem_read(command+32,8))
                    assert result==(3 if failed else 2) and completed==sequence
                    cpu.mem_write(command+32,bytes(arm7.mem_read(command+32,32)))
                    if not write: cpu.mem_write(buffer,bytes(arm7.mem_read(buffer,count*512)))
                    sync=word(cpu,0x04000180)
                    put(cpu,0x04000180,(sync&~15)|(sequence&15))
                arm9.hook_add(UC_HOOK_MEM_WRITE,ipc)
                buffer=0x02030000+(0 if aligned else 1)
                arm9.mem_write(buffer,bytes([0x79]*1024))
                result=call(arm9,s9['disk_write' if write else 'disk_read'],(device,buffer,0,2),0x0300c000)
                assert result==(1 if failed else 0),(device,write,failed,aligned,result)
                assert drivers
                if write:
                    assert bytes(arm9.mem_read(buffer,1024))==bytes([0x79]*1024)
                    assert bytes(backing[:1024])==bytes([0x5a if failed else 0x79]*1024)
                else:
                    assert bytes(arm9.mem_read(buffer,1024))==bytes([0x79 if failed else 0x5a]*1024)
                results.append(dict(device=device,write=write,failure=failed,aligned=aligned,result='PASS'))
print(f'{len(results)} linked ARM7/ARM9 storage cases PASS; cache/concurrency/hardware NOT VERIFIED')
if args.output: args.output.write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
