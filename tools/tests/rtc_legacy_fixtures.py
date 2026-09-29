"""Independent synthetic v1 RTC fixtures from the frozen pre-BCD-fix format.

The byte layout comes from f6e31b8's RtcPersistence::StateFile.  The old
time masks (0x1f, 0x3f, 0x3f) come from that same revision.  No production
serializer or migration code is imported here.
"""

from __future__ import annotations

from datetime import datetime
import hashlib
import struct

EPOCH = datetime(2000, 1, 1)
CYCLE_SECONDS = 36525 * 86400
V1_MAGIC = 0x54523347
V1_FORMAT = struct.Struct("<IHH6IhHHHI")
assert V1_FORMAT.size == 44
V2_SIZE = 200


def fnv1a(data: bytes) -> int:
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def bcd(value: int) -> int:
    assert 0 <= value <= 99
    return (value // 10 << 4) | value % 10


def from_bcd(value: int) -> int:
    return value - 6 * (value >> 4)


def old_seconds(value: datetime, *, time24h: bool = True) -> int:
    """Reproduce the old input conversion, including its lossy BCD masks."""
    day_start = value.replace(hour=0, minute=0, second=0, microsecond=0)
    days = (day_start - EPOCH).days
    hour = bcd(value.hour if time24h else value.hour % 12)
    if not time24h and value.hour >= 12:
        hour |= 0x80
    hours = from_bcd(hour & 0x1F)
    if not time24h and hour & 0x80:
        hours += 12
    minute = from_bcd(bcd(value.minute) & 0x3F)
    second = from_bcd(bcd(value.second) & 0x3F)
    return ((days * 24 + hours) * 60 + minute) * 60 + second


def correct_seconds(value: datetime) -> int:
    return int((value - EPOCH).total_seconds())


def legacy_record(
    identity: tuple[int, int, int], *, sequence: int, host_seconds: int,
    game_seconds: int, weekday_offset: int = -2,
    status: int = 0x40, interrupt: int = 0x1234,
) -> bytes:
    assert 0 <= host_seconds < CYCLE_SECONDS
    assert 0 <= game_seconds < CYCLE_SECONDS
    assert -6 <= weekday_offset <= 6
    assert status & ~0xEA == 0
    header = V1_FORMAT.pack(
        V1_MAGIC, 1, 32, *identity, sequence, host_seconds, game_seconds,
        weekday_offset, status, interrupt, 0, 0,
    )
    return header[:40] + struct.pack("<I", fnv1a(header[:40]))


def legacy_record_from_old_clock(
    identity: tuple[int, int, int], *, sequence: int,
    host: datetime, game: datetime, weekday_offset: int = -2,
    status: int = 0x40, interrupt: int = 0x1234,
) -> bytes:
    return legacy_record(
        identity, sequence=sequence, host_seconds=old_seconds(host),
        game_seconds=old_seconds(game), weekday_offset=weekday_offset,
        status=status, interrupt=interrupt,
    )


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def modern_record(
    identity: tuple[int, int, int], *, sequence: int, host_seconds: int,
    game_seconds: int, weekday_offset: int = -2, status: int = 0x40,
    interrupt: int = 0x1234, legacy: tuple[bytes | None, bytes | None, bytes | None]
    = (None, None, None), selected_role: int = 255, pending: bool = False,
) -> bytes:
    """Independent little-endian v2 contract serializer for test inputs."""
    assert len(legacy) == 3
    assert 0 <= host_seconds < CYCLE_SECONDS and 0 <= game_seconds < CYCLE_SECONDS
    data = bytearray(V2_SIZE)
    struct.pack_into("<IHH", data, 0, 0x32523347, 2, 188)
    struct.pack_into("<III", data, 8, *identity)
    struct.pack_into("<III", data, 20, sequence, host_seconds, game_seconds)
    struct.pack_into("<hHHH", data, 32, weekday_offset, status, interrupt, 0)
    presence = sum(1 << i for i, item in enumerate(legacy) if item is not None)
    policy = 1 if presence else 0
    if presence:
        assert 0 <= selected_role < 3 and legacy[selected_role] is not None
        selected_sequence = struct.unpack_from("<I", legacy[selected_role], 20)[0]
    else:
        assert selected_role == 255 and not pending
        selected_sequence = 0
    struct.pack_into("<HBBB", data, 40, policy, 0 if pending else 1,
                     presence, selected_role)
    struct.pack_into("<I", data, 48, selected_sequence)
    for role, record in enumerate(legacy):
        if record is not None:
            assert len(record) == 44
            struct.pack_into("<I", data, 52 + 4 * role, fnv1a(record))
            data[64 + 44 * role:108 + 44 * role] = record
    struct.pack_into("<I", data, 196, fnv1a(data[:196]))
    return bytes(data)


ID_A = (0x45455042, 32 * 1024 * 1024, 0x12345678)
ID_B = (0x45565841, 16 * 1024 * 1024, 0x87654321)
