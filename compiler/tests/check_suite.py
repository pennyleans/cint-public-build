"""The check part of the `front` suite: compiler/resolve.ci and compiler/check.ci (slice 2 task 2.10c).

The checker takes its tables as views, which cint-harness cannot pass, so this part builds
compiler/tests/check_host.c with the C that cint-seed emits for compiler/tests/front_check.ci
and runs it over a list of sources in one process per configuration. Each source is one
case, judged against `cint_ref` (ref/cint_ref: lexer, parser, and check.check_module, the
same calls `cint_ref run` makes before it executes anything, on the surface of boxes 09
and 12, arenas and pools included):

  - `cint_ref` gives a diagnostic of the source text, scanner or parser: cintc gives the
    same `.expect` lines (`diagnostic.code`, `diagnostic.position`);
  - the module is inside the hello subset of compiler/check.ci (in_subset below, the same
    rules as check.ci's gate, written against `cint_ref`'s tree) and `cint_ref` checks it:
    cintc checks it too;
  - inside the subset, `cint_ref` reports a compile error: cintc's `.expect` lines are
    `cint_ref`'s byte for byte (code, position, and for C6001 every `diagnostic.fault.*`
    line), its C6001 record is `cint_ref`'s canonical compile-time record (SPEC-01 IM-149
    v2), and for C3004, C3011 and the module-level initializer's C6004 its note is the one
    `cint_ref` gives (decision patch D-22): the position of the top-level statement that
    makes the module a script, or none;
  - outside the subset: cintc refuses with C9102 (and `cint_ref` may give anything);
  - `cint_ref`'s parser refuses the text (outside its surface): cintc gives C9102 or a
    diagnostic of its front end, never a checked module.

The sources are every `.ci` file under conformance/ and compiler/, the written modules,
grid, constant and prelude-name cases of check_cases.py, and seeded mutants of the written
modules. A fault inside the compiler (an internal error, CINTC-10) or a sanitizer report
fails the case or the configuration.
"""
import bisect
import hashlib
import json
import pathlib
import re
import subprocess
import sys

import check_cases
import front_ref  # noqa: F401  (puts ref/ on the path)

from cint_ref import check as ref_check  # noqa: E402
from cint_ref import lexer, parser  # noqa: E402
from cint_ref import tree as T  # noqa: E402
from cint_ref.faults import COMPILE_TIME, CompileError, Refused, encode_fault_record  # noqa: E402
from cint_ref.types import INT_TYPES  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
TESTS = ROOT / "compiler" / "tests"
REQUIRES = ["compiler/scan.ci", "compiler/parse.ci", "compiler/fold.ci", "compiler/resolve.ci",
            "compiler/check.ci", "compiler/tests/front_check.ci", "compiler/tests/check_host.c"]
MUTANTS_PER_FILE = 12
MUTANT_SEED = 2103
RECORDS = ("scan_5_Token", "parse_4_Node", "parse_5_Frame", "limits_12_CompilerDiag", "resolve_3_Sem",
           "resolve_3_Sym", "resolve_4_Name")
NOTE_CODES = ("C3004", "C3011", "C6004")
PRELUDE = ref_check.PRELUDE   # cintc's prelude: boxes 09 and 12
# The integer built-ins that check.ci's mark_builtin admits (resolve.ci builtin_id): those of
# box 07 and the result-returning forms of box 12 (`add_result` and the others), which take
# no type argument and no mode.
BUILTIN_SIGS = dict(ref_check.BUILTIN_SIGS)
BUILTIN_SIGS.update({name: ("E", 2, False, False) for name in ref_check.RESULT_BUILTINS})
# The array built-ins of box 09 that check.ci's gate admits (check.ci mark_arrayfn); `fill`
# and `reshape` stay outside (compiler/OPEN.md CINTC-OQ-15), `min` and `max` are BUILTIN_SIGS'.
ARRAY_FNS = frozenset({"len", "extent", "size", "copy", "transpose", "reverse", "sum", "fold_checked", "sum_wrap",
                       "sum_sat", "count", "dot"})
