"""Checkpoints, recordings, and replay through the Python bridge (SPEC-03 5.3, 6.1, H-14b, H-17a,
H-19, H-19a; section 9 cases 29, 29a and 29b; box 13 note 7.2, unit 9).

`Context.checkpoint`, `restore`, `state_hash` and `entry_scope` on libraries that `cint build
--lib` wrote, the typed view of a checkpoint (`cint.read_checkpoint`), `cint.Recorder` with
`record=`, and `cint.replay` on both CPU backends: "cpu", the module's own library, and "ref",
cpu-sir-interp through the interpreter's library, which the bridge builds from this tree's
`interp/`. The tests skip as `test_calls.py` does when no `cint` of this tree is found.
"""
import array
import ctypes
import gc
import hashlib
import os
import shutil
import struct
import subprocess
import tempfile
import unittest

import cint
from cint import _build, _record, _replay, _state
from tests.test_calls import PROGRAMS, _tree_problem, load

LEDGER = os.path.join("state", "ledger.ci")
# The C types the mirrors of cint._state stand for (rt/cint_state.h, rt/cint_rt.h).
C_NAMES = {_state.TValue: "cint_tvalue", _state.Site: "cint_site", _state.Sink: "cint_sink",
           _state.CodeModule: "cint_code_module", _state.CheckpointInfo: "cint_checkpoint_info",
           _state.CheckpointGlobal: "cint_checkpoint_global", _state.CheckpointBuffer: "cint_checkpoint_buffer",
           _state.RecordingInfo: "cint_recording_info", _state.RecordingEvent: "cint_recording_event",
           _state.ReplayConfig: "cint_replay_config", _state.ReplayReport: "cint_replay_report",
           _state.ObserverEntry: "cint_observer_entry", _state.Observer: "cint_observer"}


def setUpModule():
    try:
        exe = _build.find_cint()
    except cint.BuildError as missing:
        raise unittest.SkipTest(str(missing))
    problem = _tree_problem(exe)
    if problem is not None:
        raise unittest.SkipTest(problem)


def ledger() -> cint.Module:
    return load(LEDGER)


def i64s(*values) -> array.array:
    return array.array("q", values)


def events(module: cint.Module, data: bytes) -> list:
    """The events of a recording, as the runtime's reader gives them."""
    api = module._state_api()
    info = _state.RecordingInfo()
    assert api.recording_decode(data, len(data), ctypes.byref(info)) == 0
    out, e = [], _state.RecordingEvent()
    while api.recording_next(ctypes.byref(info), ctypes.byref(e)) == 0:
        copy = _state.RecordingEvent()
        ctypes.pointer(copy)[0] = e
        out.append(copy)
    return out


