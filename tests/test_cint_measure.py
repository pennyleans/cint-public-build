"""The arithmetic of tools/cint_measure.py, with no toolchain (slice 2 task 2.17)."""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import cint_measure as measure  # noqa: E402

MIB = 1048576


def sample(path, phases, tables=(), modules=None):
    """A sample of the receipt's form: phase rows [phase, module, bytes, status, result, fuel, fault]
    and table rows [table, elem_bytes, capacity, scope, used]."""
    n = max((row[2] for row in phases), default=0)
    return {"path": path, "modules": modules or [["m.ci", n]], "phases": [list(r) for r in phases],
            "tables": [list(t) for t in tables]}


class Fuel(unittest.TestCase):
    def test_empty_intercept_and_per_byte_ceiling(self):
        fit = measure.fuel_fit([sample("@empty", [(1, -1, 0, 0, 0, 11, None), (2, 0, 0, 0, 0, 7, None)]),
                                sample("x.ci", [(2, 0, 3, 0, 0, 19, None), (4, -1, 3, 0, 0, 12, None)])])
        self.assertEqual((fit["raw_c"], fit["raw_k"]), (11, 3))
        self.assertEqual((fit["candidate_c"], fit["candidate_k"]), (44, 12))
        self.assertEqual(fit["k_at"], {"path": "x.ci", "phase": 2, "module": 0, "bytes": 3, "fuel": 19})
        self.assertEqual(fit["most_fuel_by_phase"]["2"]["fuel"], 19)
        self.assertTrue(fit["verified"])

    def test_diagnostic_results_count_and_faults_do_not(self):
        fit = measure.fuel_fit([sample("@empty", [(1, -1, 0, 0, 0, 10, None)]),
                                sample("d.ci", [(2, 0, 2, 0, 1, 20, None)]),
                                sample("f.ci", [(2, 0, 2, 1, None, 999, 9)])])
        self.assertEqual(fit["raw_k"], 5)
        self.assertEqual(fit["calls"], 2)

    def test_refused_and_faulted_empty_calls_give_no_intercept(self):
        fit = measure.fuel_fit([sample("@empty", [(1, -1, 0, 1, None, 3, 9), (2, 0, 0, 2, None, None, None)])])
        self.assertIsNone(fit["raw_c"])
        self.assertFalse(fit["verified"])


def plan_rows(n, decls):
    """Manifest rows [elem_bytes, capacity, scope] of a toy plan: tokens n + 1 (32 B), symbols
    n / 2 + 64 (80 B), names 2 x symbols + 4,096 + 2 x decls (40 B), SIR 8 x tokens + 16 (48 B)."""
    rows = [[1, 0, 1] for _ in measure.TABLE_NAMES]
    symbols = (n + 1) // 2 + 64
    rows[0], rows[16] = [80, 2, 0], [64, decls, 0]
    rows[3], rows[6], rows[12] = [32, n + 1, 1], [48, 8 * (n + 1) + 16, 1], [80, symbols, 1]
    rows[13] = [40, 2 * symbols + 4096 + 2 * decls, 1]
    return rows


def as_tables(rows, used=0):
    return [(t, e, cap, scope, used) for t, (e, cap, scope) in enumerate(rows)]