BUILTIN_TYPES = ref_check.BUILTIN_TYPES | ref_check.BOX12_TYPES
# The built-in type names that check.ci's gate refuses, each a scanner class of its own.
# compiler/scan.ci classes only the contextual `round`, so B1 takes `Round` for an ordinary
# type name and gives C3006, as `cint_ref` does (compiler/OPEN.md, contextual names);
# `ArithError` (box 12) is an ordinary name to the scanner, which resolve.ci recognizes by
# its spelling, so the gate admits it.
GATED_TYPES = BUILTIN_TYPES - {"Round", "ArithError"}
BOOT_INTS = ("I8", "I16", "I32", "I64", "U8", "U16", "U32", "U64")
CHECKED_TYPES = BOOT_INTS + ("Bool",)
# Box 12 memory lifetimes: the positions where check.ci's gate admits an arena, pool or handle
# type (mem_admit): a function's parameter, its result or the value of an `AllocError!`
# result, a local variable that is neither a view nor a constant, a struct field, a
# module-level arena or pool that is not exported, and a top-level variable of a script.
MEM_SITES = ("param", "result", "local", "field", "memory", "script")
# The refusals of `cint_ref` for arenas, pools and handles where cintc gives C9102
# (resolve.ci mem_type_ref, check.ci h_member and the dereference).
MEMORY_REFUSALS = ("a capacity outside a module-level arena or pool declaration", "a handle of an array that is not",
                   "an arena or pool of ", "an arena or pool in a kernel body", "a dereference with other than one",
                   "an array field of a pool element", "a member call is outside the surface", "` is not a member of ",
                   " variable without an initializer")


# ------------------------------------------------------------------------------ the subset

