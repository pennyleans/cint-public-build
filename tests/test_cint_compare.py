"""tools/cint_compare.py without a toolchain: configurations, receipts, records files, and the comparison."""
import json
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

try:
    import cint_compare as cmp
except ImportError:
    cmp = None

RECEIPT = ROOT / "results" / "cint" / "slice2" / "t1-b1-572349b" / "receipt-b1-clang-0-portable-san.json"
CONFIG = {"compiler": "b1", "leg": "gcc", "opt": "0", "helpers": "portable", "sanitize": False}


def program(name, records, category=("compared", {}), kind="program"):
    return {"category": list(category), "executions": len(records), "kind": kind, "program": name,
            "records": [[label, body] for label, body in records], "sanitizer": 0}


def records_file(path, observations, cats=None, end=True):
    names = sorted((o["program"] for o in observations), key=lambda n: n.encode("utf-8"))
    head = {"categories": cats or {}, "config": CONFIG, "only": [], "programs": names,
            "schema": cmp.RECORDS_SCHEMA, "scope": "quick", "suite_sha256": "0" * 64, "tool": {}}
    lines = [head] + sorted(observations, key=lambda o: o["program"].encode("utf-8"))
    if end:
        lines.append({"end": {"c0": {}, "executions": 0, "sanitizer_reports": 0}})
    path.write_text("".join(cmp.ascii_json(x) + "\n" for x in lines), encoding="ascii")
    return path