class DerivedK(unittest.TestCase):
    def test_k_is_the_formula_slope_without_the_declaration_term(self):
        plans = {n: plan_rows(n, n // 4 + n // 32 + 2) for n in (1, 4096, 1 << 20, 4 * MIB)}
        derived = measure.derive_K(plans)
        r = {n: measure.module_bytes(rows) - 80 * rows[16][1] for n, rows in plans.items()}
        k = {n: max(0, -(-(r[n] - MIB) // n)) for n in plans}
        self.assertEqual(derived["K"], max(k.values()))
        self.assertEqual(derived["K"], 496)   # 32 + 384 + 40 + 40 bytes per source byte, ceiling at 1 MiB
        at = derived["at_bytes"]
        self.assertEqual(at, min(n for n in plans if k[n] == derived["K"]))
        self.assertEqual(derived["declaration_row_bytes"], 80)
        self.assertEqual(derived["name_constant_rows"], [4096])
        self.assertTrue(derived["within_ceiling"])
        names = next(t for t in derived["tables"] if t["name"] == "names")
        self.assertEqual(names["bytes"], 40 * (2 * ((at + 1) // 2 + 64) + 4096))
        self.assertEqual(sum(t["bytes"] for t in derived["tables"]), r[at])

    def test_k_above_the_ceiling(self):
        rows = plan_rows(100000, 1)
        rows[6][1] = 4000000
        self.assertFalse(measure.derive_K({100000: rows})["within_ceiling"])

    def test_no_plans_give_no_k(self):
        self.assertIsNone(measure.derive_K({})["K"])

    def test_sweep_ends_at_the_module_limit(self):
        sizes = measure.sweep_sizes()
        self.assertEqual((sizes[0], sizes[1023], sizes[1024], sizes[-1]), (1, 1024, 2048, 4194304))


class Storage(unittest.TestCase):
    def test_inputs_are_checked_against_k_n_plus_one_mib_plus_declarations(self):
        derived = measure.derive_K({4096: plan_rows(4096, 9000)})
        fits = sample("a.ci", [(1, -1, 4096, 0, 0, 1, None)], as_tables(plan_rows(4096, 500000), 3),
                      modules=[["a.ci", 4096], ["b.ci", 10]])
        over_rows = plan_rows(4096, 500000)
        over_rows[6][1] += MIB
        over = sample("o.ci", [(1, -1, 4096, 0, 0, 1, None)], as_tables(over_rows))
        result = measure.storage([fits, over, sample("x.ci", [(1, -1, 5, 0, 1, 1, None)])], derived)
        self.assertEqual(result["inputs_over_bound"], ["o.ci"])
        self.assertEqual(result["name_constant_rows"], [4096])
        self.assertEqual(result["most_with_declarations"]["path"], "o.ci")
        self.assertEqual(result["most_with_declarations"]["declaration_bytes"], 80 * 500000)
        tokens = next(u for u in result["use"] if u["table"] == 3)
        self.assertEqual(tokens["most_used_fraction"], [3, 4097])
        self.assertEqual(tokens["most_used_bytes_per_source_byte"], [96, 4096])

    def test_without_k_every_input_is_over(self):
        s = sample("a.ci", [(1, -1, 10, 0, 0, 1, None)], as_tables(plan_rows(10, 1)))
        self.assertEqual(measure.storage([s], measure.derive_K({}))["inputs_over_bound"], ["a.ci"])


class Frozen(unittest.TestCase):
    def test_every_call_within_the_frozen_budget(self):
        samples = [sample("x.ci", [(2, 0, 3, 0, 0, 19, None), (4, -1, 3, 0, 1, 25, None)])]
        derived = {"K": 1000, "within_ceiling": True}
        self.assertTrue(measure.frozen(samples, (5, 10), derived)["every_call_within"])
        self.assertFalse(measure.frozen(samples, (4, 10), derived)["every_call_within"])

    def test_a_faulted_call_is_not_within(self):
        samples = [sample("x.ci", [(2, 0, 3, 1, None, 25, 9)])]
        result = measure.frozen(samples, (100, 100), {"K": 1, "within_ceiling": True})
        self.assertEqual((result["calls_not_returned"], result["every_call_within"]), (1, False))

    def test_committed_receipts_support_the_bridge_budget(self):
        k, c = measure.bridge_budget()
        self.assertEqual((k, c), (21524, 19876))   # decision 2026-10-05 on OQ-175 (CINTC-09)
        for leg in ("gcc", "clang"):
            with self.subTest(leg=leg):
                receipt = measure.json.loads((ROOT / "results/cint/t2" / ("measure-%s.json" % leg)).read_bytes())
                self.assertEqual(receipt["schema"], measure.SCHEMA)
                self.assertEqual(receipt["identity"]["production_budget"], {"k": k, "c": c})
                frozen = receipt["outcome"]["frozen"]
                self.assertEqual((frozen["k"], frozen["c"]), (k, c))
                self.assertTrue(frozen["every_call_within"])
                self.assertLessEqual(frozen["K"], measure.K_CEILING)
                self.assertEqual(receipt["outcome"]["storage"]["inputs_over_bound"], [])
                self.assertEqual(receipt["outcome"]["measurement_errors"], [])


class Host(unittest.TestCase):
    def test_events_parse_and_unknown_lines_are_errors(self):
        out = (b'{"e":"phase","phase":1,"module":-1,"budget":16777216,"status":0,"result":0,"fuel":5,"fault":null}\n'
               b'{"e":"table","table":0,"elem_bytes":80,"capacity":2,"scope":0,"used":1}\n'
               b'noise\n'
               b'{"e":"build","result":0,"diag":[]}\n')
        phases, tables, build, errors = measure.parse_events(out)
        self.assertEqual(phases[0]["fuel"], 5)
        self.assertEqual(tables[0]["used"], 1)
        self.assertEqual(build, {"result": 0, "diag": []})
        self.assertEqual(len(errors), 1)

    def test_bridge_budget_matches_the_header(self):
        k, c = measure.bridge_budget()
        text = (ROOT / "rt/cint_bridge.h").read_text(encoding="ascii")
        self.assertIn("#define CINT_PHASE_FUEL_K ((int64_t)%d)" % k, text)
        self.assertIn("#define CINT_PHASE_FUEL_C ((int64_t)%d)" % c, text)

    def test_inventory_covers_conformance_and_compiler_in_path_order(self):
        rows = measure.inventory(ROOT)
        names = [top + "/" + rel for top, rel in rows]
        self.assertEqual(names, sorted(names, key=lambda n: n.encode("utf-8")))
        self.assertIn("compiler/main.ci", names)
        self.assertTrue(any(n.startswith("conformance/module/lib/") for n in names))
        self.assertEqual(len(measure.TABLE_NAMES), 17)


if __name__ == "__main__":
    unittest.main()
