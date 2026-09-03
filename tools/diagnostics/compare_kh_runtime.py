#!/usr/bin/env python3
"""Compare durable observations by input phase, never by exact frame lockstep.

Classifications describe evidence, not proven subsystem defects. In particular,
one stationary last-emulation marker does not identify the executing instruction.
"""
import argparse
import collections
import json
from pathlib import Path
from autocapture_format import UNKNOWN, select_pair

CATEGORIES = ('NO_RUNTIME_CHECKPOINT', 'LIVE_CPU_DISPLAY_BLANK', 'GUEST_POLLING_LOOP',
              'IRQ_WAIT_OR_MASK_MISMATCH', 'DMA_COMPLETION_STALL', 'TIMER_STALL',
              'HICODE_OR_CACHE_MAPPING_DIVERGENCE', 'SAVE_ACCESS_DIVERGENCE',
              'FILESYSTEM_OR_DIAGNOSTIC_FAILURE', 'INSUFFICIENT_EVIDENCE')

def values(rows, name):
    return sorted({r[name] for r in rows if name in r})

def delta(rows, name):
    return (rows[-1][name] - rows[0][name]) & 0xFFFFFFFF if rows and name in rows[0] else None

def summarize(rows):
    pcs = collections.Counter(r['irq_pc'] for r in rows)
    fields = ['hicode_block', 'hicode_block_mask', 'mpu_region4', 'icache_lockdown', 'cp15_control',
              'dispcnt', 'ds_dispcnt', 'ds_master_brightness', 'ds_capture_control', 'ds_vram_abcd', 'ds_vram_efg',
              'ie', 'if', 'ime', 'cpsr', 'native_spsr', 'ds_ie', 'ds_if', 'ds_ime', 'hw_irq_mask',
              'forced_irq_mask', 'nested_irq_level', 'vm_spsr_irq', 'vm_lr_irq', 'vm_sp_irq',
              'sd_old_block', 'sd_new_block', 'sd_cache_block']
    result = dict(samples=len(rows), sample_span=[rows[0]['sample'], rows[-1]['sample']] if rows else [],
                  unique_irq_pc=len(pcs), repeated_irq_pc=pcs.most_common(8),
                  last_emulation_markers=values(rows, 'emulated_pc'),
                  registers={k: values(rows, k) for k in fields},
                  counter_deltas={k: delta(rows, k) for k in ['dma_starts', 'sram_reads', 'sram_writes', 'sd_load_count']},
                  timer_unique_counters={f'timer{i}': len({r[f'timer{i}'] & 0xFFFF for r in rows}) for i in range(4)},
                  dma={f'dma{i}': {k: values(rows, f'dma{i}_{k}') for k in ('control', 'count', 'current_source', 'current_destination')} for i in range(4)})
    result['forced_blank'] = bool(rows) and all(r['dispcnt'] & 0x80 for r in rows)
    result['ds_display_disabled'] = bool(rows) and all(r['ds_dispcnt'] & 0x30000 == 0 for r in rows)
    return result