class Subset:
    """Whether a module that `cint_ref` parses is inside the hello subset of compiler/check.ci
    (its header and gate): types Bool and the eight integer types I8 to I64 and U8 to U64,
    `Round` and other ordinary names as unknown types (C3006), local arrays and views of
    them, a local array's initializer but no constant local array (compiler/OPEN.md,
    whole-array assignment), nested records and fixed array fields, box 09's arrays and views
    of rank 2 to 4 with scalar elements in a parameter or a local variable, local view
    variables, slices, array literals that initialize a local variable, kernels without an
    `over` range and `reduce`, the array built-ins of ARRAY_FNS, test
    blocks and assert (task 2.14 part (c)), the integer built-ins of BUILTIN_SIGS (box 07:
    a type argument of `muldiv` and a trailing mode written as names are admitted whatever
    they name, and named arguments, which the checker rejects with C2063; box 12: the
    result-returning forms), no import, other built-in, string value but an `in U8` view
    argument, fraction literal, hole conversion, format width past `I64` (which `cint_ref`
    refuses), named arguments only to a constructor, and `T.min` and `T.max` of those
    integer types only. A prelude name the module declares (`declared_names`, check.ci
    `declared`) is inside: its declaration is C3001 (G-C2 review COR-4).

    Box 12 (check.ci gate and top_union): error sets and combined sets, whatever their
    values' names; `E!T` and `E!void` as a function's result; `.name`, `Set.name`, `as?`,
    `defer` and `errdefer`; and `try` and `catch` only as the whole expression of a
    declaration, an assignment with `=`, a `return`, an expression statement or `_ =`
    (`top` below).

    Box 12 memory lifetimes (check.ci gate and mem_admit): `Arena(T)`, `Pool(T)` and
    `Handle(T)` at MEM_SITES (mem_ok), `AllocError`, member calls `name.m(...)` and
    dereferences `name[h]`."""

    def __init__(self, text: str, path: str):
        self.literals = []
        self.structs = set()
        self.declared = set()
        self.funcs = {}
        self.sizes = ()
        self.local_extents = []
        self.nd_extents = []
        self.in_function = True
        try:
            self.literals = [((t.line, t.col), t.text) for t in lexer.tokenize(text, path)
                             if t.kind in ("int", "char", "frac")]
        except CompileError:
            pass

    def type_ok(self, t, where=None) -> bool:
        if t is None:
            return True
        if getattr(t, "inner", None) is not None:
            return self.mem_ok(t, where)
        dims = t.dims or [t.shape]
        if t.shape is not None and len(dims) > 1:
            # Box 09: ranks 2 to 4 with an integer or Bool element, in a parameter or a local
            # variable (check.ci shape_ok); an extent is `_` or an expression.
            if len(dims) > 4 or t.module is not None or t.name not in CHECKED_TYPES or where not in (
                    "local", "view", "param"):
                return False
            for d in dims:
                if d == "_":
                    continue
                if not self.expr(d):
                    return False
                self.nd_extents.append(d)
            return True
        if t.shape is not None and t.module is None and where in ("local", "view", "field", "param"):
            if t.shape == "_":
                return where in ("param", "view")
            if where == "param" and isinstance(t.shape, T.Name) and t.shape.name in self.sizes:
                return True
            if not self.expr(t.shape):
                return False
            if t.name in CHECKED_TYPES or t.name in self.structs or (
                    t.name not in INT_TYPES and t.name not in GATED_TYPES and t.name != "_"):
                self.local_extents.append((t.shape, where == "local" and self.in_function))
                return True
        if t.shape is not None or t.module is not None:
            return False
        if t.name in CHECKED_TYPES:
            return True
        if t.name in self.structs:
            return where != "modvar"
        return t.name not in INT_TYPES and t.name not in GATED_TYPES and t.name != "_"

    def mem_ok(self, t, where) -> bool:
        """check.ci mem_admit: memory type t at a position of MEM_SITES, no handle in an array,
        a capacity inside the subset, and an innermost T that is one unqualified name of an
        integer type, Bool or an ordinary name, with no shape but the `[]` or `[_]` of
        `Handle(E[])`. The checker decides the rest (resolve.ci mem_type_ref): C2051, and
        C9102 where `cint_ref` refuses (MEMORY_REFUSALS)."""
        if where not in MEM_SITES:
            return False
        y, parent = t, None
        while getattr(y, "inner", None) is not None:
            if y.name == "Handle" and y.shape is not None:
                return False
            if getattr(y, "capacity", None) is not None and not self.expr(y.capacity):
                return False
            parent, y = y, y.inner
        if y.module is not None or (y.name not in CHECKED_TYPES and (y.name in INT_TYPES or y.name in GATED_TYPES)):
            return False
        return y.shape is None or (parent.name == "Handle" and (y.dims or [y.shape]) == ["_"])

    def lit_text(self, e):
        at = (e.pos.line, e.pos.column)
        k = bisect.bisect_left(self.literals, (at, ""))
        if k < len(self.literals) and self.literals[k][0] == at:
            return self.literals[k][1]
        return None

    def lit_ok(self, e) -> bool:
        """Not a fraction. Balanced-ternary literals are inside the subset (SPEC-04 LS-30)."""
        return e.kind != "frac"

    def expr(self, e, top=False) -> bool:
        """Whether expression e is inside; with `top`, e may be a `try` or a `catch` (check.ci
        top_union's SEM_TOP)."""
        stack = [e]
        while stack:
            x = stack.pop()
            if x is None:
                continue
            if isinstance(x, (T.Try, T.Catch)):
                if not top or x is not e:
                    return False
                if isinstance(x, T.Try):
                    stack.append(x.operand)
                    continue
                stack += [x.left, x.value]
                if x.block is not None and not self.stmts(x.block.stmts):
                    return False
            elif isinstance(x, T.ErrorLit):
                pass
            elif isinstance(x, T.Lit):
                if not self.lit_ok(x):
                    return False
            elif isinstance(x, (T.BoolLit, T.TypeArg)):
                pass
            elif isinstance(x, T.Name):
                if x.name in PRELUDE and x.name not in self.declared:
                    return False
            elif isinstance(x, T.TypeProp):
                if x.type_name not in BOOT_INTS or x.prop not in ("min", "max"):
                    return False
            elif isinstance(x, T.Unary):
                stack.append(x.operand)
            elif isinstance(x, (T.Binary, T.Compare, T.Logical)):
                stack += [x.left, x.right]
            elif isinstance(x, T.Cond):
                stack += [x.cond, x.a, x.b]
            elif isinstance(x, T.Index):
                stack.append(x.obj)
                for item in x.items:
                    if isinstance(item, T.Slice):
                        stack += [item.lo, item.hi, item.step]   # box 09 (SPEC-04 LS-161)
                    else:
                        stack.append(item)
            elif isinstance(x, T.Field):
                stack.append(x.obj)
            elif isinstance(x, T.Convert):
                if x.op not in ("as", "as%", "as?") or not self.type_ok(x.target):
                    return False
                stack.append(x.operand)
            elif isinstance(x, T.Call) and x.qual is None and x.callee in ARRAY_FNS and x.callee not in self.declared:
                # check.ci mark_arrayfn admits it, its named arguments (C2063) and the
                # operation name of `fold_checked`; the checker decides them.
                for i, a in enumerate(x.args):
                    if not (x.callee == "fold_checked" and i == 0 and a.name is None and isinstance(a.value, T.Name)):
                        stack.append(a.value)
            elif isinstance(x, T.Call) and x.qual is None and x.callee in BUILTIN_SIGS:
                _, _, has_type, has_mode = BUILTIN_SIGS[x.callee]
                for i, a in enumerate(x.args):
                    v = a.value
                    if a.name is None and isinstance(v, (T.Name, T.TypeArg)) and (
                            (has_type and i == 0) or (has_mode and i == len(x.args) - 1)):
                        continue   # check.ci mark_builtin admits it; the checker decides it
                    stack.append(v)
            elif isinstance(x, T.Call) and x.qual is not None:
                # `name.m(...)`, a member call of an arena or a pool (box 12; no import is
                # inside): the checker decides the receiver, the member and named arguments.
                if x.qual in PRELUDE and x.qual not in self.declared:
                    return False
                for a in x.args:
                    if isinstance(a.value, T.StrLit):
                        return False
                    stack.append(a.value)
            elif isinstance(x, T.Call):
                if (x.callee in PRELUDE and x.callee not in self.declared) or (
                        x.callee not in self.structs and any(a.name is not None for a in x.args)):
                    return False
                f = self.funcs.get(x.callee)
                for i, a in enumerate(x.args):
                    if isinstance(a.value, T.StrLit):
                        # A string literal only as an `in U8` view argument (BOOT-02).
                        p = f.params[i] if f is not None and a.name is None and i < len(f.params) else None
                        if p is None or p.mode != "in" or p.type.name != "U8" or p.type.shape is None:
                            return False
                    else:
                        stack.append(a.value)
            else:
                return False
        return True

    @staticmethod
    def place_ok(t) -> bool:
        """Admits field/index chains rooted in a variable."""
        def record_place(value):
            if not isinstance(value, T.Field):
                return False
            while isinstance(value, (T.Field, T.Index)):
                value = value.obj
            return isinstance(value, T.Name)

        if isinstance(t, T.Field):
            return record_place(t)
        if isinstance(t, T.Index) and not isinstance(t.obj, T.Name):
            if record_place(t.obj):
                return True
            root, field = t.obj, False
            while isinstance(root, (T.Index, T.Field)):
                field = field or isinstance(root, T.Field)
                root = root.obj
            return not (field and isinstance(root, T.Name))
        return True

    def stmts(self, stmts) -> bool:
        stack = list(stmts)
        while stack:
            s = stack.pop()
            if s is None:
                continue
            if isinstance(s, T.Block):
                stack += s.stmts
            elif isinstance(s, T.VarDecl):
                if s.type is not None and s.type.shape is not None and s.is_const:
                    return False   # a constant local array: C9102; a variable may have an initializer
                if s.mode is not None:
                    # A local view variable (box 09, SPEC-04 LS-106).
                    if not self.type_ok(s.type, "view") or not self.expr(s.init):
                        return False
                    continue
                if not self.type_ok(s.type, "const" if s.is_const else "local"):
                    return False
                init = [s.init]
                while init:
                    v = init.pop()
                    if isinstance(v, T.ArrayLit):
                        init += v.items   # an array literal, nested ones included (box 09)
                    elif not self.expr(v, v is s.init):   # a `try` or `catch` initializer (box 12)
                        return False
            elif isinstance(s, T.Assign):
                if not self.place_ok(s.target):
                    return False
                if not self.expr(s.target) or not self.expr(s.value, s.op == "="):
                    return False
            elif isinstance(s, T.IncDec):
                if not self.place_ok(s.target):
                    return False
                if not self.expr(s.target):
                    return False
            elif isinstance(s, (T.ExprStmt, T.Discard)):
                if not self.expr(s.expr, True):
                    return False
            elif isinstance(s, T.Print):
                for h in s.parts:
                    if isinstance(h, bytes):
                        continue
                    if h.conv is not None or not self.expr(h.expr):
                        return False
            elif isinstance(s, T.If):
                if not self.expr(s.cond):
                    return False
                stack += [s.then, s.else_]
            elif isinstance(s, T.While):
                if not self.expr(s.cond):
                    return False
                stack.append(s.body)
            elif isinstance(s, T.ForC):
                if not self.expr(s.cond):
                    return False
                stack += [s.init, s.update, s.body]
            elif isinstance(s, T.ForRange):
                if not (self.expr(s.lo) and self.expr(s.hi) and self.expr(s.step)):
                    return False
                stack.append(s.body)
            elif isinstance(s, T.Return):
                if not self.expr(s.value, True):
                    return False
            elif isinstance(s, T.Defer):
                stack.append(s.body)   # `defer` and `errdefer` (box 12)
            elif isinstance(s, T.Switch):
                if not self.expr(s.scrutinee):
                    return False
                for c in s.cases:
                    if not all(self.expr(lo) and self.expr(hi) for lo, hi in c.items):
                        return False
                    stack += c.body
                if s.default is not None:
                    stack += s.default.body
            elif isinstance(s, (T.Break, T.Continue, T.Fallthrough)):
                pass
            elif isinstance(s, T.Reduce):
                if not self.expr(s.call):
                    return False
            elif isinstance(s, T.StaticAssert):
                if not self.expr(s.cond):
                    return False
            elif isinstance(s, T.Assert):
                # Task 2.14 part (c): assert, its message's holes as a print statement's.
                if not self.expr(s.cond):
                    return False
                for h in s.message or []:
                    if isinstance(h, bytes):
                        continue
                    if h.conv is not None or not self.expr(h.expr):
                        return False
            else:
                return False
        return True

    def module(self, m) -> bool:
        if m.imports:
            return False
        self.declared = declared_names(m)
        self.structs = {s.name for s in m.structs}
        self.funcs = {}
        for f in m.functions:
            self.funcs.setdefault(f.name, f)
        for s in m.structs:
            for fl in s.fields:
                ft = fl.type
                if getattr(ft, "inner", None) is not None:
                    if not self.mem_ok(ft, "field"):
                        return False
                    continue
                if ft.name not in CHECKED_TYPES and ft.name not in self.structs:
                    continue
                if ft.module is not None:
                    return False   # Exercises imported fields through the multi-module driver.
                if not self.type_ok(ft, "field"):
                    return False
        for e in m.errors:
            # The values of a set may have any name; a combined set's operands are type names.
            if e.operands is not None and not all(self.type_ok(t) for t in e.operands):
                return False
        for d in m.memories:
            # A module-level arena or pool (box 12), not exported.
            if getattr(d, "exported", False) or not self.mem_ok(d.type, "memory") or not self.expr(d.init):
                return False
        script = any(not isinstance(s, T.VarDecl) for s in m.statements)
        for s in list(m.consts) + list(m.statements):
            if isinstance(s, T.VarDecl) and getattr(s.type, "inner", None) is not None and (
                    not script or s.is_const or getattr(s, "exported", False)):
                return False   # a memory type at module level, other than a script's variable
        for f in m.functions:
            self.sizes = tuple(n for n, _ in f.size_params)
            result = f.result
            site = "result"
            if isinstance(result, T.UnionType):
                # `E!T` or `E!void`, admitted as a function's result (box 12); a memory type
                # as T only after `AllocError`.
                if not self.type_ok(result.set):
                    return False
                e = result.set
                if not (e.name == "AllocError" and e.module is None and e.shape is None and
                        getattr(e, "inner", None) is None):
                    site = None
                result = result.value
            if result is not None and result.shape is not None:
                return False
            if not self.type_ok(result, site) or not all(self.type_ok(p.type, "param") for p in f.params):
                return False
            if any(p.mode == "out" for p in f.params):
                return False   # `out` is a kernel's parameter mode (check.ci gate)
            if not self.stmts(f.body.stmts):
                return False
        self.in_function = False
        for k in m.kernels:
            # Box 09 (SPEC-02 K-1): an `over` extent that is a range stays outside.
            self.sizes = tuple(n for n, _ in k.size_params)
            if any(getattr(p.type, "inner", None) is not None for p in k.params):
                return False   # a kernel's parameter is no memory type (check.ci gate)
            if not all(self.type_ok(p.type, "param") for p in k.params):
                return False
            if k.over is not None and not all(self.expr(e) for _, _, e in k.over):
                return False
            if not all(self.expr(c) for c in k.where) or not self.stmts(k.body.stmts):
                return False
        self.sizes = ()
        if any(s.type is not None and (s.type.shape is not None or s.type.name in self.structs)
               for s in list(m.consts) + list(m.statements) if isinstance(s, T.VarDecl)):
            return False   # arrays and structs at module level or in a script: C9102 (task 2.13)
        self.in_function = True
        if not all(self.stmts(t.body.stmts) for t in m.tests):
            return False   # test blocks are in the subset since task 2.14 part (c)
        self.in_function = False
        return self.stmts(list(m.consts) + list(m.statements) + list(m.static_asserts))


