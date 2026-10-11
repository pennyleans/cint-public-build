"""tools/cint_bench.py (v1.0 unit P1): the SPEC-06 11 statistics, bar verdicts, manifests and
their parameter lines, the alternating order, bench-call's output, and the receipt form, with
no toolchain; then bench/driver/bench_call.c built on this platform's leg against the stand-in
baseline bench/tests/tick_c.c, which needs the leg's C compiler and no cint."""
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cint_bench as cb  # noqa: E402
import cint_check  # noqa: E402

LEG = "msvc" if os.name == "nt" else "apple-clang" if sys.platform == "darwin" else "gcc"
MANIFESTS = sorted(cb.WORKLOADS.glob("*.json")) + [ROOT / "bench" / "tests" / "tick_stream.json"]
COMPILER_S2 = cb.WORKLOADS / "compiler_s2.json"
GRAVITY = cb.WORKLOADS / "gravity_pairs.json"
NO_GRAVITY = "gravity_pairs is not in the public tree: it times the flight engine"


def stats(*samples):
    return cb.statistics(list(samples))


class Statistics(unittest.TestCase):
    def test_nearest_rank(self):
        ordered = list(range(1, 101))
        self.assertEqual(cb.nearest_rank(ordered, 50), 50)
        self.assertEqual(cb.nearest_rank(ordered, 99), 99)
        self.assertEqual(cb.nearest_rank([7, 9, 11], 50), 9)
        self.assertEqual(cb.nearest_rank([7, 9], 50), 7)
        self.assertEqual(cb.nearest_rank([5], 99), 5)

    def test_reportable(self):
        self.assertFalse(cb.reportable(50, 1))
        self.assertTrue(cb.reportable(50, 2))
        self.assertFalse(cb.reportable(99, 99))
        self.assertTrue(cb.reportable(99, 100))

    def test_integer_statistics_in_decimal_strings(self):
        s = stats(30, 10, 20)
        self.assertEqual(s, {"count": 3, "min_ns": "10", "max_ns": "30", "sum_ns": "60", "median_ns": "20",
                             "samples_ns": ["30", "10", "20"]})
        self.assertNotIn("median_ns", stats(4))
        self.assertEqual(stats(*range(100, 0, -1))["p99_ns"], "99")

    def test_statistic_is_an_exact_fraction(self):
        s = stats(1, 2, 4)
        self.assertEqual(cb.statistic(s, "mean"), (7, 3))
        self.assertEqual(cb.statistic(s, "median"), (2, 1))
        self.assertIsNone(cb.statistic(s, "p99"))