def instruction_evidence(row, elf=None, rom=None, marker=False):
    pc = row['emulated_pc'] if marker else row['irq_pc']
    state = None if marker else row.get('native_spsr')
    evidence = dict(address=pc, role='last_emulation_marker' if marker else 'sampled_native_irq_return_pc',
                    arm_thumb='unknown' if state is None else ('thumb' if state & 32 else 'arm'),
                    wait_address=None, polling_proven=False)
    data = None
    if not marker and row.get('source_instruction_valid'):
        data = row['source_instruction'].to_bytes(4, 'little')[pc & 3:]
        evidence['source'] = 'captured executable word'
    if data is None and elf:
        from elftools.elf.elffile import ELFFile
        with elf.open('rb') as stream:
            obj = ELFFile(stream)
            for sec in obj.iter_sections():
                # No guess at dynamic guest relocation/hicode cache aliases.
                if sec['sh_flags'] & 4 and sec['sh_type'] != 'SHT_NOBITS' and sec['sh_addr'] <= pc < sec['sh_addr'] + sec['sh_size']:
                    data = sec.data()[pc-sec['sh_addr']:pc-sec['sh_addr']+4]
                    evidence['source'] = 'exact ELF executable section'
                    break
    if data is None and rom and 0x08000000 <= pc < 0x0E000000:
        offset = (pc - 0x08000000) % 0x02000000
        with rom.open('rb') as stream:
            stream.seek(offset)
            data = stream.read(4)
        evidence['source'] = 'explicit local GBA ROM mapping'
    if data and state is not None:
        try:
            import capstone
            dis = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB if state & 32 else capstone.CS_MODE_ARM)
            ops = list(dis.disasm(data, pc, count=1))
            if ops:
                evidence['instruction'] = f'{ops[0].mnemonic} {ops[0].op_str}'
                evidence['branch_instruction'] = ops[0].mnemonic.startswith('b')
        except ImportError:
            evidence['disassembly_unavailable'] = 'install capstone==5.0.7'
    evidence['limitation'] = 'A single sample has no general-register snapshot or executed backedge trace; MMIO wait and polling are not established.'
    return evidence

def inspect(paths, elf=None, rom=None):
    try:
        newest, payload, rejected = select_pair(paths)
    except ValueError as error:
        return dict(metadata=dict(status=4, validation_error=str(error), build_id=None),
                    newest_file=None, rejected_files=[str(error)], payload_file=None,
                    phases={}, events=[], instruction_evidence=[])
    metadata = getattr(newest, 'metadata', dict(version=newest.header[1], status=newest.header[11], sequence=newest.checkpoint_sequence,
                                               transition_sample=newest.header[15], build_id=None))
    result = dict(metadata=metadata, newest_file=str(newest.path), rejected_files=rejected,
                  payload_file=str(payload.path) if payload else None, phases={}, events=[], instruction_evidence=[])
    if not payload:
        return result
    rows = [dict(zip(payload.columns, r)) for r in payload.rows]
    anchors = [dict(zip(payload.columns, r)) for r in getattr(payload, 'phase_rows', [])]
    all_rows = {r['sample']: r for r in anchors + rows}
    transition = metadata.get('transition_sample', UNKNOWN)
    first_a = metadata.get('first_a_sample', transition)
    groups = {'before_first_A': [], 'between_A_inputs': [], 'after_last_A': []}
    for _, row in sorted(all_rows.items()):
        key = 'before_first_A' if first_a == UNKNOWN or row['sample'] < first_a else ('after_last_A' if row['sample'] >= transition else 'between_A_inputs')
        groups[key].append(row)
    result['phases'] = {key: summarize(group) for key, group in groups.items() if group}
    result['anchors'] = [{k: r[k] for k in ('sample', 'irq_pc', 'dispcnt', 'ds_dispcnt', 'anchor_reason', 'transition_sample') if k in r} for r in anchors]
    result['events'] = getattr(payload, 'events', [])
    for pc, _ in collections.Counter(r['irq_pc'] for r in rows).most_common(3):
        result['instruction_evidence'].append(instruction_evidence(next(r for r in rows if r['irq_pc'] == pc), elf, rom))
    for pc, _ in collections.Counter(r['emulated_pc'] for r in rows).most_common(2):
        result['instruction_evidence'].append(instruction_evidence(next(r for r in rows if r['emulated_pc'] == pc), elf, rom, True))
    return result