def declared_names(module) -> set:
    """The names check.ci's gate counts as declared (`declared`): every function, parameter,
    size parameter, variable, constant, loop variable and struct of the module."""
    names, stack, seen = set(), [module], set()
    while stack:
        x = stack.pop()
        if id(x) in seen:
            continue
        seen.add(id(x))
        if isinstance(x, (list, tuple)):
            stack += list(x)
            continue
        if type(x).__module__ != T.__name__:
            continue
        if isinstance(x, (T.FuncDecl, T.Param, T.VarDecl, T.StructDecl)):
            names.add(x.name)
        if isinstance(x, T.FuncDecl):
            names.update(n for n, _ in x.size_params)
        elif isinstance(x, T.ForRange):
            names.add(x.var)
        stack += [v for v in vars(x).values() if isinstance(v, (list, tuple)) or type(v).__module__ == T.__name__]
    return names


# ------------------------------------------------------------------------------ cint_ref

PRELUDE_REFUSAL = re.compile(r"the built-in `(\w+)` is outside the scalar surface")
NOTE_RE = re.compile(r"note: this module is a script because of the top-level statement at .*:(\d+):(\d+): `")


def note_of(d) -> list:
    """[line, column, kind] of the D-22 note of diagnostic d, as compiler/resolve.ci stores it."""
    for n in d.notes:
        m = NOTE_RE.match(n)
        if m:
            return [int(m.group(1)), int(m.group(2)), 1]
        if n.startswith("note: this file is a module, not a script"):
            return [0, 0, 2]
    return [0, 0, 0]


