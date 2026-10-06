"""The program symbol audit of tools/cint_check.py: SPEC-09 EMIT-31 and SPEC-03 A-3 as amended by
slice 2 decision patch D-2 (exports `cx_*`, `cm_*`, `cint_program_abi`, `cint_observer_desc`;
external, non-exported `ci_*` and `cg_*` of per-module output), with the Apple Clang target row of
decision 25 (`__chkstk_darwin` on the apple-clang leg only), the `bzero` that the ruling of
2026-10-05 adds to that row (G-C1, rt/OPEN.md RT-OQ-32), the MSVC `/GS` range check
`__report_rangecheckfailure` and the MSVC C runtime's `__isa_available` on the msvc leg only
(OQ-211, OQ-215), and MSVC's `__isa_available_default` among the toolchain names (OQ-215).
Python standard library only; no toolchain is needed."""
import pathlib
import sys
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from cint_check import Toolchain, audit_coverage, audit_program, norm  # noqa: E402

RT = {"cint_rt_entry_begin", "cint_rt_entry_end", "cint_view_bind", "cint_fault_shape", "cint_ctx_create"}
SEED_OBJECT = ({"cx_3_add_4_add3", "cm_3_add", "cint_program_abi", "cint_observer_desc"},
               {"cint_rt_entry_begin", "cint_view_bind", "memcpy", "__chkstk"})


