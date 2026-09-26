#!/usr/bin/env python3
"""Independent assertions for old v1 bytes and the lossy clock transform."""

from datetime import datetime
import struct
import unittest

from rtc_legacy_fixtures import (
    ID_A, V1_FORMAT, correct_seconds, fnv1a, legacy_record,
    legacy_record_from_old_clock, old_seconds,
)


class LegacyFixtureTests(unittest.TestCase):
    def test_exact_old_layout_and_checksum(self):
        data = legacy_record(ID_A, sequence=7, host_seconds=1234,
                             game_seconds=5678, weekday_offset=-2,
                             status=0x40, interrupt=0xABCD)
        self.assertEqual(len(data), 44)
        self.assertEqual(data[:8], bytes.fromhex("47 33 52 54 01 00 20 00"))
        self.assertEqual(V1_FORMAT.unpack(data),
                         (0x54523347, 1, 32, *ID_A, 7, 1234, 5678,
                          -2, 0x40, 0xABCD, 0, fnv1a(data[:40])))
        self.assertEqual(struct.unpack_from("<I", data, 40)[0], fnv1a(data[:40]))

    def test_old_masks_collapse_distinct_clock_times(self):
        pairs = [
            (datetime(2024, 2, 28, 0, 0, 0), datetime(2024, 2, 28, 20, 40, 40)),
            (datetime(2024, 2, 28, 2, 5, 5), datetime(2024, 2, 28, 22, 45, 45)),
        ]
        for early, late in pairs:
            with self.subTest(early=early, late=late):
                self.assertNotEqual(correct_seconds(early), correct_seconds(late))
                self.assertEqual(old_seconds(early), old_seconds(late))

    def test_both_host_and_game_fields_can_be_lossy(self):
        host = datetime(2024, 2, 28, 22, 45, 45)
        game = datetime(2024, 2, 28, 21, 39, 39)
        data = legacy_record_from_old_clock(ID_A, sequence=19, host=host, game=game)
        fields = V1_FORMAT.unpack(data)
        self.assertEqual(fields[7], old_seconds(host))
        self.assertEqual(fields[8], old_seconds(game))
        self.assertNotEqual(fields[7], correct_seconds(host))
        self.assertNotEqual(fields[8], correct_seconds(game))
        self.assertNotEqual(fields[7] - fields[8],
                            correct_seconds(host) - correct_seconds(game),
                            "direct subtraction of old and corrected timestamps is invalid")

    def test_v1_version_does_not_prove_pre_fix_provenance(self):
        clock = datetime(2024, 2, 28, 22, 45, 45)
        old = legacy_record_from_old_clock(ID_A, sequence=1, host=clock, game=clock)
        fixed = legacy_record(ID_A, sequence=1,
                              host_seconds=correct_seconds(clock),
                              game_seconds=correct_seconds(clock))
        self.assertEqual(old[4:8], fixed[4:8])
        self.assertNotEqual(old, fixed)


if __name__ == "__main__":
    unittest.main()
