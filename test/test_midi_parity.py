"""Parity tests for hymidi, the MIDI reader that replaced mido in hysong.

Swapping the parser is only safe if it yields the same message stream, so
these compare hymidi against mido directly on the real corpus rather than
against hand-written expectations.

mido stays a test dependency for exactly this reason: it is the oracle. It
is also still used by test_batchanalysis to *write* MIDI files.

Run with:
    cd docs && PYTHONPATH=.. python -m unittest discover -s ../test -p "test_midi_parity.py"

"""

import glob
import io
import os
import unittest

import mido

import hydra.hymidi as hymidi


CORPUS = os.path.join("..", "test", "input", "common")

# The metas hysong can match, split by which attribute carries the string.
TEXT_METAS = {'text', 'copyright', 'lyrics', 'marker', 'cue_marker'}
NAME_METAS = {'track_name', 'instrument_name', 'device_name'}


def event_view(tracks):
    """Absolute-tick view of everything hysong is able to act on.

    hymidi deliberately omits events hysong cannot match and rolls their
    delta into the next emitted event, so the comparison is on absolute
    time and content, not on message count.

    """
    view = []
    for track in tracks:
        tick = 0
        events = []
        for msg in track:
            tick += msg.time
            mtype = getattr(msg, 'type', None)
            if mtype == 'note_on':
                events.append((tick, 'note_on', msg.note, msg.velocity))
            elif mtype == 'note_off':
                events.append((tick, 'note_off', msg.note, msg.velocity))
            elif mtype == 'set_tempo':
                events.append((tick, 'set_tempo', msg.tempo))
            elif mtype == 'time_signature':
                events.append(
                    (tick, 'time_signature', msg.numerator, msg.denominator))
            elif mtype in TEXT_METAS:
                events.append((tick, mtype, 'text', msg.text))
            elif mtype in NAME_METAS:
                events.append((tick, mtype, 'name', msg.name))
        view.append(events)
    return view


def midi_files():
    return sorted(glob.glob(
        os.path.join(CORPUS, "**", "*.mid"), recursive=True))


class TestMidiParity(unittest.TestCase):

    def test_corpus_is_present(self):
        self.assertGreater(len(midi_files()), 0, f"no .mid files under {CORPUS}")

    def test_message_streams_match_mido(self):
        for path in midi_files():
            with self.subTest(chart=path):
                with open(path, 'rb') as f:
                    raw = f.read()

                expected = mido.MidiFile(file=io.BytesIO(raw), clip=True)
                observed = hymidi.MidiFile(data=raw)

                self.assertEqual(observed.ticks_per_beat,
                                 expected.ticks_per_beat)
                self.assertEqual(len(observed.tracks), len(expected.tracks))
                self.assertEqual([t.name for t in observed.tracks],
                                 [t.name for t in expected.tracks])
                self.assertEqual(event_view(observed.tracks),
                                 event_view(expected.tracks))


class TestMidiReaderEdgeCases(unittest.TestCase):
    """Shapes the corpus may not contain but a user's library might."""

    def _build(self, messages, ticks_per_beat=480):
        mid = mido.MidiFile(ticks_per_beat=ticks_per_beat)
        track = mido.MidiTrack()
        mid.tracks.append(track)
        for m in messages:
            track.append(m)
        buf = io.BytesIO()
        mid.save(file=buf)
        return buf.getvalue()

    def test_running_status_and_zero_velocity(self):
        # note_on with velocity 0 is how most charts spell note_off, and
        # consecutive same-status messages exercise running status.
        raw = self._build([
            mido.Message('note_on', note=96, velocity=100, time=0),
            mido.Message('note_on', note=96, velocity=0, time=120),
            mido.Message('note_on', note=97, velocity=90, time=0),
        ])
        self.assertEqual(event_view(hymidi.MidiFile(data=raw).tracks),
                         event_view(mido.MidiFile(file=io.BytesIO(raw)).tracks))

    def test_meta_and_sysex_are_skipped_without_losing_time(self):
        # A sysex between two notes must not shift the second note's tick.
        raw = self._build([
            mido.Message('note_on', note=96, velocity=100, time=0),
            mido.Message('sysex', data=[1, 2, 3], time=48),
            mido.MetaMessage('marker', text='mix 3 drums0d', time=48),
            mido.Message('note_off', note=96, velocity=0, time=48),
        ])
        self.assertEqual(event_view(hymidi.MidiFile(data=raw).tracks),
                         event_view(mido.MidiFile(file=io.BytesIO(raw)).tracks))

    def test_track_name_is_not_offered_as_text(self):
        # hysong matches MetaMessage(text=...) for disco/dynamics markers.
        # A track_name must not expose .text, or every track title would be
        # offered to those regexes.
        raw = self._build([
            mido.MetaMessage('track_name', name='PART DRUMS', time=0),
            mido.Message('note_on', note=96, velocity=100, time=0),
        ])
        track = hymidi.MidiFile(data=raw).tracks[0]
        self.assertEqual(track.name, 'PART DRUMS')

        meta = next(m for m in track if getattr(m, 'type', '') == 'track_name')
        self.assertEqual(meta.name, 'PART DRUMS')
        with self.assertRaises(AttributeError):
            meta.text

    def test_rejects_non_midi(self):
        with self.assertRaises(ValueError):
            hymidi.MidiFile(data=b'this is not a midi file at all')


if __name__ == '__main__':
    unittest.main()