class Bars(unittest.TestCase):
    def bar(self, **kw):
        bar = {"id": "b", "status": "Ruled", "statistic": "median", "relation": "at_most", "cint": ["x", "y"],
               "against": {"variants": ["c", "d"]}, "times": [11, 10]}
        bar.update(kw)
        return bar

    def test_best_cint_against_fastest_baseline(self):
        per = {"x": stats(120, 120), "y": stats(110, 110), "c": stats(100, 100), "d": stats(105, 105)}
        out = cb.evaluate_bar(self.bar(), per, {})
        self.assertEqual((out["cint_variant"], out["against_variant"], out["verdict"]), ("y", "c", "met"))
        self.assertEqual((out["cint"], out["against"]), (["110", "1"], ["100", "1"]))
        per["y"] = stats(111, 111)
        self.assertEqual(cb.evaluate_bar(self.bar(), per, {})["verdict"], "not met")

    def test_at_most_and_below_at_equality(self):
        per = {"x": stats(110, 110), "c": stats(100, 100)}
        self.assertEqual(cb.evaluate_bar(self.bar(), per, {})["verdict"], "met")
        self.assertEqual(cb.evaluate_bar(self.bar(relation="below"), per, {})["verdict"], "not met")

    def test_mean_compares_fractions_exactly(self):
        per = {"x": stats(1, 2), "c": stats(1, 1, 1)}   # 3/2 against 3/3 times 3/2
        bar = self.bar(statistic="mean", times=[3, 2])
        self.assertEqual(cb.evaluate_bar(bar, per, {})["verdict"], "met")
        self.assertEqual(cb.evaluate_bar(dict(bar, relation="below"), per, {})["verdict"], "not met")

    def test_reference_by_configuration(self):
        bar = self.bar(cint=["x"], against={"ns_by_config": {"gcc-2-builtin": 1000}}, times=[5, 4])
        per = {"x": stats(1250, 1250)}
        out = cb.evaluate_bar(bar, per, {"x": "gcc-2-builtin"})
        self.assertEqual((out["verdict"], out["against_variant"]), ("met", "reference gcc-2-builtin"))
        self.assertEqual(cb.evaluate_bar(bar, per, {"x": "msvc-2-builtin"})["verdict"], "not evaluated")

    def test_reference_per_element(self):
        bar = self.bar(cint=["x", "y"], against={"ns_per_element": 2000}, times=[1, 1])
        per = {"x": stats(132001, 132001), "y": stats(132000, 132000)}   # 66 elements at 2000 ns
        out = cb.evaluate_bar(bar, per, {}, 66)
        self.assertEqual((out["verdict"], out["cint_variant"], out["against_variant"]),
                         ("met", "y", "reference 2000 ns per element"))
        self.assertEqual((out["cint"], out["against"], out["cint_per_element"]),
                         (["132000", "1"], ["132000", "1"], ["132000", "66"]))
        self.assertEqual((out["elements"], out["ns_per_element"]), (66, 2000))
        self.assertEqual(cb.evaluate_bar(dict(bar, relation="below"), per, {}, 66)["verdict"], "not met")
        self.assertEqual(cb.evaluate_bar(dict(bar, cint=["x"]), per, {}, 66)["verdict"], "not met")
        mean = cb.evaluate_bar(dict(bar, statistic="mean"), {"y": stats(131999, 132002)}, {}, 66)
        self.assertEqual((mean["verdict"], mean["cint_per_element"]), ("not met", ["264001", "132"]))

    def test_not_evaluated_without_a_reportable_statistic(self):
        per = {"x": stats(5), "c": stats(4, 4)}
        out = cb.evaluate_bar(self.bar(), per, {})
        self.assertEqual((out["verdict"], out["reason"]), ("not evaluated", "no cint variant measured"))
        out = cb.evaluate_bar(self.bar(), {"x": stats(5, 5)}, {})
        self.assertEqual(out["verdict"], "not evaluated")