def outcome(data: bytes, path: str) -> dict:
    """`cint_ref`'s outcome for one module at module path `path`."""
    old = sys.getrecursionlimit()
    sys.setrecursionlimit(max(old, 200000))
    try:
        try:
            text = lexer.decode_source(data, path)
            module = parser.parse_module(text, path, box09=True, box12=True)   # cintc's surface: boxes 09 and 12
        except CompileError as e:
            return {"kind": "front", "lines": e.diagnostic.to_expect_lines()}
        except Refused:
            return {"kind": "refused"}
        subset = Subset(text, path)
        if not subset.module(module):
            return {"kind": "out"}
        try:
            if ref_check.module_path_rule(module.path) is None:
                ref_check.check_module(module)
            else:
                # C3030 belongs to the module paths of the plan stage (SPEC-09 CINTC-12), not to
                # check.ci: the module itself is checked as check_program would after it, C5012
                # (LS-122) included.
                checker = ref_check.Checker(module, {})
                checker.check()
                ref_check._check_module_state_args([checker])
        except CompileError as e:
            if wide_mul_full(module):
                return {"kind": "out"}
            d = e.diagnostic
            want = {"kind": "error", "lines": d.to_expect_lines(), "code": d.code}
            if d.code in NOTE_CODES:
                want["note"] = note_of(d)
            if d.fault is not None:
                want["record"] = encode_fault_record(d.record(), COMPILE_TIME).hex()
            return want
        except Refused as e:
            # check.ci gives C9102 at a format width past I64, after the rules of LS-213.
            if e.message == "a negative extent: no clause gives its diagnostic" or e.message.startswith(
                    ("`mul_full` on U64", "a format width of 2^63")):
                return {"kind": "out"}
            if any(r in e.message for r in MEMORY_REFUSALS):
                return {"kind": "out"}   # box 12: resolve.ci mem_type_ref and check.ci h_member give C9102
            if e.message.startswith("clamp with lo > hi at run time"):
                return {"kind": "open"}   # OQ-06: cintc decides E_DOMAIN, so C6006 can come first
            m = PRELUDE_REFUSAL.match(e.message)
            if m and m.group(1) in subset.declared:
                return {"kind": "out"}   # declared elsewhere, so past the gate: resolve.lookup's C9102
            return {"kind": "ref-refused", "why": e.message}
        if wide_mul_full(module):
            return {"kind": "out"}
        if any(e.const is not None and e.const.value > (2 ** 32 if big else 65534)
               for e, big in subset.local_extents) or any(
                e.const is not None and e.const.value > 3070 for e in subset.nd_extents):
            # resolve.ARRAY_EXTENT_MAX at rank 1, BIG_EXTENT_MAX for a function's local array
            # (decision 2026-10-06 on OQ-213), NARRAY_EXTENT_MAX above
            return {"kind": "out"}
        return {"kind": "ok"}
    finally:
        sys.setrecursionlimit(old)