class Layout(unittest.TestCase):
    """The ctypes mirrors of cint._state have the sizes and field offsets of rt/cint_state.h,
    as the toolchain's C compiler lays them out."""

    def test_layout(self):
        self.assertIsNone(_state.layout_problem())
        tool = _build.Toolchain(_build.find_cint())
        env, flags, msvc = _build._compile_env(tool)
        lines = ["#include <stddef.h>", "#include <stdio.h>", '#include "cint_state.h"', "int main(void) {"]
        for cls, name in C_NAMES.items():
            lines.append('    printf("%s %%zu", sizeof(%s));' % (name, name))
            for field in cls._fields_:
                lines.append('    printf(" %%zu", offsetof(%s, %s));' % (name, field[0]))
            lines.append('    printf("\\n");')
        lines += ["    return 0;", "}", ""]
        with tempfile.TemporaryDirectory() as d:
            src, exe = os.path.join(d, "probe.c"), os.path.join(d, "probe.exe" if msvc else "probe")
            with open(src, "w", encoding="ascii", newline="\n") as f:
                f.write("\n".join(lines))
            include = tool.values["include"]
            if msvc:
                command = [tool.values["cc"], "/nologo", *flags, "/I" + include, src, "/Fe" + exe, "/Fo" + d + os.sep]
            else:
                command = [tool.values["cc"], *flags, "-I", include, src, "-o", exe]
            built = subprocess.run(command, cwd=d, env=env, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            out = subprocess.run([exe], capture_output=True, text=True, check=True).stdout.splitlines()
        for (cls, name), line in zip(C_NAMES.items(), out):
            words = line.split()
            self.assertEqual(words[0], name)
            want = [ctypes.sizeof(cls)] + [getattr(cls, f[0]).offset for f in cls._fields_]
            self.assertEqual([int(w) for w in words[1:]], want, name)


class Checkpoints(unittest.TestCase):
    """SPEC-03 H-19 and H-19a: a checkpoint between entries, its typed view, and its restore."""

    def setUp(self):
        self.mod = ledger()
        self.ctx = self.mod.context()

    def test_round_trip(self):
        c = self.ctx
        self.assertEqual(c.deposit(5), 105)
        h = c.state_hash()
        self.assertEqual(len(h), 32)
        saved = c.checkpoint()
        self.assertEqual(c.state_hash(), h)                  # a checkpoint changes nothing
        self.assertEqual(cint.read_checkpoint(saved).state_hash, h)
        self.assertEqual(c.deposit(7), 112)
        self.assertNotEqual(c.state_hash(), h)
        c.restore(saved)
        self.assertEqual(c.state_hash(), h)
        self.assertEqual(c.balance(), 105)
        c.restore(bytearray(saved))
        c.restore(memoryview(saved))
        with self.assertRaises(TypeError):
            c.restore(saved.hex())
        other = self.mod.context()
        other.restore(saved)                                 # into another context of the same revision
        self.assertEqual(other.state_hash(), h)
        self.assertEqual(other.notes(), 1)

    def test_view(self):
        c = self.ctx
        c.deposit(5)
        c.keep(200)
        c.shut()
        v = cint.read_checkpoint(c.checkpoint())
        self.assertEqual([(g.name, g.type, g.value) for g in v.globals],
                         [("ledger.active", "Bool", False), ("ledger.mark", "U8", 200), ("ledger.total", "I64", 105),
                          ("lib.audit.notes", "I64", 1)])
        self.assertEqual((v.global_value("total"), v.global_value("lib.audit.notes")), (105, 1))
        with self.assertRaises(KeyError):
            v.global_value("missing")
        self.assertEqual((v.next_buffer, v.next_arena, v.next_pool, v.entries, v.effects), (1, 1, 1, 3, 1))
        self.assertEqual((v.registry, v.arenas, v.pools, v.fault), ((), (), (), None))
        self.assertEqual(v.revision, self.mod.revision)
        self.assertEqual(v.contract, "cint-rt-3")
        text = str(v)
        self.assertTrue(text.startswith("checkpoint cint-core-1/checkpoint/v1\ncontract cint-rt-3\n"), text)
        self.assertIn("global ledger.total I64 105\n", text)
        self.assertIn("state-hash %s\n" % v.state_hash.hex(), text)
        self.assertIn("4 globals", repr(v))
        with self.assertRaises(ValueError):
            cint.read_checkpoint(b"not a checkpoint")

    def test_faulted(self):
        """BX13-06: a Faulted context checkpoints, and its fault record restores with it."""
        c = self.ctx
        with self.assertRaises(cint.DivZeroFault):
            c.share(1, 0)
        saved = c.checkpoint()
        held = cint.read_checkpoint(saved).fault
        self.assertIsInstance(held, cint.DivZeroFault)
        c.clear_fault()
        self.assertEqual(c.share(6, 3), 2)
        c.restore(saved)
        self.assertIsInstance(c.fault(), cint.DivZeroFault)
        with self.assertRaises(cint.Faulted):
            c.balance()
        c.clear_fault()
        self.assertEqual(c.balance(), 100)

    def test_handles(self):
        """Module-level handles (decision on OQ-242) restore with their pool and arena, the
        view gives each as a HandleValue (rt/OPEN.md RT-OQ-50), None for the null handle, and
        the recording replays on both CPU backends."""
        mod = load(os.path.join("state", "handles.ci"))
        d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, d, ignore_errors=True)
        rec = cint.Recorder(os.path.join(d, "handles.rec"))
        c = mod.context(record=rec)
        self.assertEqual(c.run(), 1001)
        saved = c.checkpoint()
        self.assertEqual(c.run(), 102004)
        self.assertEqual(c.run(), 203009)
        c.restore(saved)
        self.assertEqual(c.run(), 102004)
        v = cint.read_checkpoint(saved)
        self.assertEqual([(g.name, g.type, g.value) for g in v.globals],
                         [("handles.calls", "I64", 1),
                          ("handles.newest", "Handle", _record.HandleValue("pool", 1, 0, 1, 0)),
                          ("handles.oldest", "Handle", None),
                          ("handles.span", "Handle", _record.HandleValue("arena", 1, 0, 1, 0))])
        self.assertIn("global handles.oldest Handle null\n", str(v))
        self.assertIn("global handles.newest Handle pool 1 slot 0 length 1 generation 0\n", str(v))
        other = mod.context()
        other.restore(saved)
        self.assertEqual(other.state_hash(), v.state_hash)
        self.assertEqual(other.run(), 102004)
        c.close()
        for backend in ("cpu", "ref"):
            r = cint.replay(mod, rec.path, backend=backend)
            self.assertEqual((r.status, r.boundaries), ("ok", 4), backend)

    def test_case_29(self):
        """Case 29: a checkpoint of another revision of the program is refused, unchanged."""
        with tempfile.TemporaryDirectory() as d:
            shutil.copytree(os.path.join(PROGRAMS, "state"), os.path.join(d, "state"))
            path = os.path.join(d, "state", "ledger.ci")
            with open(path, encoding="utf-8", newline="") as f:
                text = f.read()
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(text.replace("I64 total = 100;", "I64 total = 101;"))
            other = cint.load(path)
        self.assertNotEqual(other.revision, self.mod.revision)
        theirs = other.context().checkpoint()
        c = self.ctx
        c.deposit(5)
        h = c.state_hash()
        with self.assertRaises(cint.Refused) as caught:
            c.restore(theirs)
        self.assertEqual(caught.exception.reason, "revision")
        self.assertIn("case 29", str(caught.exception))
        self.assertEqual(c.state_hash(), h)
        self.assertEqual(c.balance(), 105)

    def test_case_29a(self):
        """Case 29a: every truncation and a trailing byte are refused with the context
        unchanged; a changed byte is refused unchanged, or restores a well-formed state."""
        c = self.ctx
        c.deposit(5)
        saved = c.checkpoint()
        c.deposit(7)
        h = c.state_hash()
        for n in range(len(saved)):
            with self.assertRaises(cint.Refused) as caught:
                c.restore(saved[:n])
            self.assertEqual(caught.exception.reason, "malformed")
        with self.assertRaises(cint.Refused):
            c.restore(saved + b"\0")
        self.assertEqual(c.state_hash(), h)
        accepted = 0
        for i in range(len(saved)):
            bad = bytearray(saved)
            bad[i] ^= 0xFF
            try:
                c.restore(bad)
            except cint.Refused:
                self.assertEqual(c.state_hash(), h, "byte %d" % i)
            else:
                accepted += 1
                c.restore(saved)
                c.deposit(7)
                h = c.state_hash()
        self.assertGreater(accepted, 0)                      # a global's value, at least
        self.assertLess(accepted, len(saved))

    def test_structs(self):
        """Module-level structs (decision on OQ-260) restore, the view gives each as a
        StructValue (rt/OPEN.md RT-OQ-51) with its array fields and the structs they hold, the
        text gives its type and the SHA-256 of its payload, and the recording replays on both
        CPU backends."""
        mod = load(os.path.join("state", "structs.ci"))
        d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, d, ignore_errors=True)
        rec = cint.Recorder(os.path.join(d, "structs.rec"))
        c = mod.context(record=rec)
        self.assertEqual(c.run(), 89100100)
        saved = c.checkpoint()
        self.assertEqual(c.run(), 91221100)
        self.assertEqual(c.run(), 12321413)
        c.restore(saved)
        self.assertEqual(c.run(), 91221100)
        v = cint.read_checkpoint(saved)

        def cell(count, odd):
            return _record.StructValue("structs.Cell", (("count", count), ("odd", odd)))

        track = _record.StructValue("structs.Track", (
            ("last", _record.ArrayValue("I64", (3,), (8, 9, 1))),
            ("cells", _record.ArrayValue("structs.Cell", (2,), (cell(0, False), cell(1, False)))),
            ("grid", _record.ArrayValue("I16", (2, 2), (0, 0, 0, 0)))))
        self.assertEqual([(g.name, g.type, g.value) for g in v.globals],
                         [("structs.calls", "I64", 1), ("structs.track", "structs.Track", track)])
        self.assertEqual(v.global_value("track")["cells"].values[1]["count"], 1)
        payload = (struct.pack("<3q", 8, 9, 1) + struct.pack("<q", 0) + b"\0" + struct.pack("<q", 1) + b"\0" +
                   struct.pack("<4h", 0, 0, 0, 0))
        self.assertIn("global structs.track structs.Track payload-sha256 %s\n" % hashlib.sha256(payload).hexdigest(),
                      str(v))
        other = mod.context()
        other.restore(saved)
        self.assertEqual(other.state_hash(), v.state_hash)
        self.assertEqual(other.run(), 91221100)
        c.close()
        for backend in ("cpu", "ref"):
            r = cint.replay(mod, rec.path, backend=backend)
            self.assertEqual((r.status, r.boundaries), ("ok", 4), backend)

    def test_case_29b(self):
        """Case 29b: five registrations and their releases, a checkpoint, a sixth (id 6), a
        restore; the next registration is id 7 (M-11), and the restore ended the sixth (M-11a)."""
        c = load("calls.ci").context()
        for _ in range(5):
            self.assertEqual(c.total_of(i64s(1, 2, 3)), 6)       # each bare array registers and is released
        saved = c.checkpoint()
        v = cint.read_checkpoint(saved)
        self.assertEqual((v.next_buffer, v.registry), (6, ()))
        held = cint.borrow(i64s(4, 5))
        self.assertEqual(c.total_of(held), 9)
        self.assertEqual([r.id for r in cint.read_checkpoint(c.checkpoint()).registry], [6])
        c.restore(saved)
        self.assertEqual(c.total_of(held), 9)                    # registers again (BX13-04 A)
        v = cint.read_checkpoint(c.checkpoint())
        self.assertEqual([(r.id, r.generation, r.elem, r.extent, r.owner) for r in v.registry],
                         [(7, 1, "I64", 2, "borrowed")])
        self.assertEqual(v.next_buffer, 8)

    def test_case_29b_recorded(self):
        """Case 29b while recording: the `restore` event records next buffer 7. The bridge
        registers through its internals here, since an entry that borrows an array is refused
        while recording (P-16); a registration is not an event of CINT-REC-1, so this recording
        does not replay: the state hash of its first outcome already differs."""
        mod = load("calls.ci")
        with tempfile.TemporaryDirectory() as d:
            rec = cint.Recorder(os.path.join(d, "29b.rec"))
            c = mod.context(record=rec)
            for _ in range(5):
                view = cint.borrow(i64s(1, 2, 3))
                with c._exclusive():
                    c._register(view)
                del view                                         # released at the next call
            c.add(1, 2)
            saved = c.checkpoint()
            sixth = cint.borrow(i64s(4))
            with c._exclusive():
                self.assertEqual(c._register(sixth).ident, 6)
            c.restore(saved)
            with c._exclusive():
                self.assertEqual(c._register(sixth).ident, 7)
            c.close()
            with open(rec.path, "rb") as f:
                data = f.read()
        restores = [e for e in events(mod, data) if e.kind == _state.EVENT_RESTORE]
        self.assertEqual([(e.next_buffer, e.next_arena, e.next_pool) for e in restores], [(7, 1, 1)])
        with self.assertRaises(cint.ReplayDivergence) as caught:
            cint.replay(mod, data, backend="cpu")
        self.assertEqual((caught.exception.result.what, caught.exception.result.boundaries), ("state", 0))


