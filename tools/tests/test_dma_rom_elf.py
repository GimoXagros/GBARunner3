#!/usr/bin/env python3
"""Linked DMA fast-path logical 4 KiB boundaries and noncontiguous backing.

Synthetic ROM bytes only. Counts instructions, not physical ARM9 cycles.
Does not model SD/cache coherency, IRQ timing, or hardware DMA.
"""
import argparse
import json
import struct
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_PC, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_CPSR
from test_hicode_dispatch_elf import load_elf

p=argparse.ArgumentParser()
p.add_argument('elf',type=Path)
p.add_argument('--output',type=Path)
args=p.parse_args()
symbols,sections=load_elf(args.elf)
results=[]
for width in (2,4):
    for distance in (2,4,16,32):
        if distance%width: continue
        for mirror in (0x08000000,0x0a000000,0x0c000000):
            for length in (distance+width,distance+64,4096+distance+32):
                cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
                for base,size in [(0,0x8000),(0x02000000,0x400000),(0x03000000,0x10000),
                    (0x04000000,0x2000),(0x06800000,0x100000),(0xffff8000,0x8000)]:
                    cpu.mem_map(base,size)
                for address,data in sections: cpu.mem_write(address,data)
                cache=symbols['sdc_cache']
                table=symbols['sdc_romBlockToCacheBlock']
                logical=0x200000
                data=bytes((i*37+i//4096*19)&255 for i in range(3*4096))
                for block,slot in enumerate((7,1,23)):
                    cpu.mem_write(cache+slot*4096,data[block*4096:(block+1)*4096])
                    cpu.mem_write(table+(logical//4096+block)*4,struct.pack('<I',cache+slot*4096))
                # Guest EWRAM is mirrored into ARM9 main RAM; use a disjoint
                # buffer and verify guards after the actual assembly copier.
                destination=0x02010000
                translated=0x02010000
                cpu.mem_write(translated-4,b'HEAD'+b'\xa5'*length+b'TAIL')
                cpu.reg_write(UC_ARM_REG_CPSR,0xd3)
                cpu.reg_write(UC_ARM_REG_SP,0x0300f000)
                cpu.mem_write(0x0300f000,struct.pack('<I',1))
                cpu.reg_write(UC_ARM_REG_LR,0x0300e000)
                values=(mirror+logical+4096-distance,destination,length,1)
                for reg,value in zip((UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3),values): cpu.reg_write(reg,value)
                count=[0]
                def hook(uc,pc,size,_):
                    count[0]+=1
                    assert pc!=symbols['sdc_loadRomBlockDirect'], 'unexpected backing miss'
                    assert pc!=symbols['dma_immTransferSafe16' if width==2 else 'dma_immTransferSafe32'], 'did not exercise fast path'
                cpu.hook_add(UC_HOOK_CODE,hook)
                cpu.emu_start(symbols['dma_immTransfer16' if width==2 else 'dma_immTransfer32'],0x0300e000,count=100000)
                assert cpu.reg_read(UC_ARM_REG_PC)==0x0300e000
                expected=data[4096-distance:4096-distance+length]
                actual=bytes(cpu.mem_read(translated,length))
                assert actual==expected,(width,distance,mirror,length,actual[:16].hex(),expected[:16].hex())
                assert bytes(cpu.mem_read(translated-4,4))==b'HEAD'
                assert bytes(cpu.mem_read(translated+length,4))==b'TAIL'
                last=int.from_bytes(expected[-width:],'little')
                if width==2: last|=last<<16
                assert struct.unpack('<I',cpu.mem_read(symbols['dma_transferRegister'],4))[0]==last
                results.append(dict(width=width,distance=distance,mirror=hex(mirror),bytes=length,
                    instructions=count[0],result='PASS'))
print(f'{len(results)} linked DMA boundary/mirror/noncontiguous cases PASS; hardware NOT RUN')
if args.output: args.output.write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