def compare(hardware, lab):
    hm, lm = hardware['metadata'], lab['metadata']
    findings = []
    def add(category, evidence):
        findings.append(dict(classification=category, evidence=evidence, certainty='OBSERVED BUT NOT ROOT CAUSE'))
    if hm.get('status') == 4 or hm.get('fs_failures', 0) or hm.get('stack_flags', 0):
        add('FILESYSTEM_OR_DIAGNOSTIC_FAILURE', {k: hm.get(k) for k in ('status', 'file_result', 'fs_failures', 'stack_flags', 'validation_error')})
    if not hardware['payload_file']:
        add('NO_RUNTIME_CHECKPOINT', dict(durable_stages=hm.get('stage_names'), rejected=hardware['rejected_files']))
    identity_match = bool(hm.get('build_id')) and hm.get('build_id') == lm.get('build_id')
    differences = {}
    for phase in hardware['phases'].keys() & lab['phases'].keys():
        h, l = hardware['phases'][phase], lab['phases'][phase]
        diff = {k: {'hardware': v, 'lab': l['registers'][k]} for k, v in h['registers'].items() if v != l['registers'][k]}
        differences[phase] = diff
        if identity_match and h['samples'] >= 8 and h['unique_irq_pc'] >= 3 and h['counter_deltas']['dma_starts'] and max(h['timer_unique_counters'].values()) > 1 and (h['forced_blank'] or h['ds_display_disabled']) and not (l['forced_blank'] or l['ds_display_disabled']):
            add('LIVE_CPU_DISPLAY_BLANK', dict(phase=phase, pc_count=h['unique_irq_pc'], dma_delta=h['counter_deltas']['dma_starts'], timers=h['timer_unique_counters'], display=diff))
        # Only classify cache mapping divergence with a captured first target event
        # and native mapping evidence. Different UI phases alone are insufficient.
        mapping = {k: diff[k] for k in ('hicode_block', 'mpu_region4', 'icache_lockdown') if k in diff and diff[k]['hardware'] and diff[k]['lab']}
        if identity_match and phase == 'after_last_A' and hardware['events'] and mapping and not lab['events']:
            add('HICODE_OR_CACHE_MAPPING_DIVERGENCE', dict(phase=phase, first_event=hardware['events'][0], mapping=mapping,
                                                         caveat='Observed mapping/control-flow divergence; hardware coherence defect is not yet proven.'))
    if not findings:
        add('INSUFFICIENT_EVIDENCE', dict(reason='No conservative classification threshold met. Stationary PC/marker or differing counters alone do not prove a wait or subsystem stall.', matching_build=identity_match))
    return dict(classification=findings[0]['classification'], findings=findings, matching_build=identity_match,
                environment_differences={k: dict(hardware=hm.get(k), lab=lm.get(k)) for k in ('build_id', 'rom_header_hash', 'save_size', 'mount_device', 'dsi_mode', 'clock_control', 'config_data_hash', 'a_count') if hm.get(k) != lm.get(k)},
                phase_register_differences=differences,
                unresolved_categories=['GUEST_POLLING_LOOP', 'IRQ_WAIT_OR_MASK_MISMATCH', 'DMA_COMPLETION_STALL', 'TIMER_STALL', 'SAVE_ACCESS_DIVERGENCE'],
                limitations=['Phase labels are based on observed A edges, not visual UI recognition; compare matching user steps.',
                             'IRQ samples cannot prove uninterrupted CPU progress or IRQ non-entry.',
                             'Hardware timer/DMA stalls require a decoded wait condition and trigger semantics.',
                             'A total storage failure cannot report its final RAM stage. The last durable stage is a lower bound.',
                             'Same-build old/new sessions are not uniquely tagged; keep each run in its own directory.'],
                hardware=hardware, lab=lab)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--hardware', nargs='+', required=True, type=Path)
    p.add_argument('--lab', nargs='+', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--elf', type=Path, help='exact M ELF, optional; no guessed guest relocation')
    p.add_argument('--rom', type=Path, help='local ROM only; never uploaded or copied')
    a = p.parse_args()
    report = compare(inspect(a.hardware, a.elf, a.rom), inspect(a.lab, a.elf, a.rom))
    a.output.with_suffix('.json').write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    lines = ['# Runtime phase comparison', '', report['classification'], '', 'OBSERVED BUT NOT ROOT CAUSE', '']
    for finding in report['findings']:
        lines += [f"- {finding['classification']}: `{json.dumps(finding['evidence'], ensure_ascii=False)}`"]
    lines += ['', '## Limits', ''] + ['- ' + item for item in report['limitations']]
    lines += ['', 'Full register/counter evidence, rejected files and first events are in the adjacent JSON.']
    a.output.with_suffix('.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(report['classification'])

if __name__ == '__main__':
    main()
