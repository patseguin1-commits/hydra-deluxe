"""A MIDI reader built for Hydra's needs, replacing mido on the parse path.

Parsing dominates chart analysis -- 65.6% of profiled runtime, against 25.1%
for the path search -- and mido spends it on generality Hydra never uses.
Profiling a single chart showed 208k calls to ``read_byte``, 94k data-byte
validations and 96k abstract-base-class isinstance checks, all to produce
message objects whose fields Hydra reads once.

This reader does the same job by pulling the file into one bytes object and
walking it with an index: no per-byte reads, no validation, no ABCs, and no
message objects for events Hydra cannot act on.

Only what hysong actually consumes is produced:

    MidiFile   ticks_per_beat, tracks
    MidiTrack  name, and iteration over messages
    Message    type ('note_on'/'note_off'), note, velocity, time
    MetaMessage
               type, time, and per type: text / tempo / numerator+denominator

``time`` is a delta in ticks, as in mido. Events Hydra cannot match are not
emitted, and their delta is rolled into the next emitted event, so the
running ``elapsed_ticks`` total that hysong accumulates is unchanged.

Matching mido's shape matters as much as its values: hysong matches with
class patterns like ``MetaMessage(text=t)``, which succeed only when the
attribute is present. So ``text`` is set on exactly the meta types mido gives
it to, and the classes use __slots__ so an absent field raises AttributeError
and the pattern falls through -- the same way mido behaves.

"""

import struct


# mido splits the string-valued metas across two different attribute names,
# and the split is load-bearing: hysong matches MetaMessage(text=...) for
# disco-flip and dynamics markers, and a class pattern only fires when the
# attribute is present. Giving track_name a .text -- as an earlier version of
# this file did -- would offer every track title to those regexes, which mido
# never does. Verified against mido.midifiles.meta._META_SPECS.
_TEXT_METAS = {
    0x01: 'text',
    0x02: 'copyright',
    0x05: 'lyrics',
    0x06: 'marker',
    0x07: 'cue_marker',
}

# These carry .name instead. 0x08 is deliberately absent: mido has no spec
# for it, so it arrives as an unknown meta with neither attribute.
_NAME_METAS = {
    0x03: 'track_name',
    0x04: 'instrument_name',
    0x09: 'device_name',
}

# How many data bytes follow each channel status, by high nibble.
_CHANNEL_DATA_LEN = {
    0x80: 2, 0x90: 2, 0xA0: 2, 0xB0: 2, 0xC0: 1, 0xD0: 1, 0xE0: 2,
}


def _decode(payload):
    """Meta strings, decoded the way mido does it.

    mido reads every meta string as latin-1 (its _charset), which never
    raises and round-trips each byte, so a chart with non-UTF-8 bytes in a
    marker parses the same here as it does there.

    """
    return payload.decode('latin-1')


class Message:
    """A channel message. Only note on/off are produced."""

    __slots__ = ('type', 'note', 'velocity', 'time')

    def __init__(self, type, note, velocity, time):
        self.type = type
        self.note = note
        self.velocity = velocity
        self.time = time

    def __repr__(self):
        return (f"Message({self.type} note={self.note} "
                f"velocity={self.velocity} time={self.time})")


class MetaMessage:
    """A meta event. Fields are set only when the type carries them."""

    __slots__ = ('type', 'time', 'text', 'name', 'tempo',
                 'numerator', 'denominator')

    def __init__(self, type, time):
        self.type = type
        self.time = time

    def __repr__(self):
        return f"MetaMessage({self.type} time={self.time})"


class MidiTrack:
    __slots__ = ('name', 'messages')

    def __init__(self):
        self.name = ''
        self.messages = []

    def __iter__(self):
        return iter(self.messages)

    def __len__(self):
        return len(self.messages)

    def __getitem__(self, i):
        return self.messages[i]


