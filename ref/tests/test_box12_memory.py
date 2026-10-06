import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""Tests for the box 12 memory lifetimes group (docs/design/notes/2026-10-05-box12-errors-cleanup-lifetimes.md,
section 7): module-level arenas and pools, child arenas, handles and their checks, `AllocError`,
and the frame arena, under the readings of ref/OPEN.md REF-OQ-45. `MemoryCases` runs the 40
cases of conformance/memory (decision 2026-10-06 on OQ-210 adopts the readings; all 40 are
frozen from cint_ref) and compares each outcome with the one its header states; the other
classes pin the readings one at a time."""
import re
import shutil
import tempfile
import unittest

from cint_ref import parser
from cint_ref.exec import run_program
from cint_ref.faults import Refused

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CONFORMANCE = os.path.join(ROOT, "conformance")


def run(src, entry="run", root=None):
    return run_program(src.encode("utf-8"), "t.ci", entry, root=root)


def run_case(rel):
    with open(os.path.join(CONFORMANCE, rel + ".ci"), "rb") as f:
        data = f.read()
    m = re.search(r"^// entry: (\w+)", data.decode("utf-8"), re.M)
    return run_program(data, rel + ".ci", m.group(1) if m else None, root=CONFORMANCE)


def summary(o):
    """(outcome, detail[, fuel]), as in test_box12.py."""
    if o.kind == "value":
        return o.kind, o.value.render() if o.value is not None else None, o.fuel
    if o.kind == "error":
        return o.kind, o.error, o.fuel
    if o.kind == "fault":
        r = o.record
        return o.kind, (r.code, r.operation, [v.render() for v in r.operands], r.exact,
                        r.limit.render() if r.limit else None, (r.position.line, r.position.column),
                        [(p.line, p.column) for p in r.stack]), o.fuel
    if o.kind == "compile-error":
        p = o.diagnostic.position
        return o.kind, (o.diagnostic.code, (p.line, p.column))
    return o.kind, o.message


def refused(src, entry="run"):
    o = run(src, entry)
    assert o.kind == "refused", summary(o)
    return o.message

# The outcome each memory case's header states (`spec outcome`), with its fuel.
CASES = {
    "memory/arena_alloc_full": ("fault", ("E_BOUNDS", "arena.alloc", ["I64 4", "I64 7", "I64 10"], None, None, (14, 31), []), 1),
    "memory/arena_alloc_negative": ("fault", ("E_BOUNDS", "arena.alloc", ["I64 -1", "I64 3", "I64 8"], None, None, (12, 31), []), 1),
    "memory/arena_alloc_result_full": ("value", "I64 392", 12),
    "memory/arena_alloc_view_sum": ("value", "I64 110", 7),
    "memory/arena_alloc_zero_filled": ("value", "I64 0", 5),
    "memory/arena_child_carved_from_parent": ("fault", ("E_BOUNDS", "arena.alloc", ["I64 3", "I64 6", "I64 8"], None, None, (20, 31), []), 1),
    "memory/arena_child_exhausts_parent": ("fault", ("E_BOUNDS", "arena.alloc", ["I64 4", "I64 5", "I64 8"], None, None, (11, 31), []), 1),
    "memory/arena_child_retired_by_parent_reset": ("fault", ("E_STALE_HANDLE", "handle.check", ["U64 0", "I64 2", "U64 0", "U32 3", "U8 1", "U64 0", "U8 2"], None, None, (17, 16), []), 1),
    "memory/arena_grandchild_retired": ("fault", ("E_STALE_HANDLE", "handle.check", ["U64 0", "I64 1", "U64 0", "U32 3", "U8 1", "U64 0", "U8 2"], None, None, (16, 26), []), 1),
    "memory/arena_index_bounds": ("fault", ("E_BOUNDS", "index.checked.i64", ["I64 4"], None, "I64 4", (13, 24), []), 1),
    "memory/arena_parameters": ("value", "I64 104", 8),
    "memory/arena_reset_stale_generation": ("fault", ("E_STALE_HANDLE", "handle.check", ["U64 0", "I64 4", "U64 0", "U32 1", "U8 1", "U64 1", "U8 3"], None, None, (13, 19), []), 1),
    "memory/arena_retired_alloc": ("fault", ("E_STALE_HANDLE", "arena.alloc", ["U64 0", "I64 4", "U64 0", "U32 2", "U8 1", "U64 0", "U8 2"], None, None, (13, 28), []), 1),
    "memory/arena_stale_view_after_callee_reset": ("fault", ("E_STALE_HANDLE", "handle.check", ["U64 0", "I64 3", "U64 0", "U32 1", "U8 1", "U64 1", "U8 3"], None, None, (15, 14), [(22, 12)]), 4),
    "memory/arena_struct_elements": ("value", "I64 114", 1),
    "memory/c2001_arena_handle_into_pool": ("compile-error", ("C2001", (12, 19))),
    "memory/c2051_arena_in_struct": ("compile-error", ("C2051", (7, 17))),
    "memory/c2058_child_arena_rebound": ("compile-error", ("C2058", (11, 5))),
    "memory/c2060_write_through_in_arena": ("compile-error", ("C2060", (10, 5))),
    "memory/c2067_reset_through_in_parameter": ("compile-error", ("C2067", (10, 5))),
    "memory/c2103_handle_compare": ("compile-error", ("C2103", (12, 9))),
    "memory/c4012_arena_in_function": ("compile-error", ("C4012", (8, 5))),
    "memory/c6004_capacity_not_constant": ("compile-error", ("C6004", (8, 11))),
    "memory/c6006_capacity_negative": ("compile-error", ("C6006", (8, 12))),
    "memory/frame_arena_counts_elements": ("fault", ("E_BOUNDS", "arena.alloc", ["I64 1", "I64 2097152", "I64 2097152"], None, None, (8, 12), [(16, 16)]), 2),
    "memory/frame_arena_recursion_exhausts": ("fault", ("E_BOUNDS", "arena.alloc", ["I64 1000000", "I64 2000000", "I64 2097152"], None, None, (8, 20), [(17, 12), (13, 12), (13, 12)]), 4),
    "memory/frame_arena_released_at_block_exit": ("value", "I64 3", 4),
    "memory/pool_capacity_zero": ("fault", ("E_BOUNDS", "pool.insert", ["I64 0", "I64 0", "I64 0"], None, None, (10, 26), []), 1),
    "memory/pool_free_list_lifo": ("fault", ("E_STALE_HANDLE", "pool.deref", ["U64 2", "I64 1", "U64 1", "U32 1", "U8 2", "U64 2", "U8 3"], None, None, (21, 26), []), 1),
    "memory/pool_full_insert_faults": ("fault", ("E_BOUNDS", "pool.insert", ["I64 2", "I64 0", "I64 2"], None, None, (16, 29), []), 1),
    "memory/pool_handles_stored": ("value", "I64 83", 2),
    "memory/pool_insert_deref_remove": ("value", "I64 735", 1),
    "memory/pool_insert_result_full": ("value", "I64 46", 1),
    "memory/pool_insert_result_try_main": ("error", ("AllocError", "full", "U16 1"), 1),
    "memory/pool_null_handle": ("fault", ("E_STALE_HANDLE", "pool.deref", ["U64 0", "I64 0", "U64 0", "U32 0", "U8 0", "U64 0", "U8 1"], None, None, (14, 18), []), 1),
    "memory/pool_other_container": ("fault", ("E_STALE_HANDLE", "pool.deref", ["U64 0", "I64 1", "U64 0", "U32 2", "U8 2", "U64 0", "U8 6"], None, None, (15, 18), []), 1),
    "memory/pool_remove_stale": ("fault", ("E_STALE_HANDLE", "pool.deref", ["U64 0", "I64 1", "U64 0", "U32 1", "U8 2", "U64 1", "U8 3"], None, None, (15, 12), []), 1),
    "memory/pool_slot_assign": ("value", "I64 128", 1),
    "memory/pool_stale_generation": ("fault", ("E_STALE_HANDLE", "pool.deref", ["U64 0", "I64 1", "U64 0", "U32 1", "U8 2", "U64 1", "U8 3"], None, None, (15, 21), []), 1),
    "memory/script_pool_module_level": ("value", None, 2),
}

STDOUT = {
    "memory/pool_insert_result_try_main": b"one\ntwo\n",
    "memory/script_pool_module_level": b"42\n",
}


def stale(o):
    """The operands of an `E_STALE_HANDLE` record, after checking the code."""
    assert o.kind == "fault" and o.record.code == "E_STALE_HANDLE", summary(o)
    return [v.render() for v in o.record.operands]


class MemoryCases(unittest.TestCase):
    """Each memory case gives the outcome its header states, which its frozen `.expect` file records."""

    def test_outcomes(self):
        for rel, want in CASES.items():
            with self.subTest(case=rel):
                o = run_case(rel)
                self.assertEqual(summary(o), want)
                self.assertEqual(o.stdout, STDOUT.get(rel, b""))


A = "Arena(I64, 8) scratch;\n\n"
P = "Pool(I64, 4) counts;\n\n"


class Surface(unittest.TestCase):
    """Declarations, types and the view rules for arena and pool values (REF-OQ-45 items 1 to 6, a, e)."""

    def test_memory_false_refuses_the_types(self):
        for src in (P, "export I64 run(in Handle(I64) h) {\n    return 0;\n}\n"):
            with self.subTest(src=src):
                with self.assertRaises(Refused):
                    parser.parse_module(src, "t.ci", memory=False)

    def test_identifiers_per_kind_in_declaration_order(self):
        # Arenas 1 and 2, pool 1: the handle from `second` names container 2.
        src = ("Arena(I64, 4) first;\nPool(I64, 2) counts;\nArena(I64, 4) second;\n\n"
               "export I64 run() {\n    Handle(I64[]) h = second.alloc(1);\n    second.reset();\n"
               "    return second[h][0];\n}\n")
        self.assertEqual(stale(run(src)), ["U64 0", "I64 1", "U64 0", "U32 2", "U8 1", "U64 1", "U8 3"])

    def test_identifiers_follow_the_load_order_of_modules(self):
        # The root module is loaded first (SPEC-09 CONF-11 rule 10), so its arena is 1 and the
        # imported module's is 2.
        root = tempfile.mkdtemp()
        try:
            with open(os.path.join(root, "lib.ci"), "w") as f:
                f.write("Arena(I64, 4) store;\n\nexport I64 stale() {\n    Handle(I64[]) h = store.alloc(1);\n"
                        "    store.reset();\n    return store[h][0];\n}\n")
            src = ("import lib;\n\nArena(I64, 4) mine;\n\nexport I64 run() {\n    Handle(I64[]) h = mine.alloc(1);\n"
                   "    I64 x = mine[h][0];\n    return x + lib.stale();\n}\n")
            with open(os.path.join(root, "t.ci"), "w") as f:
                f.write(src)
            o = run(src, root=root)
        finally:
            shutil.rmtree(root)
        self.assertEqual(stale(o)[3], "U32 2")

    def test_a_handle_never_set_is_null_in_a_global_or_a_field(self):
        src = (P + "struct S { Handle(I64) h; I64 n; }\nHandle(I64) g;\n\n"
               "export I64 run() {\n    S s;\n    _ = counts.insert(1);\n    return counts[s.h] + counts[g];\n}\n")
        self.assertEqual(stale(run(src)), ["U64 0", "I64 0", "U64 0", "U32 0", "U8 0", "U64 0", "U8 1"])

    def test_a_handle_global_writes_no_state_line(self):
        o = run(P + "Handle(I64) g;\nI64 n = 0;\n\nexport I64 run() {\n    g = counts.insert(5);\n    n = counts[g];\n"
                    "    return n;\n}\n")
        self.assertEqual(summary(o), ("value", "I64 5", 1))
        self.assertEqual([name for _, name, _ in o.state], ["n"])

    def test_c2051_for_an_arena_value_returned_or_global(self):
        o = run(A + "Arena(I64) keep(inout Arena(I64) a) {\n    return a;\n}\n\nexport I64 run() {\n    return 0;\n}\n")
        self.assertEqual(summary(o), ("compile-error", ("C2051", (3, 1))))
        o = run("Pool(I64) any;\n\nexport I64 run() {\n    return 0;\n}\n")
        self.assertEqual(summary(o), ("compile-error", ("C2051", (1, 1))))

    def test_c2103_for_compared_pools(self):
        o = run(P + "export I64 run() {\n    return counts == counts ? 1 : 0;\n}\n")
        self.assertEqual(summary(o)[0:2], ("compile-error", ("C2103", (4, 12))))

    def test_c4012_inside_a_test_block(self):
        o = run("test \"t\" {\n    Pool(I64, 2) p;\n}\n\nexport I64 run() {\n    return 0;\n}\n")
        self.assertEqual(summary(o), ("compile-error", ("C4012", (2, 5))))


class Operations(unittest.TestCase):
    """Member operations, retirement, the `container` reason and their records (items 7 to 14)."""

    def test_alloc_result_of_a_negative_count_faults(self):
        o = run(A + "AllocError!I64 f() {\n    Handle(I64[]) h = try scratch.alloc_result(-1);\n    return 0;\n}\n\n"
                    "export I64 run() {\n    return f() catch 9;\n}\n")
        self.assertEqual(summary(o)[1][:3], ("E_BOUNDS", "arena.alloc", ["I64 -1", "I64 0", "I64 8"]))

    def test_reset_of_a_retired_arena(self):
        o = run(A + "export I64 run() {\n    Arena(I64) part = scratch.child(2);\n    scratch.reset();\n    part.reset();\n"
                    "    return 0;\n}\n")
        self.assertEqual(summary(o)[1][:3], ("E_STALE_HANDLE", "arena.reset",
                                             ["U64 0", "I64 2", "U64 0", "U32 2", "U8 1", "U64 0", "U8 2"]))
        self.assertEqual(summary(o)[1][5], (6, 10))

    def test_null_comes_before_container(self):
        o = run(P + "export I64 run() {\n    Handle(I64) h;\n    return counts[h];\n}\n")
        self.assertEqual(stale(o)[-1], "U8 1")

    def test_container_comes_before_retired(self):
        # A handle of `scratch` used through its retired child: the container is checked first.
        o = run(A + "export I64 run() {\n    Arena(I64) part = scratch.child(2);\n    Handle(I64[]) h = scratch.alloc(1);\n"
                    "    scratch.reset();\n    return part[h][0];\n}\n")
        self.assertEqual(stale(o), ["U64 2", "I64 1", "U64 0", "U32 1", "U8 1", "U64 0", "U8 6"])

    def test_a_handle_parameter_dereferences_its_arena(self):
        o = run("Arena(I64, 4) scratch;\nPool(I64, 4) counts;\n\nI64 peek(in Handle(I64[]) h) {\n    return scratch[h][0];\n}\n\n"
                "export I64 run() {\n    Handle(I64[]) a = scratch.alloc(1);\n    _ = counts.insert(3);\n    return peek(a);\n}\n")
        self.assertEqual(summary(o)[0:2], ("value", "I64 0"))

    def test_a_slice_of_an_arena_view_reports_its_handle(self):
        o = run(A + "export I64 run() {\n    Handle(I64[]) h = scratch.alloc(4);\n    in I64[] v = scratch[h][1..3];\n"
                    "    scratch.reset();\n    return v[0];\n}\n")
        self.assertEqual(stale(o), ["U64 0", "I64 4", "U64 0", "U32 1", "U8 1", "U64 1", "U8 3"])
        self.assertEqual(o.record.operation, "handle.check")

    def test_member_operations_charge_no_fuel(self):
        o = run(A + P + "export I64 run() {\n    Handle(I64) c = counts.insert(1);\n    counts.remove(c);\n"
                        "    Handle(I64[]) h = scratch.alloc(2);\n    Arena(I64) part = scratch.child(2);\n    part.reset();\n"
                        "    scratch.reset();\n    return 7;\n}\n")
        self.assertEqual(summary(o), ("value", "I64 7", 1))


class FrameArena(unittest.TestCase):
    """The frame arena of 2,097,152 elements (items 15 and 16)."""

    def test_released_on_return_and_break(self):
        src = ("I64 early(I64 k) {\n    I64[2_000_000] big;\n    if (k > 0) {\n        return k;\n    }\n    return 0;\n}\n\n"
               "export I64 run() {\n    I64 s = 0;\n    while (true) {\n        I64[2_000_000] w;\n        s += 1;\n"
               "        if (s == 2) {\n            break;\n        }\n    }\n    return s + early(1) + early(2);\n}\n")
        self.assertEqual(summary(run(src))[0:2], ("value", "I64 5"))

    def test_each_entry_starts_empty(self):
        src = "export I64 run() {\n    I64[2_097_152] all;\n    return 1;\n}\n"
        for _ in range(2):
            self.assertEqual(summary(run(src)), ("value", "I64 1", 1))

    def test_a_script_top_level_array_is_charged(self):
        o = run("I64[2_097_152] all;\nI64[1] more;\n\"done\\n\";\n", entry=None)
        self.assertEqual(summary(o)[1][:3], ("E_BOUNDS", "arena.alloc", ["I64 1", "I64 2097152", "I64 2097152"]))
        self.assertEqual(summary(o)[1][5], (2, 8))

    def test_scalars_and_structs_are_not_charged(self):
        src = ("struct S { I64[4] a; }\n\nexport I64 run() {\n    I64[2_097_152] all;\n    S s;\n    I64 n = 3;\n"
               "    return n + s.a[0];\n}\n")
        self.assertEqual(summary(run(src))[0:2], ("value", "I64 3"))


class Refusals(unittest.TestCase):
    """Forms with no specified diagnostic or record (REF-OQ-45)."""

    def test_refused(self):
        cases = {
            "an Arena local without an initializer":
                A + "export I64 run() {\n    Arena(I64) a;\n    return 0;\n}\n",
            "an array field of a pool element":
                "struct B { I64[2] v; }\nPool(B, 2) bs;\n\nexport I64 run() {\n    B b;\n    Handle(B) h = bs.insert(b);\n"
                "    return bs[h].v[0];\n}\n",
            "a handle result of the entry":
                P + "export Handle(I64) run() {\n    return counts.insert(1);\n}\n",
            "a capacity outside a declaration":
                "I64 f(in Arena(I64, 4) a) {\n    return 0;\n}\n\nexport I64 run() {\n    return 0;\n}\n",
            "a dereference with two handles":
                P + "export I64 run() {\n    Handle(I64) h = counts.insert(1);\n    return counts[h, h];\n}\n",
        }
        for what, src in cases.items():
            with self.subTest(what=what):
                refused(src)


if __name__ == "__main__":
    unittest.main()