class Audit(unittest.TestCase):
    def test_seed_program_passes(self):
        exports = {"cx_3_add_4_add3", "cm_3_add", "cint_program_abi", "cint_observer_desc"}
        self.assertEqual(audit_program([SEED_OBJECT], exports, RT, "gcc"), [])

    def test_linked_runtime_exports_pass(self):
        # An ELF shared object exports the runtime functions linked into it.
        exports = {"cx_3_add_4_add3", "cint_observer_desc", "cint_rt_entry_begin", "cint_ctx_create"}
        self.assertEqual(audit_program([SEED_OBJECT], exports, RT, "gcc"), [])

    def test_retired_and_foreign_names_fail(self):
        obj = ({"cint_program_desc", "cint_entry_add", "helper"}, {"printf", "cint_unknown"})
        exports = {"cint_entries", "ci_3_add_4_add3"}
        self.assertEqual(audit_program([obj], exports, RT, "gcc"), [
            "defines cint_entry_add", "defines cint_program_desc", "defines helper",
            "refers to cint_unknown", "refers to printf",
            "exports ci_3_add_4_add3", "exports cint_entries"])

    def test_per_module_objects(self):
        # cintc output: one object per module and a program-level object (CONF-13 X-4).
        mod_a = ({"ci_1_a_1_f", "cg_1_a_obs", "cg_1_a_sites", "cx_1_a_1_f", "cm_1_a"}, {"ci_1_b_1_g"})
        mod_b = ({"ci_1_b_1_g", "cg_1_b_obs", "cg_1_b_sites"}, set())
        prog = ({"cint_observer_desc", "cint_program_abi"}, {"cg_1_a_obs", "cg_1_b_obs", "cg_1_a_sites"})
        exports = {"cx_1_a_1_f", "cm_1_a", "cint_observer_desc", "cint_program_abi"}
        self.assertEqual(audit_program([mod_a, mod_b, prog], exports, RT, "gcc"), [])
        # An import that no object of the program defines is an undefined symbol outside the list.
        # The cross-module ci_ rule (task 2.13 part (iii)) also names the missing definer.
        self.assertEqual(audit_program([mod_a, prog], exports, RT, "gcc"), [
            "refers to ci_1_b_1_g", "refers to cg_1_b_obs",
            "refers to ci_1_b_1_g, which no single object of its module path defines"])
        # Internal symbols are never exported, and a runtime name defined by the program is not the runtime's.
        self.assertEqual(audit_program([mod_a, mod_b, prog], exports | {"cg_1_a_obs"}, RT, "gcc"), ["exports cg_1_a_obs"])


    def test_apple_stack_probe_on_the_apple_target_only(self):
        # Decision 25: Apple Clang's large-frame probe, as norm() leaves it, passes on apple-clang.
        exports = {"cx_3_add_4_add3", "cm_3_add", "cint_program_abi", "cint_observer_desc"}
        obj = (SEED_OBJECT[0], SEED_OBJECT[1] | {"__chkstk_darwin"})
        self.assertEqual(audit_program([obj], exports, RT, "apple-clang"), [])
        for leg in ("msvc", "gcc", "clang"):
            self.assertEqual(audit_program([obj], exports, RT, leg), ["refers to __chkstk_darwin"])

    def test_apple_target_still_rejects_unknown_symbols(self):
        exports = {"cx_3_add_4_add3", "cint_observer_desc"}
        # The raw Mach-O spelling is not the normalized name, and an unrelated symbol still fails.
        obj = (SEED_OBJECT[0], SEED_OBJECT[1] | {"___chkstk_darwin", "__chkstk_darwin", "printf"})
        self.assertEqual(audit_program([obj], exports, RT, "apple-clang"),
                         ["refers to ___chkstk_darwin", "refers to printf"])
        # The allowance is for an undefined reference only; a program may not define the probe.
        obj = (SEED_OBJECT[0] | {"__chkstk_darwin"}, SEED_OBJECT[1])
        self.assertEqual(audit_program([obj], exports, RT, "apple-clang"), ["defines __chkstk_darwin"])

    def test_apple_bzero_on_the_apple_target_only(self):
        # Ruling 2026-10-05 (G-C1): Apple Clang at -O0 zeroes the local `int64_t l0_a[512] = {0};`
        # of boot/const_extent_limit with bzero. The name passes on apple-clang only.
        exports = {"cx_3_add_4_add3", "cm_3_add", "cint_program_abi", "cint_observer_desc"}
        obj = (SEED_OBJECT[0], SEED_OBJECT[1] | {"bzero"})
        self.assertEqual(audit_program([obj], exports, RT, "apple-clang"), [])
        for leg in ("msvc", "gcc", "clang"):
            self.assertEqual(audit_program([obj], exports, RT, leg), ["refers to bzero"])
        # Both rows of the target pass together, as in one -O0 object with a large frame.
        obj = (SEED_OBJECT[0], SEED_OBJECT[1] | {"bzero", "__chkstk_darwin"})
        self.assertEqual(audit_program([obj], exports, RT, "apple-clang"), [])

    def test_apple_bzero_allowance_is_exact(self):
        exports = {"cx_3_add_4_add3", "cint_observer_desc"}
        # The raw Mach-O spelling, Darwin's internal spelling and a neighboring legacy name fail.
        obj = (SEED_OBJECT[0], SEED_OBJECT[1] | {"_bzero", "__bzero", "bcopy", "bzero"})
        self.assertEqual(audit_program([obj], exports, RT, "apple-clang"),
                         ["refers to __bzero", "refers to _bzero", "refers to bcopy"])
        # The allowance is for an undefined reference only; a program may not define it.
        obj = (SEED_OBJECT[0] | {"bzero"}, SEED_OBJECT[1])
        self.assertEqual(audit_program([obj], exports, RT, "apple-clang"), ["defines bzero"])

    def test_msvc_range_check_on_the_msvc_target_only(self):
        # OQ-211: MSVC at /Od guards B1's zero store `l[z] = 0;` into a local array of 1-byte
        # elements with its /GS range check. The name passes on msvc only.
        exports = {"cx_3_add_4_add3", "cm_3_add", "cint_program_abi", "cint_observer_desc"}
        obj = (SEED_OBJECT[0], SEED_OBJECT[1] | {"__report_rangecheckfailure"})
        self.assertEqual(audit_program([obj], exports, RT, "msvc"), [])
        for leg in ("apple-clang", "gcc", "clang"):
            self.assertEqual(audit_program([obj], exports, RT, leg),
                             ["refers to __report_rangecheckfailure"])
        # The allowance is for an undefined reference only; a program may not define it, and a
        # neighboring /GS name stays a finding.
        obj = (SEED_OBJECT[0] | {"__report_rangecheckfailure"}, SEED_OBJECT[1] | {"__report_gsfailure"})
        self.assertEqual(audit_program([obj], exports, RT, "msvc"),
                         ["defines __report_rangecheckfailure", "refers to __report_gsfailure"])

    def test_msvc_isa_level_on_the_msvc_target_only(self):
        # OQ-215: MSVC at /O2 refers to its C runtime's CPU feature level from the object of box
        # 09's reduce/dot_value. The name passes on msvc only.
        exports = {"cx_3_add_4_add3", "cm_3_add", "cint_program_abi", "cint_observer_desc"}
        obj = (SEED_OBJECT[0], SEED_OBJECT[1] | {"__isa_available"})
        self.assertEqual(audit_program([obj], exports, RT, "msvc"), [])
        for leg in ("apple-clang", "gcc", "clang"):
            self.assertEqual(audit_program([obj], exports, RT, leg), ["refers to __isa_available"])
        # The allowance is for an undefined reference only; a program may not define it, and a
        # neighboring C runtime name stays a finding.
        obj = (SEED_OBJECT[0] | {"__isa_available"}, SEED_OBJECT[1] | {"__isa_enabled", "__favor"})
        self.assertEqual(audit_program([obj], exports, RT, "msvc"),
                         ["defines __isa_available", "refers to __favor", "refers to __isa_enabled"])

    def test_msvc_isa_default_is_a_toolchain_name(self):
        # OQ-215: the object of reduce/dot_value at MSVC /O2 defines __isa_available_default and
        # refers to __isa_available. The first is excluded as a toolchain name, by exact name, on
        # the msvc leg (the only leg that reads dumpbin); a longer name stays a finding.
        dump = "\n".join([
            "008 00000000 SECT3  notype ()    External     | cx_3_add_4_add3",
            "009 00000000 SECT4  notype       External     | cm_3_add",
            "00A 00000000 SECT5  notype       External     | __isa_available_default",
            "00B 00000000 SECT6  notype       External     | __isa_available_default2",
            "00C 00000000 UNDEF  notype       External     | __isa_available",
            "00D 00000000 UNDEF  notype ()    External     | cint_view_bind"])
        tc = object.__new__(Toolchain)
        tc.leg, tc.dumpbin = "msvc", "dumpbin.exe"
        tc.run = mock.Mock(return_value=mock.Mock(stdout=dump))
        defined, undefined = tc.symbols(pathlib.Path("prog.obj"))
        self.assertEqual(defined, {"cx_3_add_4_add3", "cm_3_add", "__isa_available_default2"})
        self.assertEqual(undefined, {"__isa_available", "cint_view_bind"})
        exports = {"cx_3_add_4_add3", "cm_3_add"}
        self.assertEqual(audit_program([(defined, undefined)], exports, RT, "msvc"),
                         ["defines __isa_available_default2"])
        # On another leg the same set is two findings: the name is excluded by dumpbin's reader
        # only, and the reference is in the msvc row only.
        self.assertEqual(audit_program([(defined | {"__isa_available_default"}, undefined)],
                                       exports, RT, "gcc"),
                         ["defines __isa_available_default", "defines __isa_available_default2",
                          "refers to __isa_available"])

    def test_mach_o_normalization_removes_one_underscore(self):
        with mock.patch.object(sys, "platform", "darwin"):
            self.assertEqual(norm("___chkstk_darwin"), "__chkstk_darwin")
            self.assertEqual(norm("_memcpy"), "memcpy")
            self.assertEqual(norm("_bzero"), "bzero")
        with mock.patch.object(sys, "platform", "linux"):
            self.assertEqual(norm("__chkstk_darwin"), "__chkstk_darwin")

    def test_msvc_pooled_constants(self):
        # MSVC /O2 pools string literals as ??_C@ and vector constants as __xmm@, __ymm@, __zmm@
        # (COMDAT externals the toolchain makes); a pooled floating-point constant __real@ is
        # not excluded and stays a finding (task 2.13, module/import_plain at MSVC /O2).
        dump = "\n".join([
            "008 00000000 SECT3  notype ()    External     | cx_3_add_4_add3",
            "009 00000000 SECT4  notype       External     | cm_3_add",
            "00A 00000000 SECT5  notype       External     | ??_C@_05CJBACGMB@hello?$AA@",
            "00B 00000000 SECT6  notype       External     | __xmm@00000000000000040000000000000003",
            "00C 00000000 SECT7  notype       External     | __ymm@" + "0" * 64,
            "00D 00000000 SECT8  notype       External     | __zmm@" + "0" * 128,
            "00E 00000000 SECT9  notype       External     | __real@3ff0000000000000",
            "00F 00000000 UNDEF  notype ()    External     | cint_view_bind",
            "010 00000000 UNDEF  notype       External     | __xmm@00000000000000000000000000000001",
            "011 00000000 SECT3  notype       Static       | $LN3"])
        tc = object.__new__(Toolchain)
        tc.leg, tc.dumpbin = "msvc", "dumpbin.exe"
        tc.run = mock.Mock(return_value=mock.Mock(stdout=dump))
        defined, undefined = tc.symbols(pathlib.Path("prog.obj"))
        self.assertEqual(defined, {"cx_3_add_4_add3", "cm_3_add", "__real@3ff0000000000000"})
        self.assertEqual(undefined, {"cint_view_bind"})
        exports = {"cx_3_add_4_add3", "cm_3_add"}
        self.assertEqual(audit_program([(defined, undefined)], exports, RT, "msvc"),
                         ["defines __real@3ff0000000000000"])


