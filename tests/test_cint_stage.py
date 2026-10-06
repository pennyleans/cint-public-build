"""The staged bootstrap's pins: PIN, the file and receipt checks, cut, the floor layout and the design rules."""
import json
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

try:
    import cint_check as cc
    import cint_stage as stage
    from cint_ref import check as ref_check
    from cint_ref import parser as ref_parser
except ImportError:
    stage = None

FILES = {"compiler/main.ci": b"export I64 run() {\n    return 0;\n}\n", "rt/cint_rt.h": b"/* rt */\n",
         "rt/cint_rt.c": b"/* rt.c */\n", "compiler/tests/golden_host.c": b"/* host */\n"}
GROUPS = {"compiler_sources": ("compiler/main.ci",), "runtime_sources": ("rt/cint_rt.c", "rt/cint_rt.h"),
          "host_sources": ("compiler/tests/golden_host.c",)}
CONFIGURATIONS = (("apple-clang", "0", "portable", False), ("clang", "0", "portable", False),
                  ("gcc", "0", "portable", False), ("gcc", "2", "builtin", True), ("msvc", "0", "portable", False))


def receipt(configuration, files=FILES, result="pass", s1="b" * 64):
    identity = {group: [{"path": p, "bytes": len(files[p]), "sha256": cc.sha256(files[p])} for p in paths]
                for group, paths in GROUPS.items()}
    identity.update(compiler_source_identity="1" * 64, seed_source_identity="2" * 64,
                    runtime_contract_version="cint-rt-2", s2_tree_sha256="3" * 64)
    outcome = {"result": result}
    leg, opt, helpers, sanitize = configuration
    return {"schema": "cint-fixpoint-receipt-1", "identity": identity, "outcome": outcome,
            "identity_sha256": cc.sha256(cc.canonical({"identity": identity, "outcome": outcome})),
            "observations": {"configuration": {"leg": leg, "opt": opt, "helpers": helpers, "sanitize": sanitize},
                             "s1_sha256": s1}}


def matrix(**changes):
    return {"fixpoint-%s-%s-%s%s.json" % (c[0], c[1], c[2], "-san" if c[3] else ""): receipt(c, **changes)
            for c in CONFIGURATIONS}


def fields(receipts):
    return {"pin": "1", "commit": "c" * 40, "receipts": "results/x/fixpoint-c", **stage.matrix_fields(receipts)["fields"]}


def listed(files=FILES):
    return {p: (cc.sha256(d), len(d)) for p, d in files.items()}


def checked(sources):
    modules = {}

    def load(rel):
        if rel not in sources:
            return None
        modules[rel] = ref_parser.parse_module(sources[rel].decode("utf-8"), rel)
        return modules[rel]

    ref_check.check_program(load("main.ci"), load)
    return modules


