"""Strict G3DGv4 reader. Geometry is shared with the generated C++ header."""
import json
import struct
from dataclasses import dataclass
from pathlib import Path

SCHEMA = json.loads(Path(__file__).with_name('autocapture_schema.json').read_text())
HEADER = struct.Struct('<' + 'I' * len(SCHEMA['header']))
RECORD = struct.Struct('<17I21H4H1H4I3I4I' + 'IIHHII' * 4 + 'I' * len(SCHEMA['extra']))
EVENT = struct.Struct('<' + 'I' * len(SCHEMA['event']))
COMPLETE_SIZE = HEADER.size + RECORD.size * (SCHEMA['runtime_capacity'] + SCHEMA['phase_capacity']) + EVENT.size * SCHEMA['event_capacity']
UNKNOWN = 0xFFFFFFFF

@dataclass(frozen=True)
class AutoCheckpoint:
    path: Path
    header: tuple
    rows: list
    columns: list
    metadata: dict
    phase_rows: list
    events: list

    @property
    def checkpoint_sequence(self):
        return self.metadata['sequence']

def read_auto(path, data):
    from decode_g3diag import fnv1a, CORE_COLUMNS, DISPLAY_V3_COLUMNS, IRQ_COLUMNS, TAIL_COLUMNS, HARDWARE_COLUMNS, dma_columns
    if len(data) < HEADER.size:
        raise ValueError(f'{path}: truncated v4 header')
    header = HEADER.unpack_from(data)
    h = dict(zip(SCHEMA['header'], header))
    expected = dict(magic=0x47443347, version=4, header_size=HEADER.size,
                    record_size=RECORD.size, capacity=SCHEMA['runtime_capacity'],
                    phase_capacity=SCHEMA['phase_capacity'], event_capacity=SCHEMA['event_capacity'],
                    event_record_size=EVENT.size, complete_size=COMPLETE_SIZE,
                    stack_size=SCHEMA['stack_size'], event_stack_size=SCHEMA['event_stack_size'])
    if any(h[k] != v for k, v in expected.items()):
        raise ValueError(f'{path}: v4 geometry mismatch')
    if h['write_index'] >= h['capacity'] or h['phase_write_index'] >= h['phase_capacity'] or h['event_count'] > h['event_capacity']:
        raise ValueError(f'{path}: invalid ring/event index')
    if h['flags'] & ~1 or h['status'] not in (1, 2, 3, 4):
        raise ValueError(f'{path}: unsupported v4 flags/status')
    size = HEADER.size if h['flags'] & 1 else COMPLETE_SIZE
    if len(data) != size:
        raise ValueError(f'{path}: incomplete checkpoint ({len(data)} != {size})')
    checked = bytearray(data)
    struct.pack_into('<I', checked, SCHEMA['header'].index('checksum') * 4, 0)
    if fnv1a(checked) != h['checksum']:
        raise ValueError(f'{path}: checksum mismatch')
    build = struct.pack('<10I', *(h[f'build_id_{i}'] for i in range(10))).decode('ascii', errors='replace')
    if len(build) != 40 or any(c not in '0123456789abcdef' for c in build):
        raise ValueError(f'{path}: invalid compile-time build ID')
    h['build_id'] = build
    h['stage_names'] = [k for k, v in SCHEMA['stages'].items() if h['stages'] & v]
    h['header_only'] = bool(h['flags'] & 1)
    columns = CORE_COLUMNS + DISPLAY_V3_COLUMNS + IRQ_COLUMNS + TAIL_COLUMNS + HARDWARE_COLUMNS + dma_columns() + SCHEMA['extra']
    def ring(offset, capacity, index, total):
        oldest = index if total >= capacity else 0
        return [RECORD.unpack_from(data, offset + ((oldest + i) % capacity) * RECORD.size)
                for i in range(min(total, capacity))]
    rows, phases, events = [], [], []
    if not h['header_only']:
        rows = ring(HEADER.size, h['capacity'], h['write_index'], h['total_samples'])
        phases = ring(HEADER.size + h['capacity'] * RECORD.size, h['phase_capacity'], h['phase_write_index'], h['phase_total'])
        offset = HEADER.size + (h['capacity'] + h['phase_capacity']) * RECORD.size
        events = [dict(zip(SCHEMA['event'], EVENT.unpack_from(data, offset + i * EVENT.size))) for i in range(h['event_count'])]
    return AutoCheckpoint(path, header, rows, columns, h, phases, events)

def select_pair(paths):
    """Return newest durable metadata and newest complete payload, with rejected files.

    A same-build restarted run cannot be distinguished after a failed READY write;
    callers must use isolated directories and preserve one physical test per pair.
    """
    from decode_g3diag import read_checkpoint
    valid, errors = [], []
    for path in paths:
        try:
            valid.append(read_checkpoint(Path(path)))
        except (ValueError, OSError) as error:
            errors.append(str(error))
    if not valid:
        raise ValueError('no valid checkpoint: ' + '; '.join(errors))
    identities = {(c.header[1], c.header[7], c.header[8], getattr(c, 'metadata', {}).get('build_id')) for c in valid}
    if len(identities) != 1:
        raise ValueError('mixed build/ROM/format identity; do not combine these files')
    for key in ('rom_header_hash', 'save_size', 'dsi_mode', 'mount_device', 'config_data_hash'):
        values = {c.metadata[key] for c in valid if hasattr(c, 'metadata') and c.metadata['env_valid']}
        if len(values) > 1:
            raise ValueError(f'mixed environment identity ({key})')
    newest = max(valid, key=lambda c: c.checkpoint_sequence)
    complete = [c for c in valid if c.rows]
    payload = max(complete, key=lambda c: c.checkpoint_sequence) if complete else None
    return newest, payload, errors