class Coverage(unittest.TestCase):
    """program_objects counts the libraries audited, not those with a non-empty export table, and
    entry_programs names the emitted entry programs the audit does not cover (CINTC-OQ-36)."""

    @staticmethod
    def ran(name, exports=0, audited=False, entry=False, emitted=True):
        return {"name": name, "exports": exports, "audited": audited, "entry": entry,
                "emitted_sha256": "0" * 64 if emitted else None}

    def test_every_audited_library_counts_and_entry_programs_are_named(self):
        ran = [self.ran("arith/add", 4, True), self.ran("arith/none", 0, True),
               self.ran("control/print_no_newline_then_fault", entry=True),
               self.ran("control/print_hole_fault_partial", entry=True),
               self.ran("diag/refused", emitted=False), self.ran("view/build_failed")]
        self.assertEqual(audit_coverage(ran), {
            "program_exports": 4, "program_objects": 2,
            "entry_programs": ["control/print_hole_fault_partial",
                               "control/print_no_newline_then_fault"]})

    def test_a_build_failure_is_neither_audited_nor_an_entry(self):
        ran = [self.ran("a", 3, True), self.ran("b")]
        cover = audit_coverage(ran)
        self.assertLess(cover["program_objects"] + len(cover["entry_programs"]),
                        sum(1 for r in ran if r["emitted_sha256"]))


if __name__ == "__main__":
    unittest.main()