def wide_mul_full(module) -> bool:
    """Whether `cint_ref` typed a `mul_full` of a 64-bit operand type: its 128-bit result is
    outside cintc, which refuses it with C9102 where `cint_ref` goes on."""
    stack, seen = [module], set()
    while stack:
        x = stack.pop()
        if id(x) in seen:
            continue
        seen.add(id(x))
        if isinstance(x, (list, tuple)):
            stack += list(x)
        elif type(x).__module__ == T.__name__:
            if isinstance(x, T.Call) and x.callee == "mul_full" and getattr(x, "operand_type", None) in ("I64", "U64"):
                return True
            stack += [v for v in vars(x).values() if isinstance(v, (list, tuple)) or type(v).__module__ == T.__name__]
    return False


def module_path(name: str) -> str:
    """The module path `cint_ref` gives a frozen file (relative to conformance/), else case.ci."""
    if name.startswith("conformance/"):
        return name.split("/", 1)[1]
    return "case.ci"


# ------------------------------------------------------------------------------ the part

def prepare(gen: pathlib.Path) -> list:
    """The sources, written under gen, with their expected outcomes, computed once."""
    import front_cases
    gen.mkdir(parents=True, exist_ok=True)
    sources = []

    def add(name, data):
        path = gen / (hashlib.sha256(name.encode("utf-8")).hexdigest()[:20] + ".ci")
        if not path.exists() or path.read_bytes() != data:
            path.write_bytes(data)
        mp = module_path(name)
        want = outcome(data, mp)
        if check_cases.beyond_ls311(name):
            want["c9004"] = True
        sources.append((name, path, mp, want))

    for name, data in front_cases.frozen():
        add(name, data)
    written = check_cases.written() + check_cases.grid() + check_cases.consts() + check_cases.widths()
    written += check_cases.nesting() + check_cases.prelude_names()
    for name, data in written:
        add(name, data)
    for name, data in check_cases.mutants(MUTANTS_PER_FILE, MUTANT_SEED, check_cases.written()):
        add(name, data)
    return sources