class MidiFile:
    """Reads a standard MIDI file from a file object or raw bytes."""

    __slots__ = ('ticks_per_beat', 'tracks', 'format')

    def __init__(self, file=None, data=None):
        if data is None:
            if file is None:
                raise ValueError("MidiFile needs either file= or data=")
            data = file.read()
        if not isinstance(data, (bytes, bytearray, memoryview)):
            raise TypeError(f"unsupported MIDI source: {type(data).__name__}")

        self.ticks_per_beat = 0
        self.format = 0
        self.tracks = []
        self._parse(bytes(data))

    def _parse(self, data):
        size = len(data)
        if size < 14 or data[0:4] != b'MThd':
            raise ValueError("not a MIDI file: missing MThd header")

        header_len = struct.unpack_from('>I', data, 4)[0]
        self.format, ntracks, division = struct.unpack_from('>3h', data, 8)

        if division < 0:
            # SMPTE timing. Charts are all ticks-per-beat, and treating an
            # SMPTE division as one would silently misplace every note.
            raise ValueError("SMPTE time division is not supported")
        self.ticks_per_beat = division

        pos = 8 + header_len
        while pos < size:
            if data[pos:pos + 4] != b'MTrk':
                # Unknown chunk: the length field still tells us how to skip.
                if pos + 8 > size:
                    break
                pos += 8 + struct.unpack_from('>I', data, pos + 4)[0]
                continue

            chunk_len = struct.unpack_from('>I', data, pos + 4)[0]
            start = pos + 8
            end = min(start + chunk_len, size)
            self.tracks.append(self._parse_track(data, start, end))
            pos = start + chunk_len

    def _parse_track(self, data, pos, end):
        track = MidiTrack()
        append = track.messages.append

        # Ticks accumulated since the last emitted message. Events that are
        # skipped hand their delta to whatever comes next, so absolute time
        # is preserved for the consumer.
        pending = 0
        status = 0

        while pos < end:
            # Delta time: a variable-length quantity, 7 bits per byte.
            delta = 0
            while pos < end:
                b = data[pos]
                pos += 1
                delta = (delta << 7) | (b & 0x7F)
                if not (b & 0x80):
                    break
            pending += delta

            if pos >= end:
                break

            b = data[pos]
            if b & 0x80:
                status = b
                pos += 1
            elif not status:
                # Running status with nothing to run from: the track is
                # malformed past this point, so stop rather than guess.
                break

            if status == 0xFF:
                if pos >= end:
                    break
                meta_type = data[pos]
                pos += 1
                length, pos = self._read_varlen(data, pos, end)
                payload = data[pos:pos + length]
                pos += length

                msg = self._meta_message(meta_type, payload, pending)
                if msg is not None:
                    if meta_type == 0x03:
                        track.name = msg.name
                    append(msg)
                    pending = 0
                continue

            if status in (0xF0, 0xF7):
                length, pos = self._read_varlen(data, pos, end)
                pos += length
                continue

            high = status & 0xF0
            nbytes = _CHANNEL_DATA_LEN.get(high)
            if nbytes is None:
                # System-common byte we do not model; without a length there
                # is no safe way to resynchronise.
                break

            if pos + nbytes > end:
                break
            d1 = data[pos]
            d2 = data[pos + 1] if nbytes > 1 else 0
            pos += nbytes

            if high == 0x90 or high == 0x80:
                # clip=True in mido: data bytes are clamped, not rejected.
                note = d1 if d1 < 128 else 127
                velocity = d2 if d2 < 128 else 127
                append(Message(
                    'note_on' if high == 0x90 else 'note_off',
                    note, velocity, pending))
                pending = 0

        return track

    @staticmethod
    def _read_varlen(data, pos, end):
        value = 0
        while pos < end:
            b = data[pos]
            pos += 1
            value = (value << 7) | (b & 0x7F)
            if not (b & 0x80):
                break
        return value, pos

    @staticmethod
    def _meta_message(meta_type, payload, time):
        """Build the meta events hysong can act on; None for the rest."""
        if meta_type == 0x51 and len(payload) == 3:
            msg = MetaMessage('set_tempo', time)
            msg.tempo = (payload[0] << 16) | (payload[1] << 8) | payload[2]
            return msg

        if meta_type == 0x58 and len(payload) >= 2:
            msg = MetaMessage('time_signature', time)
            msg.numerator = payload[0]
            # Stored as a power of two, as in mido.
            msg.denominator = 2 ** payload[1]
            return msg

        type_name = _TEXT_METAS.get(meta_type)
        if type_name is not None:
            msg = MetaMessage(type_name, time)
            msg.text = _decode(payload)
            return msg

        type_name = _NAME_METAS.get(meta_type)
        if type_name is not None:
            msg = MetaMessage(type_name, time)
            msg.name = _decode(payload)
            return msg

        return None