class Stage(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(stage, "the staged bootstrap tool is missing")

    def test_pin_round_trips_in_byte_order_of_path(self):
        receipts = matrix()
        data = stage.write_pin(fields(receipts), FILES)
        text = data.decode("ascii")
        self.assertTrue(text.startswith("CINT-PIN-1\npin 1\ncommit " + "c" * 40 + "\n"))
        paths = [line.split(" ")[3] for line in text.splitlines() if line.startswith("file ")]
        self.assertEqual(paths, sorted(FILES, key=lambda p: p.encode("utf-8")))
        self.assertEqual(stage.read_pin(data), (fields(receipts), listed()))

    def test_pin_refuses_malformed_text(self):
        good = stage.write_pin(fields(matrix()), FILES)
        lines = good.decode("ascii").splitlines(keepends=True)
        file_line = next(i for i, line in enumerate(lines) if line.startswith("file "))
        bad = {"no header": b"".join(x.encode() for x in lines[1:]),
               "CR": good.replace(b"\n", b"\r\n"),
               "no final line feed": good[:-1],
               "fields out of order": "".join([lines[0], lines[2], lines[1]] + lines[3:]).encode(),
               "short digest": good.replace(b"receipt_identity ", b"receipt_identity x", 1),
               "files out of order": "".join(lines[:file_line] + lines[file_line:][::-1]).encode(),
               "repeated file": "".join(lines + [lines[-1]]).encode(),
               "no file": "".join(lines[:file_line]).encode()}
        for path in ("../x.c", "/x.c", "a//x.c", "a\\x.c", "PIN", "./x.c"):
            bad["path " + path] = "".join(lines + ["file %s 0 %s\n" % ("0" * 64, path)]).encode()
        bad["receipts outside the tree"] = good.replace(b"receipts results/x", b"receipts ../x")
        for name, data in bad.items():
            with self.subTest(name=name), self.assertRaises(ValueError):
                stage.read_pin(data)

    def test_files_check_names_every_difference(self):
        self.assertEqual(stage.check_files(listed(), dict(FILES)), [])
        files = dict(FILES, **{"rt/cint_rt.h": b"/* edited */\n", "rt/extra.h": b""})
        del files["rt/cint_rt.c"]
        problems = stage.check_files(listed(), files)
        self.assertEqual(len(problems), 3)
        self.assertIn("PIN lists rt/cint_rt.c, which the pin does not hold", problems)
        self.assertIn("the pin holds rt/extra.h, which PIN does not list", problems)
        self.assertTrue(any(p.startswith("rt/cint_rt.h: 13 bytes") for p in problems))

    def test_pin_tree_reads_every_file_but_pin(self):
        with tempfile.TemporaryDirectory() as temp:
            d = pathlib.Path(temp)
            for path, data in FILES.items():
                (d / path).parent.mkdir(parents=True, exist_ok=True)
                (d / path).write_bytes(data)
            (d / "PIN").write_bytes(b"x")
            self.assertEqual(stage.pin_tree(d), FILES)

    def test_receipts_check_accepts_a_matrix_that_names_the_pin(self):
        receipts = matrix()
        self.assertEqual(stage.check_receipts(fields(receipts), listed(), receipts), [])

    def test_receipts_check_refuses_a_matrix_that_does_not_pass(self):
        receipts = matrix()
        tampered = dict(receipts)
        name = sorted(tampered)[0]
        tampered[name] = json.loads(json.dumps(tampered[name]))
        tampered[name]["identity"]["s2_tree_sha256"] = "4" * 64
        failing = dict(receipts, **{name: receipt(CONFIGURATIONS[0], result="fail")})
        no_msvc = {k: v for k, v in receipts.items() if "msvc" not in k}
        repeated = dict(receipts, extra=receipts[name])
        two_sp = dict(receipts, **{name: receipt(CONFIGURATIONS[0], s1="d" * 64)})
        cases = {"tampered": (tampered, "identity_sha256 does not match"), "failing": (failing, "result fail"),
                 "no msvc": (no_msvc, "no receipt from the msvc leg"),
                 "repeated": (repeated, "a second receipt of configuration"),
                 "two SP": (two_sp, "2 digests of the seed's C"), "empty": ({}, "no fixpoint receipt"),
                 "schema": (dict(receipts, **{name: {"schema": "other"}}), "not a cint-fixpoint-receipt-1")}
        for case, (given, text) in cases.items():
            with self.subTest(case=case):
                problems = stage.check_receipts(fields(receipts), listed(), given)
                self.assertTrue(any(text in p for p in problems), problems)

    def test_receipts_check_compares_pin_fields_and_files(self):
        receipts = matrix()
        pin_fields = dict(fields(receipts), sp_sha256="e" * 64)
        problems = stage.check_receipts(pin_fields, listed(), receipts)
        self.assertEqual(problems, ["PIN gives sp_sha256 %s; the receipts give %s" % ("e" * 64, "b" * 64)])
        files = dict(FILES, **{"rt/cint_rt.h": b"/* other */\n"})
        problems = stage.check_receipts(fields(receipts), listed(files), receipts)
        self.assertEqual(len(problems), 1)
        self.assertTrue(problems[0].startswith("rt/cint_rt.h: PIN gives 12 bytes"), problems)

    def write_receipts(self, root, receipts):
        directory = root / "results" / "x" / "fixpoint-c"
        directory.mkdir(parents=True)
        for name, r in receipts.items():
            (directory / name).write_bytes(cc.canonical(r) + b"\n")
        (directory / "README.md").write_bytes(b"not a receipt\n")
        return directory

    def test_cut_writes_the_pin_that_check_accepts(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            receipts = matrix()
            directory = self.write_receipts(root, receipts)
            reads = []
            dest = stage.cut(root, 1, "c" * 40, directory,
                             lambda commit, path: reads.append((commit, path)) or FILES[path])
            self.assertEqual(dest, root / "bootstrap" / "pin-1")
            self.assertEqual(sorted(p for _, p in reads), sorted(FILES))
            self.assertEqual(stage.pin_directories(root), {1: dest})
            pin_fields, pin_files = stage.read_pin((dest / "PIN").read_bytes())
            self.assertEqual(pin_fields, fields(receipts))
            self.assertEqual(stage.check_files(pin_files, stage.pin_tree(dest)), [])
            self.assertEqual(stage.check_receipts(pin_fields, pin_files, stage.load_receipts(directory)), [])
            with self.assertRaisesRegex(ValueError, "never edited"):
                stage.cut(root, 1, "c" * 40, directory, lambda commit, path: FILES[path])

    def test_cut_refuses_a_commit_whose_files_differ_or_a_failing_matrix(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            directory = self.write_receipts(root, matrix())
            changed = dict(FILES, **{"compiler/main.ci": b"export I64 run() { return 1; }\n"})
            with self.assertRaisesRegex(ValueError, "compiler/main.ci at ccccccc"):
                stage.cut(root, 1, "c" * 40, directory, lambda commit, path: changed[path])
            self.assertFalse((root / "bootstrap").exists())
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            directory = self.write_receipts(root, {k: v for k, v in matrix().items() if "gcc" not in k})
            with self.assertRaisesRegex(ValueError, "no receipt from the gcc leg"):
                stage.cut(root, 1, "c" * 40, directory, lambda commit, path: FILES[path])

    def test_floor_stages_the_pin_runtime_headers_beside_the_current_bridge(self):
        with tempfile.TemporaryDirectory() as temp:
            base = pathlib.Path(temp)
            for side in ("pin", "current"):
                (base / side / "rt").mkdir(parents=True)
                for name in stage.FLOOR_PIN + stage.FLOOR_CURRENT:
                    (base / side / "rt" / name).write_bytes(side.encode())
            rt = stage.stage_floor(base / "pin", base / "current", base / "work")
            self.assertEqual({p.name: p.read_bytes() for p in rt.iterdir()},
                             {**{n: b"pin" for n in stage.FLOOR_PIN}, **{n: b"current" for n in stage.FLOOR_CURRENT}})

    def test_call_cycle_finds_direct_and_mutual_recursion(self):
        self.assertIsNone(stage.call_cycle({"a": [("b", 1)], "b": [("c", 2)], "c": []}))
        self.assertEqual(stage.call_cycle({"a": [("a", 1)]}), [("a", "a", 1)])
        self.assertEqual(stage.call_cycle({"a": [("b", 1)], "b": [("c", 2)], "c": [("b", 3)]}),
                         [("b", "c", 2), ("c", "b", 3)])

    def test_design_rules_accept_the_subset(self):
        sources = {"main.ci": b"import util;\nconst I64 N = 4;\nexport I64 run() {\n    return util.twice(N);\n}\n",
                   "util.ci": b"export I64 twice(I64 x) {\n    return x + x;\n}\n"}
        self.assertEqual(stage.design_problems(checked(sources), sources), [])

    def test_design_rules_refuse_with_file_and_position(self):
        sources = {"main.ci": b"import util;\nI64 counter = 0;\nexport I64 run() {\n    \"n={counter}\\n\";\n"
                              b"    return util.down(3);\n}\n",
                   "util.ci": b"// \xc3\xa9\nexport I64 down(I64 n) {\n    if (n == 0) {\n        return 0;\n    }\n"
                              b"    return up(n - 1);\n}\nI64 up(I64 n) {\n    return down(n);\n}\n"}
        problems = stage.design_problems(checked(sources), sources)
        self.assertIn("compiler/main.ci:2:1: a module-level variable `counter`", problems)
        self.assertIn("compiler/main.ci:4:5: a print statement", problems)
        self.assertIn("compiler/util.ci:1:4: a byte outside ASCII (0xC3)", problems)
        cycle = [p for p in problems if "call cycle" in p]
        self.assertEqual(len(cycle), 1)
        self.assertRegex(cycle[0], r"^compiler/util\.ci:9:\d+: a call cycle, `down` -> `up` -> `down`$")

    def test_design_rules_refuse_a_program_function_in_a_constant(self):
        sources = {"main.ci": b"const I64 N = 4;\nexport I64 run() {\n    return f();\n}\nI64 f() {\n    return N;\n}\n"}
        modules = checked(sources)
        call = next(n for n in stage.walk(modules["main.ci"].functions) if stage.user_call(n))
        modules["main.ci"].consts[0].init = call   # as if a checker had accepted `const I64 N = f();`
        self.assertEqual(stage.design_problems(modules, sources),
                         ["compiler/main.ci:3:12: a call of the program function `f` in a constant, static "
                          "assertion or extent"])

    def test_pin_1_matches_its_receipts(self):
        pins = stage.pin_directories()
        if 1 not in pins:
            self.skipTest("no bootstrap/pin-1/")
        pin_fields, pin_files = stage.read_pin((pins[1] / "PIN").read_bytes())
        self.assertEqual(stage.check_files(pin_files, stage.pin_tree(pins[1])), [])
        self.assertEqual(stage.check_receipts(pin_fields, pin_files,
                                              stage.load_receipts(ROOT / pin_fields["receipts"])), [])
        self.assertEqual((pin_fields["commit"][:7], len(pin_files), pin_fields["sp_sha256"][:8]),
                         ("66d81ca", 21, "6afe83a4"))


if __name__ == "__main__":
    unittest.main()