def records(c_text: str) -> list:
    m = re.search(r"cg_records\[\d+\] = \{\n(.*?)\n\};", c_text, re.S)
    names = re.findall(r"sizeof\((ci_\w+)\)\)", m.group(1)) if m else []
    ids = []
    for key in RECORDS:
        found = [i for i, nm in enumerate(names) if nm.endswith("_" + key)]
        if len(found) != 1:
            raise ValueError("record %s not found in the emitted C" % key)
        ids.append(str(found[0]))
    return ids


def parse_output(stdout: bytes) -> dict:
    blocks, current = {}, None
    for line in stdout.decode("utf-8", "replace").split("\n"):
        if line.startswith("file "):
            current = blocks.setdefault(line[5:], [])
        elif line and current is not None:
            current.append(line)
    return blocks


def judge(want: dict, got) -> str:
    """'' when the host's lines `got` meet `want`; otherwise what differs."""
    if got is None:
        return "no output"
    faults = [x for x in got if x.startswith("fault")]
    if faults:
        return "internal error: " + faults[0]
    status = got[0] if got else ""
    stats = next((x.split()[1:] for x in got if x.startswith("stats ")), ["0"])
    phase = int(stats[0])
    lines = [x[5:] for x in got if x.startswith("line ")]
    diags = [x.split()[1:] for x in got if x.startswith("diag ")]
    record = next((x[7:] for x in got if x.startswith("record ")), None)
    kind = want["kind"]
    if kind == "ok":
        if want.get("c9004") and phase in (1, 2, 3) and status == "status 0 1" and                 lines[:1] == ["diagnostic.code C9004"]:
            return ""   # beyond LS-311's 256 levels: cintc's own nesting limit (check_cases.beyond_ls311)
        if phase != 7 or status != "status 0 0":
            return "want no diagnostic, got %s" % (lines or status)
        return ""
    if kind == "ref-refused":
        return "cint_ref refuses a module inside the subset: %s" % want["why"]
    if kind == "open":
        return 
    if kind == "refused":
        if phase in (1, 2, 3, 5) and status == "status 0 1":
            return ""
        return "cint_ref's parser refuses it: want C9102 or a front-end diagnostic, got %s" % (lines or status)
    if kind == "out":
        if phase == 5 and lines and lines[0] == "diagnostic.code C9102":
            return ""
        return "outside the subset: want C9102, got %s" % (lines or status)
    if want.get("c9004") and phase in (1, 2, 3) and status == "status 0 1" and lines[:1] == ["diagnostic.code C9004"]:
        return ""   # beyond LS-311's 256 levels: cintc's own nesting limit (check_cases.beyond_ls311)
    if status != "status 0 1" or lines != want["lines"]:
        return "want %s, got %s" % (want["lines"], lines or status)
    if kind == "front":
        return ""
    if "note" in want and diags and [int(v) for v in diags[0][3:6]] != want["note"]:
        return "want note %s, got %s" % (want["note"], diags[0][3:6])
    if "record" in want and record != want["record"]:
        return "fault record differs: want %s, got %s" % (want["record"], record)
    return ""


