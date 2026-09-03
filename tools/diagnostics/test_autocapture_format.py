import json
import struct
import tempfile
import unittest
from pathlib import Path
from autocapture_format import SCHEMA, HEADER, RECORD, EVENT, COMPLETE_SIZE, read_auto, select_pair
from decode_g3diag import fnv1a, read_checkpoint
from compare_kh_runtime import inspect, compare

def fixture(*, sequence=1, stage_only=False, build='a'*40, total=2):
    h = dict.fromkeys(SCHEMA['header'], 0)
    h.update(magic=0x47443347, version=4, header_size=HEADER.size, record_size=RECORD.size,
             capacity=SCHEMA['runtime_capacity'], phase_capacity=SCHEMA['phase_capacity'],
             event_capacity=SCHEMA['event_capacity'], event_record_size=EVENT.size,
             complete_size=COMPLETE_SIZE, stack_size=SCHEMA['stack_size'], event_stack_size=SCHEMA['event_stack_size'],
             sequence=sequence, flags=int(stage_only), status=2 if stage_only else 3, total_samples=total,
             write_index=total % SCHEMA['runtime_capacity'], first_a_sample=0xFFFFFFFF, transition_sample=0xFFFFFFFF)
    h.update({f'build_id_{i}': w for i, w in enumerate(struct.unpack('<10I', build.encode()))})
    data = bytearray(HEADER.pack(*(h[k] for k in SCHEMA['header'])))
    if not stage_only:
        data.extend(bytes(COMPLETE_SIZE - HEADER.size))
    struct.pack_into('<I', data, SCHEMA['header'].index('checksum')*4, fnv1a(data))
    return data

class FormatTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.a = Path(self.tmp.name) / 'test.a'
        self.b = Path(self.tmp.name) / 'test.b'

    def test_geometry_and_checksum(self):
        self.assertEqual(HEADER.size, 256)
        self.assertEqual(RECORD.size, 372)
        self.assertEqual(COMPLETE_SIZE, 30528)
        data = fixture()
        c = read_auto(self.a, data)
        self.assertEqual(len(c.rows), 2)
        self.assertEqual(len(c.rows[0]), len(c.columns))
        for bad in (data[:-1], data+bytes(1), data[:200]):
            with self.assertRaises(ValueError): read_auto(self.a, bad)
        data[-1] ^= 1
        with self.assertRaisesRegex(ValueError, 'checksum'): read_auto(self.a, data)

    def test_stage_header_validates_checksum_with_nonzero_progress(self):
        data = fixture(stage_only=True, total=300)
        c = read_auto(self.a, data)
        self.assertEqual(c.metadata['total_samples'], 300)
        self.assertEqual(c.rows, [])
        data[64] ^= 1
        with self.assertRaisesRegex(ValueError, 'checksum'): read_auto(self.a, data)

    def test_latest_metadata_and_older_complete_are_both_retained(self):
        self.a.write_bytes(fixture(sequence=8))
        self.b.write_bytes(fixture(sequence=9, stage_only=True))
        newest, payload, errors = select_pair([self.a, self.b])
        self.assertEqual((newest.checkpoint_sequence, payload.checkpoint_sequence), (9, 8))
        self.assertEqual(errors, [])
        self.b.write_bytes(self.b.read_bytes()[:-1])
        newest, payload, errors = select_pair([self.b, self.a])
        self.assertEqual(newest.checkpoint_sequence, 8)
        self.assertEqual(len(errors), 1)

    def test_mixed_build_rejected(self):
        self.a.write_bytes(fixture(build='a'*40))
        self.b.write_bytes(fixture(build='b'*40))
        with self.assertRaisesRegex(ValueError, 'mixed build'): select_pair([self.a, self.b])

    def test_same_trace_is_not_falsely_called_a_polling_or_timer_stall(self):
        self.a.write_bytes(fixture(total=60))
        trace = inspect([self.a])
        r = compare(trace, trace)
        self.assertEqual(r['classification'], 'INSUFFICIENT_EVIDENCE')
        self.assertTrue(all(not item['polling_proven'] for item in trace['instruction_evidence']))

    def test_no_runtime_checkpoint(self):
        self.a.write_bytes(fixture(stage_only=True))
        trace = inspect([self.a])
        self.assertEqual(compare(trace, trace)['classification'], 'NO_RUNTIME_CHECKPOINT')

    def test_all_invalid_still_produces_failure_evidence(self):
        self.a.write_bytes(fixture()[:-1])
        trace = inspect([self.a, self.b])
        r = compare(trace, trace)
        self.assertEqual(r['classification'], 'FILESYSTEM_OR_DIAGNOSTIC_FAILURE')
        self.assertIn('no valid checkpoint', r['findings'][0]['evidence']['validation_error'])

if __name__ == '__main__':
    unittest.main()