@unittest.skipIf(cmp is None, "cint_ref is not importable")
class Compare(unittest.TestCase):
    def test_configurations(self):
        self.assertEqual(cmp.parse_config("gcc-0-portable"), CONFIG)
        self.assertEqual(cmp.parse_config("seed:clang-2-builtin-san"),
                         {"compiler": "seed", "leg": "clang", "opt": "2", "helpers": "builtin", "sanitize": True})
        for text in ("gcc-1-portable", "b2:gcc-0-portable", "gcc-0-portable-san-x", "msvc-0-portable-san",
                     "apple-clang-2-builtin-san"):
            with self.assertRaises(ValueError, msg=text):
                cmp.parse_config(text)
        for text in ("b1:gcc-0-portable", "seed:apple-clang-2-builtin", "b1:clang-0-builtin-san"):
            self.assertEqual(cmp.config_tag(cmp.parse_config(text)), text)
        self.assertEqual(cmp.config_identity(cmp.parse_config("seed:msvc-2-builtin")),
                         {"backend": "cpu-c17", "compiler": "cint-seed", "helpers": "builtin", "leg": "msvc",
                          "opt": 2, "sanitize": False})

    def test_first_disagreement_selects_its_program(self):
        cases = [({"case": "arith/x", "call": "arith.add_wrap f I64 1"}, "arith/add_wrap"),
                 ({"case": "anchors/add.checked.i64.001", "reason": "r"}, "anchors/add.checked.i64.001"),
                 ({"case": "tables/add_checked_i8:17"}, "tables/add_checked_i8"),
                 ({"case": "control/script_with_test.t0"}, "control/script_with_test"),
                 ({"case": "module/lib/plain_math", "reason": "r"}, "module/lib/plain_math")]
        for first, want in cases:
            self.assertEqual(cmp.program_of(first), want, first)

    def test_receipt_configuration(self):
        config, scope, only, suite = cmp.from_receipt(RECEIPT)
        self.assertEqual(config, {"compiler": "b1", "leg": "clang", "opt": "0", "helpers": "portable",
                                  "sanitize": True})
        self.assertEqual((scope, only, len(suite)), ("full", [], 64))

    def test_compare_program(self):
        ref = program("p", [("p:1", ["outcome value", "return I64 1"]), ("p:2", ["outcome value"]),
                            ("p:3", None)])
        cand = program("p", [("p:1", ["outcome value", "return I64 2"]), ("p:2", ["outcome value"]),
                             ("p:4", ["outcome value"])])
        c = cmp.compare_program("p", ref, cand)
        self.assertEqual((c["compared"], c["differ"], c["only_reference"], c["only_candidate"]),
                         (2, ["p:1"], ["p:3"], ["p:4"]))
        c = cmp.compare_program("p", ref, None)
        self.assertEqual((c["compared"], c["differ"], c["only_reference"]), (0, [], ["p:1", "p:2", "p:3"]))

    def test_categories(self):
        anchors = program("anchors/add", [("anchors/a.1", None), ("anchors/a.2", None)], kind="anchor")
        case = program("arith/x", [("arith/x", None)], ("unsupported", {"limit": "SEED-15"}))
        cats = cmp.case_categories({"held/y": ["held", {"open_item": "I-2"}]}, [anchors, case])
        self.assertEqual(sorted(cats), ["anchors/a.1", "anchors/a.2", "arith/x", "held/y"])
        other = dict(cats, **{"arith/x": ["compared", {}]})
        del other["held/y"]
        counts, differ = cmp.category_summary((cats, other))
        self.assertEqual(counts["compared"], {"candidate": 3, "reference": 2})
        self.assertEqual(counts["unsupported"], {"candidate": 0, "reference": 1})
        self.assertEqual([(d["case"], d["reference"][0], d["candidate"]) for d in differ],
                         [("arith/x", "unsupported", ["compared", {}]), ("held/y", "held", None)])

    def test_records_files(self):
        a = [program("b/two", [("b/two", ["outcome value", "return I64 2"])]),
             program("a/one", [("a/one:1", ["outcome value"]), ("a/one:2", ["outcome fault", "fault.code E_OVERFLOW"])])]
        b = [program("a/one", [("a/one:1", ["outcome value"]), ("a/one:2", ["outcome fault", "fault.code E_SHIFT"])]),
             program("c/three", [("c/three", ["outcome value"])])]
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            sides = [cmp.Records(records_file(tmp / "a.jsonl", a)), cmp.Records(records_file(tmp / "b.jsonl", b))]
            lines = []
            totals, observed = cmp.run_compare(sides, [None, None], tmp, 2, lines.append)
            for side in sides:
                side.close()
            self.assertEqual({k: totals[k] for k in ("compared", "differ", "only_reference", "only_candidate",
                                                     "programs")},
                             {"compared": 2, "differ": 1, "only_reference": 1, "only_candidate": 1, "programs": 3})
            self.assertEqual([p["program"] for p in totals["programs_differ"]], ["a/one", "b/two", "c/three"])
            self.assertEqual(totals["first"]["case"], "a/one:2")
            self.assertEqual(totals["first"]["reference"], {"body": ["outcome fault", "fault.code E_OVERFLOW"],
                                                            "expected": "not inventoried"})
            text = "".join(lines)
            self.assertIn("a/one: 2 records compared, 1 differ\n  first differing record a/one:2\n", text)
            self.assertIn("    reference  outcome fault\n               fault.code E_OVERFLOW\n", text)
            self.assertIn("b/two: 0 records compared, 0 differ; 1 only on the reference\n", text)
            diffs = [json.loads(x) for x in (tmp / "differences.jsonl").read_text(encoding="ascii").splitlines()]
            self.assertEqual([(d["case"], d.get("only")) for d in diffs],
                             [("a/one:2", None), ("b/two", "reference"), ("c/three", "candidate")])
            self.assertEqual([sorted(o["program"] for o in obs) for obs in observed],
                             [["a/one", "b/two"], ["a/one", "c/three"]])

    def test_records_file_must_be_whole(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            side = cmp.Records(records_file(tmp / "a.jsonl", [program("a", [("a", ["outcome value"])])], end=False))
            self.assertEqual(side.observe("a")["program"], "a")
            with self.assertRaises(cmp.Blocked):
                side.close()
            path = records_file(tmp / "b.jsonl", [program("a", [("a", None)]), program("b", [("b", None)])])
            side = cmp.Records(path)
            self.assertEqual(side.observe("b")["program"], "b")
            with self.assertRaises(cmp.Blocked):   # read in byte order of name only
                side.observe("a")
            side.close()
            (tmp / "c.jsonl").write_text('{"schema":"other"}\n', encoding="ascii")
            with self.assertRaises(cmp.Blocked):
                cmp.Records(tmp / "c.jsonl")

    def test_command_line(self):
        for argv in (["--observe", "gcc-0-portable"], ["--ref", "gcc-0-portable"],
                     ["--receipts", "a.json", "b.json", "--ref", "gcc-0-portable"],
                     ["--ref", "gcc-0-portable", "--cand", "gcc-9-portable"],
                     ["--ref", "gcc-0-portable", "--cand", "gcc-2-portable", "--out", str(ROOT / "build")]):
            with self.assertRaises(SystemExit, msg=argv):
                cmp.main(argv)


if __name__ == "__main__":
    unittest.main()