def run(tc, tools, work: pathlib.Path, emitted: dict, sources: list, cc) -> tuple:
    """Builds the host for configuration tc and runs every source. (passed, total, failures)."""
    total = len(sources)
    c_file = work / "front_check.c"
    code, err = cc.run_seed(tools["seed"], ROOT / "compiler", "tests/front_check.ci", c_file)
    if code != 0 or cc.sanitizer_reports(err):
        return 0, total, ["seed: exit %d: %s" % (code, " ".join(err.split())[:400])]
    text = c_file.read_bytes()
    digest = hashlib.sha256(text).hexdigest()
    if b"\r" in text:
        return 0, total, ["emitted C holds a CR byte"]
    if emitted.setdefault("tests/front_check.ci", digest) != digest:
        return 0, total, ["emitted C differs from the first configuration's"]
    try:
        ids = records(text.decode("ascii"))
        objs = tc.compile([c_file, TESTS / "check_host.c"], work, "front_check")
        exe = tc.link_exe(objs + [tools["rt_obj"]], work / "check-host", "front_check")
    except (cc.BuildError, ValueError) as e:
        return 0, total, ["build: %s" % " ".join(str(e).split())[:600]]
    listing = work / "check.list"
    listing.write_bytes("".join("%s\t%s\n" % (p, mp) for _, p, mp, _ in sources).encode("utf-8"))
    env = dict(tc.env)
    env["ASAN_OPTIONS"] = "detect_leaks=1:abort_on_error=0"
    env["UBSAN_OPTIONS"] = "print_stacktrace=1:halt_on_error=1"
    r = subprocess.run([str(exe), *ids, str(listing)], cwd=work, env=env, capture_output=True)
    stderr = r.stderr.decode("utf-8", "replace")
    if r.returncode != 0 or cc.sanitizer_reports(stderr):
        return 0, total, ["host exit %d: %s" % (r.returncode, " ".join(stderr.split())[:400])]
    blocks = parse_output(r.stdout)
    (work / "check.stdout").write_bytes(r.stdout)
    depths = {}
    for name, path, _, _ in sources:
        if name.startswith("switch/arms_"):
            stats = next((x.split()[1:] for x in blocks.get(str(path), []) if x.startswith("stats ")), [])
            if len(stats) == 8:
                depths[name] = int(stats[5])
    (work / "switch-depths.json").write_text(json.dumps(depths, sort_keys=True, indent=2) + "\n", encoding="ascii")
    passed, failures = 0, []
    for name, path, _, want in sources:
        problem = judge(want, blocks.get(str(path)))
        if not problem and name.startswith("switch/arms_1000_") and name in depths:
            baseline = "switch/arms_1_" + ("reverse" if name.endswith("reverse") else "ordered")
            if baseline in depths and depths[name] > depths[baseline]:
                problem = "switch traversal depth %d exceeds one-arm depth %d" % (depths[name], depths[baseline])
        if problem:
            failures.append("%s: %s" % (name, problem))
        else:
            passed += 1
    return passed, total, failures