class Manifests(unittest.TestCase):
    def test_committed_manifests_are_well_formed_and_canonical(self):
        self.assertGreaterEqual(len(MANIFESTS), 4 + COMPILER_S2.exists() + GRAVITY.exists())
        for path in MANIFESTS:
            text = path.read_text("utf-8")
            w = json.loads(text)
            self.assertEqual(cb.manifest_problems(w, path.stem), [], path.name)
            self.assertEqual(text, json.dumps(w, indent=2, sort_keys=True, ensure_ascii=True) + "\n", path.name)

    def test_every_v1_bar_of_p1_has_a_workload(self):
        names = ["orbit_1e8", "ledger", "transactions"]
        bars = {("B-1", "B-1"), ("B-6", "B-6 checked C"), ("B-6", "B-6 Rust")}
        if COMPILER_S2.exists():  # not in the public tree: its bar is set by a receipt that tree does not hold
            names.append("compiler_s2")
            bars.add(("B-10", "B-10"))
        if GRAVITY.exists():  # not in the public tree: it times the flight engine
            names.append("gravity_pairs")
            bars.add(("B-3", "B-3"))
        served = {(w["benchmark"], b["id"]) for w in map(cb.load_workload, names) for b in w["bars"]}
        self.assertTrue(bars <= served)
        for name in ("ledger", "transactions"):
            w = cb.load_workload(name)
            self.assertEqual({v.get("fuel") for v in w["variants"] if v["role"] == "cint"}, {"off", "on"})

    def test_problems_are_named(self):
        w = json.loads((cb.WORKLOADS / "ledger.json").read_text("utf-8"))
        w["bars"][0]["cint"] = ["nobody"]
        w["variants"][0]["helpers"] = "fast"
        w["args"] = ["n", "missing"]
        w["method"]["rounds"] = 0
        problems = cb.manifest_problems(w, "other")
        for text in ("name 'ledger' is not the file's name", "method.rounds is not a count",
                     "variant cint-portable: helpers 'fast'", "argument missing is not a parameter",
                     "bar B-6 checked C: cint names no variants"):
            self.assertIn(text, problems)
        self.assertEqual(cb.manifest_problems({"name": "x"}, "x")[0], "no benchmark")

    @unittest.skipUnless(GRAVITY.exists(), NO_GRAVITY)
    def test_b3_is_two_microseconds_a_pair_of_the_engine(self):
        w = cb.load_workload("gravity_pairs")
        self.assertEqual(w["bars"][0]["against"], {"ns_per_element": 2000})
        self.assertEqual((w["elements"], w["parameters"]["pairs"]["value"]), ("pairs", 11 * 10 // 2 + 11))
        self.assertEqual(w["cint"]["imports"], ["flight/gravity.ci", "flight/state.ci", "flight/units.ci"])
        self.assertEqual({v.get("fuel") for v in w["variants"]}, {"off"})

    @unittest.skipUnless(GRAVITY.exists(), NO_GRAVITY)
    def test_imports_and_times_per_element(self):
        w = json.loads((cb.WORKLOADS / "gravity_pairs.json").read_text("utf-8"))
        w["cint"]["imports"] = ["flight/gravity.ci", "flight/missing.ci", "flight/units.h", "bench/flight/gravity.ci"]
        del w["elements"]
        w["bars"][0]["against"] = {"ns_per_element": 0}
        problems = cb.manifest_problems(w, "gravity_pairs")
        for text in ("import 'flight/missing.ci' is not a .ci file of the tree",
                     "import 'flight/units.h' is not a .ci file of the tree", "two cint modules share a file name",
                     "bar B-3: ns_per_element is not a positive integer",
                     "bar B-3: a time per element needs the workload's elements"):
            self.assertIn(text, problems)

    def test_a_parameter_line_must_hold_the_parameter_value(self):
        w = json.loads((cb.WORKLOADS / "orbit_1e8.json").read_text("utf-8"))
        w["parameters"]["steps"]["value"] = 5
        self.assertTrue(any("not the parameter's value 5" in p for p in cb.manifest_problems(w, "orbit_1e8")))

    def test_workload_paths(self):
        self.assertEqual(cb.workload_path("ledger"), cb.WORKLOADS / "ledger.json")
        with self.assertRaises(cb.BenchError):
            cb.workload_path("../ledger")
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(cb.BenchError):
                cb.workload_path(str(pathlib.Path(d) / "x.json"))
        with self.assertRaises(cb.BenchError):
            cb.load_workload("no_such_workload")

    def test_values_for(self):
        w = cb.load_workload("ledger")
        self.assertEqual(cb.values_for(w, "full"), {"n": 1000000, "initial": 1000000})
        self.assertEqual(cb.values_for(w, "agreement"), {"n": 1000, "initial": 1000000})


class Lines(unittest.TestCase):
    def test_find_line(self):
        text = "x\n    const I64 STEPS = 1_000;   // c\nconst I64 STEPSX = 2;\n"
        self.assertEqual(cb.find_line(text, "const I64 STEPS = {};"), (1, 1000))
        self.assertIsNone(cb.find_line(text + "const I64 STEPS = 3;\n", "const I64 STEPS = {};"))
        self.assertIsNone(cb.find_line("y\n", "const I64 STEPS = {};"))
        with self.assertRaises(cb.BenchError):
            cb.find_line(text, "no hole")

    def test_substitute_keeps_indentation_and_comment(self):
        data = b"a\n    const long STEPS = 100000000L;   // ten to the eighth\nb\n"
        out = cb.substitute(data, {"steps": "const long STEPS = {}L;"}, {"steps": 5554})
        self.assertEqual(out, b"a\n    const long STEPS = 5554L;   // ten to the eighth\nb\n")
        with self.assertRaises(cb.BenchError):
            cb.substitute(b"a\n", {"steps": "const long STEPS = {}L;"}, {"steps": 1})

    def test_orbit_sources_hold_their_parameter(self):
        w = cb.load_workload("orbit_1e8")
        source = (ROOT / w["cint"]["source"]).read_bytes()
        out = cb.substitute(source, w["cint"]["lines"], cb.values_for(w, "agreement"))
        self.assertEqual(cb.find_line(out.decode("utf-8"), w["cint"]["lines"]["steps"]), (cb.find_line(
            source.decode("utf-8"), w["cint"]["lines"]["steps"])[0], 5554))
        self.assertEqual(len(out.split(b"\n")), len(source.split(b"\n")))


class Order(unittest.TestCase):
    def test_rounds_rotate(self):
        self.assertEqual(cb.rounds_order(["a", "b", "c"], 4),
                         [["a", "b", "c"], ["b", "c", "a"], ["c", "a", "b"], ["a", "b", "c"]])
        self.assertEqual(cb.rounds_order(["a"], 2), [["a"], ["a"]])


class Driver(unittest.TestCase):
    def test_parse(self):
        out = cb.parse_driver("bench-call 1\r\ntimer CLOCK_MONOTONIC 1 1\r\nsample 10 0 5 -1\r\n"
                              "sample 12 0 5 -1\r\nend 2\r\n")
        self.assertEqual(out, {"timer": {"kind": "CLOCK_MONOTONIC", "num": 1, "den": 1},
                               "samples": [(10, 0, 5, -1), (12, 0, 5, -1)]})
        for bad in ("", "bench-call 2\ntimer x 1 1\nend 0\n", "bench-call 1\ntimer x 1 1\nsample 1 0 0 0\nend 2\n",
                    "bench-call 1\ntimer x 1 1\nsample 1 0 0 0\n"):
            with self.assertRaises(cb.BenchError):
                cb.parse_driver(bad)

    def test_native_libs(self):
        out = "note: native-static-libs: -liconv -lSystem -lc -lm\n"
        self.assertEqual(cb.native_libs(out, True), ["-liconv"])
        self.assertEqual(cb.native_libs(out, False), ["-liconv", "-lSystem", "-lc", "-lm"])
        self.assertEqual(cb.native_libs("note: native-static-libs: kernel32.lib /defaultlib:msvcrt", False),
                         ["kernel32.lib", "/defaultlib:msvcrt"])
        self.assertEqual(cb.native_libs("", True), [])

    def test_from_outcome(self):
        self.assertEqual(cb.from_outcome(["case x", "outcome value", "return I64 3", ""]),
                         ["outcome value", "return I64 3"])


class Results(unittest.TestCase):
    def test_stream_digest(self):
        samples = [(5, 0, 7, 3), (6, 0, -2, 4)]
        self.assertEqual(cb.stream_digest(samples, 10), {
            "calls": "10 to 11", "result_stream_sha256": cb.sha256(b"7 -2"), "fuel_stream_sha256": cb.sha256(b"3 4")})
        self.assertNotIn("fuel_stream_sha256", cb.stream_digest([(5, 0, 7, -1)], 0))

    def test_same_result_ignores_fuel_only(self):
        self.assertTrue(cb.same_result({"result": "I64 1", "fuel_consumed": "9"}, {"result": "I64 1"}))
        self.assertFalse(cb.same_result({"result": "I64 1"}, {"result": "I64 2"}))
        self.assertFalse(cb.same_result({"exit": 0, "stdout_sha256": "a"}, {"exit": 0, "stdout_sha256": "b"}))


class Receipts(unittest.TestCase):
    def test_canonical_with_identity_digest(self):
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "r" / "perf.json"
            identity, outcome = {"kind": "perf", "schema": cb.PERF}, {"results": {}, "verdict": "measured"}
            ident = cb.write_receipt(path, identity, outcome, {"machine": {"cores": 4}})
            body = json.loads(path.read_bytes())
            self.assertEqual(ident, cb.sha256(cint_check.canonical({"identity": identity, "outcome": outcome})))
            self.assertEqual(body["observations"], {"machine": {"cores": 4}, "receipt_identity_sha256": ident})
            self.assertEqual(path.read_bytes(), cint_check.canonical(body) + b"\n")

    def test_a_cint_from_another_tree_is_refused(self):
        here = cb.compiler_identity(None)
        self.assertRegex(here, "^[0-9a-f]{64}$")
        with tempfile.TemporaryDirectory() as d:
            cint = pathlib.Path(d) / "cint"
            with self.assertRaises(cb.BenchError):
                cb.compiler_identity(cint)
            (pathlib.Path(d) / "bootstrap.json").write_text(json.dumps(
                {"identity": {"compiler_source_identity": "0" * 64}}), "ascii")
            with self.assertRaises(cb.BenchError):
                cb.compiler_identity(cint)
            (pathlib.Path(d) / "bootstrap.json").write_text(json.dumps(
                {"identity": {"compiler_source_identity": here}}), "ascii")
            self.assertEqual(cb.compiler_identity(cint), here)


class BenchCall(unittest.TestCase):
    """bench_call.c and target_c.c built with this platform's leg (its warnings as errors), linked
    with the stand-in baseline bench/tests/tick_c.c, and run."""

    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="cint-bench-", ignore_cleanup_errors=True)
        work = pathlib.Path(cls.temp.name)
        tc = cb.toolchain(LEG, "2", "builtin")
        flags = cb.baseline_flags(tc, {})
        objs = [cb.compile_c(tc, cb.DRIVER / "bench_call.c", work / "drv", "test", flags=flags, includes=[cb.DRIVER]),
                cb.compile_c(tc, cb.DRIVER / "target_c.c", work / "drv", "test", flags=flags,
                             defines=["BENCH_FN=tick_c", "BENCH_ARITY=2"], includes=[cb.DRIVER]),
                cb.compile_c(tc, ROOT / "bench" / "tests" / "tick_c.c", work / "obj", "test", flags=flags)]
        cls.exe = cb.link_exe(tc, objs, work / "bench-call", "test")
        cls.env = tc.env

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_driver(self, *args):
        return subprocess.run([str(self.exe), *args], capture_output=True, text=True, env=self.env)

    @staticmethod
    def tick(t, steps):
        d = t
        for i in range(steps):
            d = (d * 31 + i + t) % 1000003
        return d

    def test_samples(self):
        out = cb.run_driver(self.exe, "tick_c", [7, 100], None, 2, 5, self.env, None)
        self.assertEqual(len(out["samples"]), 5)
        self.assertEqual({s[1:] for s in out["samples"]}, {(0, self.tick(7, 100), -1)})
        self.assertTrue(all(s[0] >= 0 for s in out["samples"]))
        self.assertGreater(out["timer"]["num"], 0)
        self.assertGreater(out["timer"]["den"], 0)

    def test_index_counts_from_the_first_warmup_call(self):
        out = cb.run_driver(self.exe, "tick_c", [0, 50], None, 3, 4, self.env, None, index=0)
        self.assertEqual([s[2] for s in out["samples"]], [self.tick(t, 50) for t in range(3, 7)])

    def test_refusals(self):
        self.assertEqual(self.run_driver("--entry", "other", "--arg", "1", "--arg", "2").returncode, 3)
        self.assertEqual(self.run_driver("--entry", "tick_c", "--arg", "1").returncode, 3)
        for args in (("--arg", "1"), ("--entry", "tick_c", "--arg", "x"), ("--entry", "tick_c", "--samples", "0"),
                     ("--entry", "tick_c", "--arg", "1", "--index", "1"),
                     ("--entry", "tick_c", "--arg", "9223372036854775808"), ("--entry",)):
            self.assertEqual(self.run_driver(*args).returncode, 2, args)
        r = self.run_driver("--entry", "tick_c", "--arg", "-9223372036854775808", "--arg", "0", "--samples", "1")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("sample ", r.stdout)


class Runs(unittest.TestCase):
    """tools/cint_bench.py run, end to end at the agreement parameters on this platform's leg,
    with a cint that tools/cint_bootstrap.py builds from this tree (or CINT_BENCH_CINT, one it
    built earlier from this tree): a program (orbit_1e8), a call (ledger) and a per-iteration
    stream (bench/tests/tick_stream.json), and a per-iteration stream of a source with imports
    (gravity_pairs). Timings are not judged; agreement, the result digests and the receipt form
    are."""

    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="cint-bench-runs-", ignore_cleanup_errors=True)
        cls.dir = pathlib.Path(cls.temp.name)
        given = os.environ.get("CINT_BENCH_CINT")
        if given:
            cls.cint = pathlib.Path(given)
        else:
            boot = cls.dir / "boot"
            subprocess.run([sys.executable, "tools/cint_bootstrap.py", "--leg", LEG, "--out", str(boot)], cwd=ROOT,
                           check=True, capture_output=True)
            cls.cint = boot / ("cint.exe" if LEG == "msvc" else "cint")

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def bench(self, workload, out, *extra):
        r = subprocess.run([sys.executable, "tools/cint_bench.py", "run", workload, "--leg", LEG, "--cint",
                            str(self.cint), "--size", "agreement", "--out", str(out), *extra], cwd=ROOT,
                           capture_output=True, text=True, errors="replace")
        return r

    def receipts(self, out, count):
        import cint_receipts
        paths = sorted((out / "receipts").glob("*.json"))
        self.assertEqual(len(paths), count, [p.name for p in paths])
        rows = [cint_receipts.check_file(p) for p in paths]
        for row in rows:
            self.assertEqual(row["problems"], [], row["file"])
        perf = [json.loads(p.read_bytes()) for p in paths if p.name.startswith("perf-")]
        self.assertEqual(len(perf), 1)
        receipt = perf[0]
        for path in paths:
            if path.name.startswith("agree-"):
                agree = json.loads(path.read_bytes())
                self.assertEqual(agree["outcome"]["outcome"], "agree", path.name)
                self.assertIn(agree["observations"]["receipt_identity_sha256"],
                              receipt["identity"]["agreement"].values())
        for key in ("toolchain", "machine", "started_utc", "per_variant"):
            self.assertNotIn(key, json.dumps(receipt["identity"]) + json.dumps(receipt["outcome"]))
        self.assertEqual(receipt["outcome"]["verdict"], "measured")
        return receipt

    def test_program(self):
        out = self.dir / "orbit"
        r = self.bench("orbit_1e8", out, "--rounds", "2", "--samples", "1", "--warmup", "0")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        receipt = self.receipts(out, 3)
        self.assertEqual(receipt["identity"]["workload"]["parameters"], {"steps": 5554})
        digests = {d["stdout_sha256"] for d in receipt["outcome"]["results"].values()}
        self.assertEqual(digests, {cb.load_workload("orbit_1e8")["cint"]["expected_stdout_sha256"]["5554"]})
        self.assertEqual(receipt["observations"]["execution_order"],
                         ["cint-portable", "cint-builtin", "c-double", "cint-builtin", "c-double", "cint-portable"])
        self.assertIn(receipt["observations"]["bars"][0]["verdict"], ("met", "not met"))
        again = self.bench("orbit_1e8", out, "--rounds", "1")
        self.assertEqual(again.returncode, 1)
        self.assertIn("already holds receipts", again.stderr)

    def test_call(self):
        out = self.dir / "ledger"
        only = ["cint-builtin", "cint-builtin-fuel-on", "c-portable"]
        r = self.bench("ledger", out, "--rounds", "2", "--samples", "2", "--warmup", "1", "--only", *only)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        receipt = self.receipts(out, 3)
        results = receipt["outcome"]["results"]
        self.assertEqual(results["cint-builtin"]["result"], "I64 998995")
        self.assertEqual(results["cint-builtin"]["fuel_consumed"], results["cint-builtin-fuel-on"]["fuel_consumed"])
        self.assertEqual(results["cint-builtin-fuel-on"]["result"], "I64 998995")
        self.assertRegex(results["cint-builtin-fuel-on"]["fuel_consumed"], "^[1-9][0-9]*$")
        self.assertEqual(receipt["observations"]["baseline_results"]["c-portable"], {"result": "I64 998995"})
        fuel = {v["name"]: v.get("fuel") for v in receipt["identity"]["variants"]}
        self.assertEqual(fuel["cint-builtin"]["allowance"], "none")
        self.assertEqual(fuel["cint-builtin-fuel-on"]["allowance"], str(2 ** 63 - 1))
        bars = {b["id"]: b for b in receipt["observations"]["bars"]}
        self.assertIn(bars["B-6 checked C"]["verdict"], ("met", "not met"))
        self.assertEqual(bars["B-6 Rust"]["verdict"], "not evaluated")
        self.assertEqual(receipt["observations"]["reports"][0]["id"], "fuel cost")

    def test_stream(self):
        out = self.dir / "tick"
        r = self.bench(str(ROOT / "bench" / "tests" / "tick_stream.json"), out, "--samples", "20")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        receipt = self.receipts(out, 2)
        stream = receipt["outcome"]["results"]["cint-builtin"]
        self.assertEqual(stream["calls"], "4 to 23")
        values = " ".join(str(BenchCall.tick(t, 10)) for t in range(4, 24))
        self.assertEqual(stream["result_stream_sha256"], cb.sha256(values.encode("ascii")))
        self.assertRegex(stream["fuel_stream_sha256"], "^[0-9a-f]{64}$")
        self.assertEqual(receipt["observations"]["baseline_results"]["c"],
                         {k: v for k, v in stream.items() if k != "fuel_stream_sha256"})
        self.assertEqual(receipt["observations"]["per_variant"]["cint-builtin"]["count"], 40)

    @unittest.skipUnless(GRAVITY.exists(), NO_GRAVITY)
    def test_imports(self):
        out = self.dir / "gravity"
        r = self.bench("gravity_pairs", out, "--only", "cint-builtin", "--rounds", "2", "--samples", "3", "--warmup",
                       "0")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        receipt = self.receipts(out, 2)
        agree = json.loads((out / "receipts" / ("agree-gravity_pairs-%s-O2-cint-builtin.json" % LEG)).read_bytes())
        stream = receipt["outcome"]["results"]["cint-builtin"]
        self.assertEqual(stream["calls"], "0 to 2")
        self.assertEqual(stream["result_stream_sha256"], agree["outcome"]["returns_sha256"])
        for files in (receipt["identity"]["source"]["files"], agree["identity"]["source"]["files"]):
            for rel in ("flight/gravity.ci", "flight/state.ci", "flight/units.ci",
                        "bench/workloads/gravity_pairs/gravity_pairs.ci"):
                self.assertEqual(files[rel], cb.sha256((ROOT / rel).read_bytes()), rel)
        bar = receipt["observations"]["bars"][0]
        self.assertEqual((bar["id"], bar["elements"], bar["cint_per_element"][1]), ("B-3", 66, "66"))
        self.assertIn(bar["verdict"], ("met", "not met"))
        self.assertIn("ns per element", r.stdout)


if __name__ == "__main__":
    unittest.main()