class EntryScopes(unittest.TestCase):
    """BX13-14: `entry_scope` restores its checkpoint when a Fault leaves it, and only then."""

    def setUp(self):
        self.ctx = ledger().context()

    def test_fault_restores(self):
        c = self.ctx
        c.deposit(5)
        with self.assertRaises(cint.DivZeroFault):
            with c.entry_scope() as scoped:
                self.assertIs(scoped, c)
                c.deposit(50)
                c.share(1, 0)
        self.assertIsNone(c.fault())
        self.assertEqual(c.balance(), 105)
        with self.assertRaises(cint.FuelFault):
            with c.entry_scope():
                c.deposit(1)
                c.burn(10 ** 6, fuel=100)
        self.assertEqual(c.balance(), 105)

    def test_no_fault_keeps(self):
        c = self.ctx
        with c.entry_scope():
            c.deposit(1)
        self.assertEqual(c.balance(), 101)
        with self.assertRaises(KeyError):
            with c.entry_scope():
                c.deposit(2)
                raise KeyError("not a fault")
        self.assertEqual(c.balance(), 103)

    def test_nested(self):
        c = self.ctx
        with c.entry_scope():
            c.deposit(1)
            with self.assertRaises(cint.DivZeroFault):
                with c.entry_scope():
                    c.deposit(10)
                    c.share(1, 0)
            self.assertEqual(c.balance(), 101)
            c.deposit(2)
        self.assertEqual(c.balance(), 103)

    def test_faulted_refused(self):
        c = self.ctx
        with self.assertRaises(cint.DivZeroFault):
            c.share(1, 0)
        with self.assertRaises(cint.Faulted):
            with c.entry_scope():
                self.fail("the scope ran on a Faulted context")


class Recordings(unittest.TestCase):
    """SPEC-03 H-14b and H-17a: a Recorder records every call on its context, and the
    recording replays on both CPU backends with the same chain and final state."""

    def setUp(self):
        self.mod = ledger()
        self.dir = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.dir, ignore_errors=True)

    def record(self, name="a.rec", **options) -> tuple:
        """The steps of interp/tests/rec_host.c, in Python: entries with print statements, a
        fault and its clear, a checkpoint and a restore."""
        rec = cint.Recorder(os.path.join(self.dir, name), **options)
        c = self.mod.context(record=rec)
        self.assertIs(c.recorder, rec)
        self.assertIs(rec.context, c)
        self.assertEqual(c.deposit(5), 105)
        self.assertEqual(c.deposit(7), 112)
        self.assertEqual((c.keep(9), c.neg(-3), c.is_active()), (9, 3, True))
        with self.assertRaises(cint.DivZeroFault):
            c.share(1, 0)
        c.clear_fault()
        saved = c.checkpoint()
        c.shut()
        c.restore(saved)
        self.assertEqual(c.burn(4), 4)
        h = c.state_hash()
        c.close()
        self.assertTrue(rec.closed)
        return rec.path, h

    def test_replay_both(self):
        path, h = self.record()
        results = [cint.replay(self.mod, path, backend=b) for b in ("cpu", "ref")]
        self.assertEqual(cint.replay(self.mod, path).backend, "ref")       # the default (BX13-19)
        for r in results:
            self.assertEqual((r.status, r.what, r.boundaries, r.stopped), ("ok", None, 8, False))
            self.assertEqual(r.final_state_hash, h)
            v = cint.read_checkpoint(r.checkpoint)
            self.assertEqual((v.global_value("total"), v.global_value("active"), v.global_value("mark")),
                             (112, True, 9))
        self.assertEqual(results[0].chain, results[1].chain)
        self.assertEqual(results[0].event, results[1].event)
        with open(path, "rb") as f:
            data = f.read()
        self.assertEqual(cint.replay(self.mod, data).chain, results[0].chain)   # bytes too
        kinds = [e.kind for e in events(self.mod, data)]
        self.assertEqual(kinds.count(_state.EVENT_ENTRY), 8)
        self.assertEqual(kinds.count(_state.EVENT_RESTORE), 1)

    def test_stop(self):
        path, _ = self.record()
        for backend in ("cpu", "ref"):
            r = cint.replay(self.mod, path, backend=backend, stop=2)
            self.assertEqual((r.status, r.boundaries, r.stopped), ("ok", 2, True))
            self.assertEqual(cint.read_checkpoint(r.checkpoint).global_value("total"), 112)
            r = cint.replay(self.mod, path, backend=backend, stop=99)
            self.assertEqual((r.boundaries, r.stopped), (8, False))
        for stop in (0, -1, True, 1.5):
            with self.assertRaises(ValueError):
                cint.replay(self.mod, path, stop=stop)

    def test_divergence(self):
        """A recording altered at one boundary diverges there, on both backends."""
        path, _ = self.record()
        with open(path, "rb") as f:
            data = f.read()
        at = data.index(struct.pack("<q", 105), data.index(b"deposit 5\n"))
        cases = ((data[:at] + struct.pack("<q", 106) + data[at + 8:], "result", 0),
                 (data.replace(b"deposit 7\n", b"deposit 8\n"), "effect", 1))
        for bad, what, boundaries in cases:
            for backend in ("cpu", "ref"):
                with self.assertRaises(cint.ReplayDivergence) as caught:
                    cint.replay(self.mod, bad, backend=backend)
                r = caught.exception.result
                self.assertEqual((r.status, r.what, r.boundaries, r.backend),
                                 ("diverged", what, boundaries, backend))

    def test_refused(self):
        path, _ = self.record()
        with open(path, "rb") as f:
            data = f.read()
        for cut in (1, 41, len(data) // 2):
            with self.assertRaises(cint.Refused):
                cint.replay(self.mod, data[:-cut])
        with self.assertRaises(cint.Refused) as caught:
            cint.replay(load("calls.ci"), path, backend="cpu")       # another program's revision
        self.assertIn("revision", str(caught.exception))
        with self.assertRaises(ValueError):
            cint.replay(self.mod, path, backend="cuda")
        with self.assertRaises(TypeError):
            cint.replay(path, path)

    def test_digest_mode(self):
        path, h = self.record("d.rec", mode="digest")
        for backend in ("cpu", "ref"):
            r = cint.replay(self.mod, path, backend=backend)
            self.assertEqual((r.status, r.final_state_hash), ("ok", h))

    def test_recorder_rules(self):
        with self.assertRaises(ValueError):
            cint.Recorder(os.path.join(self.dir, "m.rec"), mode="partial")
        with self.assertRaises(NotImplementedError):
            cint.Recorder(os.path.join(self.dir, "x.rec"), audit_pure=True)
        with self.assertRaises(TypeError):
            self.mod.context(record=os.path.join(self.dir, "r.rec"))
        rec = cint.Recorder(os.path.join(self.dir, "r.rec"))
        c = self.mod.context(record=rec)
        with self.assertRaises(ValueError):
            self.mod.context(record=rec)                             # one context per recording
        c.deposit(1)
        rec.close()                                                  # the context goes on, not recorded
        self.assertIsNone(c.recorder)
        self.assertEqual(c.deposit(1), 102)
        rec.close()
        c.close()
        r = cint.replay(self.mod, rec.path)
        self.assertEqual(r.boundaries, 1)
        with cint.Recorder(os.path.join(self.dir, "w.rec")) as rec:
            c = self.mod.context(record=rec)
            c.deposit(3)
        self.assertTrue(rec.closed)
        self.assertEqual(c.deposit(1), 104)
        self.assertEqual(cint.replay(self.mod, rec.path).boundaries, 1)

    def test_dropped_context(self):
        """A recorded context dropped without close() ends its recording before it is destroyed."""
        path = os.path.join(self.dir, "dropped.rec")
        c = self.mod.context(record=cint.Recorder(path))
        c.deposit(4)
        del c
        gc.collect()
        self.assertEqual(cint.replay(self.mod, path).boundaries, 1)

    def test_arrays_refused(self):
        """P-16: an entry that borrows an array is refused while recording, before it runs."""
        mod = load("calls.ci")
        rec = cint.Recorder(os.path.join(self.dir, "arr.rec"))
        c = mod.context(record=rec)
        with self.assertRaises(cint.Refused) as caught:
            c.total_of(i64s(1, 2))
        self.assertEqual(caught.exception.reason, "recording")
        self.assertIn("P-16", str(caught.exception))
        self.assertEqual(c.add(2, 3), 5)
        c.close()
        r = cint.replay(mod, rec.path, backend="cpu")
        self.assertEqual(r.boundaries, 1)

    def test_error_results(self):
        """An error result is recorded with its set and tag (box 12), and the recording replays
        on "cpu" and, since box 13 unit 8b-1, on cpu-sir-interp ("ref")."""
        mod = load("errors.ci")
        rec = cint.Recorder(os.path.join(self.dir, "err.rec"))
        c = mod.context(record=rec)
        self.assertEqual(c.digit(ord("7")), 7)
        with self.assertRaises(cint.ErrorResult) as caught:
            c.digit(32)
        self.assertEqual((type(caught.exception), caught.exception.tag), (mod.ParseError, 1))
        self.assertEqual((c.twice(-4), c.above(60)), (-8, True))
        c.close()
        for backend in ("cpu", "ref"):
            self.assertEqual(cint.replay(mod, rec.path, backend=backend).boundaries, 4)

    def test_library_without_code(self):
        """A library loaded without the SIR text beside it records no code, so its recording
        replays on "cpu" only."""
        mod = load("calls.ci")
        shutil.copy(mod.path, self.dir)
        bare = cint.load(os.path.join(self.dir, os.path.basename(mod.path)))
        self.assertIsNone(_build.program_code(bare.path))
        self.assertIsNotNone(_build.program_code(mod.path))
        rec = cint.Recorder(os.path.join(self.dir, "bare.rec"))
        c = bare.context(record=rec)
        c.add(1, 2)
        c.close()
        self.assertEqual(cint.replay(bare, rec.path, backend="cpu").status, "ok")
        with self.assertRaises(cint.Refused) as caught:
            cint.replay(bare, rec.path)
        self.assertIn("no SIR text", str(caught.exception))
        self.assertEqual(cint.replay(mod, rec.path, backend="ref").status, "ok")


class Interpreter(unittest.TestCase):
    """The interpreter's library is built once from this tree's interp/ and rt/ and cached."""

    def test_cached(self):
        first = _build.interpreter_library()
        self.assertEqual(_build.interpreter_library(), first)
        self.assertTrue(os.path.isfile(first))
        self.assertTrue(first.startswith(_build.Toolchain(_build.find_cint()).cache))
        self.assertIs(_replay.interpreter(), _replay.interpreter())


if __name__ == "__main__":
    unittest.main()
