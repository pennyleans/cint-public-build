"""Static checks: names, types, literal context typing and constant expressions.

Implements SPEC-01 3.2 (literals take their type from context, then constant
expressions are typed first and evaluated with the run-time rules) and the
static rules of SPEC-04 that the scalar surface needs. A literal out of range
of its type is C2003; a fault during constant evaluation is C6001 carrying
the fault. Every diagnostic carries a code of the SPEC-04 17.2 table (LS-313,
decision 2026-10-03, slice 2 patch D-4) and the position that table
gives (D-11). Constant expressions follow D-9 (SPEC-01 IM-23, SPEC-04 LS-264):
a constant operand that `&&`, `||` or `?:` of a non-constant expression may
skip is not evaluated at compile time, and inside a constant expression a
skipped operand is type-checked and range-checked but not evaluated; a
checked conversion of a literal is decided at compile time wherever it
appears (D-17).

Slice 2 task 2.8 (D-15 rows 6 to 8) adds rank-1 arrays and views (SPEC-04
4.3, 6.1, 6.2, 7.6): owned arrays with a constant extent, or in a function
body an `I64` expression of size parameters; `in` and `inout` view
parameters `T[n]`, `T[_]` and `T[k]`; size parameters; indexing; whole-array
copies; the static parts of the entry checks (C2012, C5010, C2065, C2067,
C5012); and programs of several modules joined by `import` (SPEC-04 section
11), checked in the depth-first post-order of their imports.

Box 09 (docs/design/notes/2026-10-05-box09-arrays-views-kernels.md, rulings
R1 to R10) adds arrays of rank 2 to 4, partial indexing, slices, array
literals, local view variables, the view functions `transpose`, `reverse` and
`reshape`, the reductions, `dot`, `copy`, `fill`, `extent` and `size` (SPEC-04
LS-145), and kernels with their static rules (SPEC-02 K-1 to K-12; SPEC-04
LS-244 to LS-251). A rule that no clause gives a diagnostic for is refused, not
guessed. A module parsed with `box09=False` (parser.parse_module) is checked as
before box 09, where the four built-in names box 09 adds are ordinary names.

Box 12 (docs/design/notes/2026-10-05-box12-errors-cleanup-lifetimes.md) adds
error sets and combined sets (SPEC-04 LS-92, LS-93, LS-97), error-set values as
ordinary values (LS-314), error unions as function results with `try`, `catch`
and `_ =` (LS-94 to LS-96, LS-129 to LS-132), `as?` and the result-returning
built-ins over the built-in set `ArithError` (SPEC-01 IM-30, IM-188), and
`defer` and `errdefer` with their static rules (LS-185 to LS-189, LS-316 to
LS-322). A module parsed with `box12=False` is checked as before box 12, where
`ArithError` is an ordinary name.

Box 12's memory lifetimes group adds arenas and pools (SPEC-03 M-12 to M-20; the
readings of ref/OPEN.md REF-OQ-45): module-level `Arena(T, capacity) name;` and
`Pool(T, capacity) name;` with an `I64` constant capacity of at least 0 (C6004,
C6006; C4012 inside a function), `Arena(T)` and `Pool(T)` values as locals and
parameters only (C2051 elsewhere), `Handle(T)` for a pool slot and `Handle(T[])`
for an arena allocation, the members `alloc`, `alloc_result`, `child`, `reset`,
`insert`, `insert_result` and `remove` (SPEC-04 LS-145) over the built-in set
`AllocError`, and the dereferences `p[h]` and `a[h]`. A module parsed with
`memory=False` is checked as before them.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from . import fmt, views
from . import parser as P
from .arith import ROUND_MODES
from .faults import CompileError, Diagnostic, Fault, FaultSignal, Position, Refused, error
from .types import BOOL, INT_TYPES, int_type, is_int_type

VOID = "void"
Z = "Z"

# Built-in function names of SPEC-04 6.8 (the implicit prelude); they must not be redefined.
PRELUDE = frozenset("""
len extent size lower copy fill equal swap abs uabs min max clamp div_trunc rem_trunc
div_euclid rem_euclid divmod div_round muldiv mul_full isqrt isqrt_round add_result
sub_result mul_result div_result rem_result shl_result rotl rotr mul div mul_wrap mul_sat
sqrt sum fold_checked sum_wrap sum_sat count wrap_bits rescale3 rescale3_rem shl3 sign
tdot random format utf8 embed to_device to_host dot transpose reverse reshape
""".split())
# The names box 09 adds to the prelude (rulings R4 and R6); a module parsed without the
# box 09 surface may still declare them.
BOX09_PRELUDE = frozenset({"dot", "transpose", "reverse", "reshape"})
BUILTIN_TYPES = frozenset({"Bool", "Str", "Round", "T1", "T27", "PT5", "PT4", "Arena", "Pool", "Handle"})
# The built-in error set of SPEC-01 IM-188, a type name with the box 12 surface (SPEC-04 LS-21).
BOX12_TYPES = frozenset({"ArithError"})
# The built-in error set of SPEC-03 M-19, a type name with the arena and pool surface (SPEC-04 LS-21).
MEMORY_SETS = frozenset({"AllocError"})
# The members of an arena and of a pool (SPEC-04 LS-145, spelling Proposed): argument count.
ARENA_MEMBERS = {"alloc": 1, "alloc_result": 1, "child": 1, "reset": 0}
POOL_MEMBERS = {"insert": 1, "insert_result": 1, "remove": 1}
# The result-returning built-ins (SPEC-01 IM-28, IM-30) and the operation of each.
RESULT_BUILTINS = {"add_result": "add", "sub_result": "sub", "mul_result": "mul", "div_result": "div",
                   "rem_result": "rem", "shl_result": "shl"}
# The reductions of SPEC-04 LS-145; `min` and `max` are reductions when their last argument is an array.
REDUCTIONS = frozenset({"sum", "fold_checked", "sum_wrap", "sum_sat", "count", "dot"})
# The reductions a `reduce` statement names (SPEC-02 R-1, SPEC-04 LS-249).
REDUCE_OPS = frozenset({"sum", "fold_checked", "sum_wrap", "sum_sat", "min", "max", "count"})
VIEW_FUNCTIONS = frozenset({"transpose", "reverse", "reshape"})
MAX_RANK = 4                 # SPEC-02 V-2: rank 1 to 4 in cint-core-1
KERNEL_PRIVATE_BYTES = 4096  # SPEC-02 K-13, SPEC-04 LS-110

# Scalar named operations implemented by cint_ref: result kind, value-argument count,
# leading type argument, trailing mode argument.
BUILTIN_SIGS = {
    "abs": ("T", 1, False, False), "uabs": ("U", 1, False, False),
    "min": ("T", 2, False, False), "max": ("T", 2, False, False), "clamp": ("T", 3, False, False),
    "div_trunc": ("T", 2, False, False), "rem_trunc": ("T", 2, False, False),
    "div_euclid": ("T", 2, False, False), "rem_euclid": ("T", 2, False, False),
    "div_round": ("T", 2, False, True), "muldiv": ("R", 3, True, True),
    "mul_full": ("W", 2, False, False), "isqrt": ("T", 1, False, False),
    "isqrt_round": ("T", 1, False, True), "rotl": ("T", 2, False, False), "rotr": ("T", 2, False, False),
}

BIN_OPS = {
    "+": ("add", "checked"), "+%": ("add", "wrap"), "+|": ("add", "sat"),
    "-": ("sub", "checked"), "-%": ("sub", "wrap"), "-|": ("sub", "sat"),
    "*": ("mul", "checked"), "*%": ("mul", "wrap"), "*|": ("mul", "sat"),
    "/": ("div", "checked"), "%": ("rem", "checked"),
    "<<": ("shl", "checked"), "<<%": ("shl", "wrap"), ">>": ("shr", "checked"),
    "&": ("and", "checked"), "|": ("or", "checked"), "^": ("xor", "checked"),
}
SHIFTS = ("<<", "<<%", ">>")


@dataclass(eq=False)
class StructType:
    name: str
    decl: object
    fields: dict = field(default_factory=dict)     # name -> type, in declaration order

    def __repr__(self):
        return self.name


@dataclass(eq=False)
class Dim:
    """One dimension of an array type (SPEC-04 LS-62, LS-66)."""
    extent: int | None = None    # the extent, when it is known at compile time
    size: str | None = None      # the size parameter or shape symbol that a view `T[n]` binds
    expr: object = None          # an I64 expression evaluated at run time: a local array's extent
                                 # of size parameters, or a kernel parameter's shape expression


class ArrayType:
    """An array or view type of rank 1 to 4 (SPEC-04 LS-62, LS-66, LS-119; SPEC-02 V-2)."""
    __slots__ = ("elem", "dims", "writable", "view")

    def __init__(self, elem, dims, writable=True, view=False):
        self.elem = elem             # a scalar type name or a StructType
        self.dims = list(dims)       # a Dim per dimension, the first outermost
        self.writable = writable     # False for an `in` view and a string literal
        self.view = view             # a view (a parameter, a view variable, a slice), not owned storage

    @property
    def rank(self) -> int:
        return len(self.dims)

    @property
    def extents(self):
        """Every extent, when all are known at compile time; else None."""
        if any(d.extent is None for d in self.dims):
            return None
        return [d.extent for d in self.dims]

    @property
    def name(self) -> str:
        return "%s[%s]" % (type_name(self.elem), ", ".join(
            str(d.extent) if d.extent is not None else d.size or "_" for d in self.dims))

    def __repr__(self):
        return self.name


def array_of(elem, extents, writable=True, view=False) -> ArrayType:
    """The array type of `elem` with the given extents (None for one not known statically)."""
    return ArrayType(elem, [Dim(n) for n in extents], writable, view)


class ErrVal:
    """A value of an error set: the set that declares it and its name (SPEC-04 LS-92). A
    combined set holds the values of its member sets, these same objects (LS-97)."""
    __slots__ = ("set", "name")

    def __init__(self, set_, name):
        self.set = set_
        self.name = name

    def __repr__(self):
        return "%s.%s" % (self.set.name, self.name)


class ErrorSet:
    """An error set (SPEC-04 LS-92, LS-93, LS-97): its values in tag order, the tag of each
    from 1, and its unsigned underlying type."""

    def __init__(self, name, qualname, decl=None, underlying="U16"):
        self.name = name                # as declared: `ParseError`
        self.qualname = qualname        # `module.ParseError`; `ArithError` for the built-in set
        self.decl = decl                # the ErrorDecl, or None for a built-in set
        self.underlying = underlying
        self.values = []                # ErrVal, in tag order
        self.tags = {}                  # ErrVal -> its tag in this set
        self.state = None               # busy while the checker numbers the set, then done
        self.operands = None            # a combined set's operand sets as written (SPEC-03 A-21)

    def add(self, v):
        """`v` at the next tag, unless the set holds it already (LS-97: once, at its first position)."""
        if v not in self.tags:
            self.values.append(v)
            self.tags[v] = len(self.values)

    def holds(self, other) -> bool:
        """Every value of `other` is a value of this set (LS-97)."""
        return other is self or all(v in self.tags for v in other.values)

    def named(self, name):
        return [v for v in self.values if v.name == name]

    def value_text(self, v) -> str:
        """`name`, qualified by its declaring set where another value of this set has the
        same name (SPEC-09 CONF-11 rule 12)."""
        return v.name if len(self.named(v.name)) == 1 else "%s.%s" % (v.set.name, v.name)

    def __repr__(self):
        return self.name


class ErrorUnion:
    """`E!T` or `E!void`, the result type of a function (SPEC-04 LS-94, LS-95)."""
    __slots__ = ("set", "value")

    def __init__(self, set_, value):
        self.set = set_
        self.value = value              # the success type, or VOID

    @property
    def name(self) -> str:
        return "%s!%s" % (self.set.name, type_name(self.value))

    def __repr__(self):
        return self.name


ARITH_ERROR = ErrorSet("ArithError", "ArithError")
for _name in ("overflow", "div_zero", "shift", "narrow"):
    ARITH_ERROR.add(ErrVal(ARITH_ERROR, _name))
ARITH_ERROR.state = "done"
# The ArithError value that mirrors each fault code of a result-returning form (SPEC-01 IM-30).
ARITH_VALUES = {code: ARITH_ERROR.named(name)[0] for code, name in (
    ("E_OVERFLOW", "overflow"), ("E_DIV_ZERO", "div_zero"), ("E_SHIFT", "shift"), ("E_NARROW", "narrow"))}


ALLOC_ERROR = ErrorSet("AllocError", "AllocError")
ALLOC_ERROR.add(ErrVal(ALLOC_ERROR, "full"))
ALLOC_ERROR.state = "done"


class MemType:
    """`Arena(T)` or `Pool(T)` (SPEC-03 3.2, spelling Proposed): a value names an arena or a
    pool, and a copy names the same one (ref/OPEN.md REF-OQ-45)."""
    __slots__ = ("kind", "elem")

    def __init__(self, kind, elem):
        self.kind = kind                # Arena or Pool
        self.elem = elem

    @property
    def name(self) -> str:
        return "%s(%s)" % (self.kind, type_name(self.elem))

    def __eq__(self, other):
        return isinstance(other, MemType) and self.kind == other.kind and _same_elem(self.elem, other.elem)

    def __hash__(self):
        return hash((self.kind, type_name(self.elem)))

    def __repr__(self):
        return self.name


class HandleType:
    """`Handle(T)`, a pool slot, or `Handle(T[])`, an arena allocation (SPEC-03 M-12;
    ref/OPEN.md REF-OQ-45)."""
    __slots__ = ("elem", "arena")

    def __init__(self, elem, arena):
        self.elem = elem
        self.arena = arena

    @property
    def name(self) -> str:
        return "Handle(%s%s)" % (type_name(self.elem), "[]" if self.arena else "")

    def __eq__(self, other):
        return isinstance(other, HandleType) and self.arena == other.arena and _same_elem(self.elem, other.elem)

    def __hash__(self):
        return hash((self.arena, type_name(self.elem)))

    def __repr__(self):
        return self.name


def _same_elem(a, b) -> bool:
    return a is b or (isinstance(a, str) and a == b) or (isinstance(a, HandleType) and a == b)


def type_name(t) -> str:
    return t.name if isinstance(t, (StructType, ArrayType, ErrorSet, ErrorUnion, MemType, HandleType)) else str(t)


def is_aggregate(t) -> bool:
    return isinstance(t, (StructType, ArrayType))


def holds_error_value(t, seen=None) -> bool:
    """Whether a value of type `t` holds an error value, which zero-fill cannot produce
    (SPEC-04 LS-91, LS-314)."""
    if isinstance(t, ErrorSet):
        return True
    if isinstance(t, ArrayType):
        return holds_error_value(t.elem, seen)
    if isinstance(t, StructType):
        seen = seen or set()
        if id(t) in seen:
            return False
        seen.add(id(t))
        return any(holds_error_value(f, seen) for f in t.fields.values())
    return False


def elem_ident(t) -> str:
    """The `<E>` of `index.checked.<E>` (SPEC-01 IM-186): `struct` for a struct element. The
    grammar of SPEC-01 IM-130 has none for an error-set element, so that fault is refused."""
    if isinstance(t, ErrorSet):
        raise Refused("an index or slice fault on an array of error values: SPEC-01 IM-130 names no error-set "
                      "element in the operation (ref/OPEN.md REF-OQ-44)")
    return "struct" if isinstance(t, StructType) else t.lower()


@dataclass(eq=False)
class Sym:
    kind: str          # var param loopvar modvar const func struct module sizeparam view kernel errorset errbind
    name: str
    type: object
    pos: object
    decl: object = None
    value: object = None
    state: str | None = None
    exported: bool = False
    key: object = None         # modvar: (module name, variable name), the key of its state
    unit: object = None        # module: the Checker of the imported module
    mode: str = "in"           # param: in or inout, or out in a kernel; view: in or inout


@dataclass
class Program:
    path: str
    functions: dict
    structs: dict
    module_vars: list          # (name, type, value)
    is_script: bool
    script: list               # top-level statements of a script
    tests: list
    script_pos: object = None
    name: str = ""             # the module name (SPEC-04 LS-225)
    state: list = field(default_factory=list)   # every module's (key, type, value), in check order
    kernels: dict = field(default_factory=dict)
    memories: list = field(default_factory=list)  # every module's (key, MemType, capacity), in module order


def module_name(path: str) -> str:
    """`a/b.ci` names the module `a.b` (SPEC-04 LS-225)."""
    return (path[:-3] if path.endswith(".ci") else path).replace("/", ".")


class Checker:
    def __init__(self, module: P.Module, units=None):
        self.m = module
        self.path = module.path
        self.name = module_name(module.path)
        self.units = units or {}    # module path -> Checker of every module checked before this one
        self.globals: dict[str, Sym] = {}
        self.structs: dict[str, StructType] = {}
        self.scopes: list[dict] = []
        self.script_locals: set = set()
        self.ctx = "module"
        self.result = VOID
        self.loops: list = []       # stack of ("loop", label), ("switch", None), ("defer", None) and ("header", None)
        self.script_stmt = None     # the first top-level statement that makes the module a script
        self.size_params: dict = {}  # the size parameters of the function being checked
        self.writes: set = set()     # modvar keys the current function writes by name (LS-122)
        self.callees: set = set()    # functions the current function calls
        self.call_sites: list = []   # (call node, callee) in check order, for C5012
        self.func_writes: dict = {}  # FuncDecl -> (writes, callees)
        self.is_script = False
        self.selected: dict = {}     # selected import name -> the Checker of its module
        self.box09 = getattr(module, "box09", True)
        self.prelude = PRELUDE if self.box09 else PRELUDE - BOX09_PRELUDE
        self.kernel = None           # the KernelDecl being checked
        self.kreduce: dict = {}      # scalar `out` name -> its `reduce` statement, in the kernel checked
        self.box12 = getattr(module, "box12", True)
        self.types = BUILTIN_TYPES | BOX12_TYPES if self.box12 else BUILTIN_TYPES
        self.memory = self.box12 and getattr(module, "memory", True)
        if self.memory:
            self.types = self.types | MEMORY_SETS
        self.errsets: dict[str, ErrorSet] = {}
        self.memories: list = []     # (key, MemType, capacity) of each module-level arena and pool

    # -- names ----------------------------------------------------------------------
    def visible(self, name) -> bool:
        return (any(name in s for s in self.scopes) or name in self.globals
                or name in self.prelude or name in self.types)

    def declare(self, name, pos, sym):
        if self.visible(name):
            error("C3001", pos, "`%s` is already declared and visible here" % name)
        self.scopes[-1][name] = sym

    def lookup(self, name, pos) -> Sym:
        for s in reversed(self.scopes):
            if name in s:
                return s[name]
        if name in self.globals:
            sym = self.globals[name]
            if sym.kind == "const" and sym.state != "done":
                self.eval_global_const(sym, pos)
            elif sym.kind == "modvar" and sym.type is None:
                self.module_var_type(sym, pos)
            return sym
        if name in self.script_locals and self.ctx != "script":
            error("C3004", pos, "`%s` is a script local; it is not visible in a function or test" % name,
                  notes=self.script_notes("move the top-level statements into `void main() { ... }`; `%s` is "
                                          "then a module-level variable that the function can read" % name))
        if name in self.prelude:
            raise Refused("the built-in `%s` is outside the scalar surface that cint_ref implements" % name, pos)
        error("C3005", pos, "undefined name `%s`" % name)

    def script_notes(self, help_text):
        """The SPEC-04 LS-312 note and help of a diagnostic that depends on script mode."""
        s = self.script_stmt
        if s is None:
            note = "note: this file is a module, not a script, because every top-level item is a declaration (LS-218)"
        else:
            line = self.m.text.split("\n")[s.pos.line - 1][s.pos.column - 1:].rstrip()
            note = "note: this module is a script because of the top-level statement at %s: `%s`" % (s.pos, line)
        return (note, "help: " + help_text)

    # -- imports (SPEC-04 section 11) -------------------------------------------------
    def qualified(self, alias, name, pos) -> Sym:
        """The exported declaration `name` of the module imported as `alias`."""
        unit = self.globals[alias].unit
        sym = unit.globals.get(name)
        if sym is None or sym.kind == "module":
            error("C3005", pos, "module `%s` has no `%s`" % (unit.name, name))
        if not sym.exported:
            error("C3008", pos, "`%s` is not exported by module `%s` (SPEC-04 LS-227)" % (name, unit.name))
        return sym

    def is_alias(self, e) -> bool:
        """`e` names an import alias that no local declaration hides."""
        return (isinstance(e, P.Name) and not e.paren and not any(e.name in s for s in self.scopes)
                and e.name in self.globals and self.globals[e.name].kind == "module")

    # -- types ------------------------------------------------------------------------
    def resolve_type(self, ref: P.TypeRef, where="local"):
        """The type `ref` names. `where` is param, local, field, modvar or const: it decides
        which extents a shape may have (SPEC-04 LS-62, LS-66)."""
        if getattr(ref, "inner", None) is not None:
            return self.memory_type(ref, where)
        if ref.module is not None:
            if ref.module not in self.globals or self.globals[ref.module].kind != "module":
                error("C3006", getattr(ref, "start", ref.pos), "unknown type `%s.%s`: `%s` is not an imported module"
                      % (ref.module, ref.name, ref.module))
            sym = self.qualified(ref.module, ref.name, ref.pos)
            if sym.kind not in ("struct", "errorset"):
                error("C3007", ref.pos, "`%s.%s` is not a type" % (ref.module, ref.name))
            elem = sym.type
        elif ref.name in INT_TYPES or ref.name == BOOL:
            elem = ref.name
        elif ref.name in self.structs:
            elem = self.structs[ref.name]
        elif ref.name in self.errsets:
            elem = self.errsets[ref.name]
        elif ref.name in self.globals and self.globals[ref.name].kind in ("struct", "errorset"):
            elem = self.globals[ref.name].type          # a selected import
        elif self.box12 and ref.name == "ArithError":
            elem = ARITH_ERROR
        elif self.memory and ref.name == "AllocError":
            elem = ALLOC_ERROR
        else:
            error("C3006", ref.pos, "unknown type `%s`" % ref.name)
        if ref.shape is None:
            return elem
        if is_int_type(elem) and int_type(elem).width > 64:
            raise Refused("arrays of wide integers are outside the surface cint_ref implements", ref.pos)
        return self.shape_type(elem, ref.dims or [ref.shape], where, ref)

    def memory_type(self, ref: P.TypeRef, where):
        """`Arena(T)`, `Pool(T)`, `Handle(T)` or `Handle(T[])` (SPEC-03 3.2, spelling Proposed;
        ref/OPEN.md REF-OQ-45). An arena or pool value is a local or a parameter only, never
        stored or returned (C2051); a capacity belongs to a module-level declaration."""
        if ref.capacity is not None and where != "memory":
            raise Refused("a capacity outside a module-level arena or pool declaration: no clause gives the "
                          "diagnostic (ref/OPEN.md REF-OQ-45)", ref.capacity.start)
        inner = ref.inner
        if ref.name == "Handle":
            arena = inner.shape is not None
            if arena and (len(inner.dims) != 1 or inner.dims[0] != "_"):
                raise Refused("a handle of an array that is not `T[]`: no clause gives the diagnostic", inner.pos)
            saved, inner.shape, inner.dims = (inner.shape, inner.dims), None, None
            try:
                elem = self.resolve_type(inner, "elem")
            finally:
                inner.shape, inner.dims = saved
            t = HandleType(self._memory_elem(elem, inner), arena)
        else:
            elem = self.resolve_type(inner, "elem")
            t = MemType(ref.name, self._memory_elem(elem, inner))
            if where not in ("param", "local", "memory"):
                error("C2051", ref.pos, "an %s value is a local or a parameter; it is not stored in a struct "
                      "field, an array or a global, and is not returned (ref/OPEN.md REF-OQ-45)" % t.name)
            if ref.shape is not None:
                error("C2051", ref.pos, "an %s value is not stored in an array (ref/OPEN.md REF-OQ-45)" % t.name)
        if ref.shape is None:
            return t
        return self.shape_type(t, ref.dims or [ref.shape], where, ref)

    def _memory_elem(self, elem, ref):
        """The element type of an arena, a pool or a handle: an integer type, `Bool` or a struct
        (SPEC-03 M-19; ref/OPEN.md REF-OQ-45)."""
        if isinstance(elem, StructType) or elem == BOOL or (is_int_type(elem) and int_type(elem).width <= 64):
            return elem
        raise Refused("an arena or pool of %s: cint_ref implements integer, Bool and struct elements"
                      % type_name(elem), ref.pos)

    def resolve_set(self, ref: P.TypeRef) -> ErrorSet:
        """The error set `ref` names: a declared or imported set, or `ArithError` (C3006 for an
        unknown name, C3007 for a type that is not an error set; SPEC-04 LS-97)."""
        t = self.resolve_type(ref, "set")
        if not isinstance(t, ErrorSet):
            error("C3007", getattr(ref, "start", ref.pos), "`%s` is not an error set" % type_name(t))
        return t

    def union_type(self, u: P.UnionType) -> ErrorUnion:
        """The type of a result `E!T` or `E!void` (SPEC-04 LS-94)."""
        s = self.resolve_set(u.set)
        if u.value is None:
            return ErrorUnion(s, VOID)
        t = self.resolve_type(u.value, "result")
        if isinstance(t, ErrorSet):
            raise Refused("an error union whose success type is an error set: no clause says which returned values "
                          "are errors (ref/OPEN.md REF-OQ-44)", u.value.pos)
        return ErrorUnion(s, t)

    def build_set(self, s: ErrorSet):
        """Number a declared set (SPEC-04 LS-92, LS-93, LS-97): its own values from 1 in
        declaration order, or the values of its operands by concatenation, each value once,
        at its first position. The underlying type is unsigned and holds every tag (C2031)."""
        if s.state == "done":
            return
        d = s.decl
        if s.state == "busy":
            raise Refused("an error set combined from itself, directly or through other sets: no clause gives the "
                          "diagnostic (ref/OPEN.md REF-OQ-44)", d.name_pos)
        s.state = "busy"
        if d.underlying is not None:
            u = d.underlying.name
            if int_type(u).signed:
                raise Refused("an error set whose underlying type `%s` is signed (SPEC-04 LS-93): no clause gives "
                              "the diagnostic (ref/OPEN.md REF-OQ-44)" % u, d.underlying.pos)
            s.underlying = u
        if d.values is not None:
            for name, pos in d.values:
                if s.named(name):
                    error("C3001", pos, "duplicate value `%s` in error set %s" % (name, s.name))
                s.add(ErrVal(s, name))
        else:
            s.operands = []
            for ref in d.operands:
                o = self.resolve_set(ref)
                if o.state != "done":
                    self.build_set(o)
                s.operands.append(o)
                for v in o.values:
                    s.add(v)
        if len(s.values) > int_type(s.underlying).max:
            # The position is the declaration's first token (ref/OPEN.md REF-OQ-43).
            error("C2031", d.pos, "error set %s has %d values and %s holds tags up to %d (SPEC-04 LS-97)"
                  % (s.name, len(s.values), s.underlying, int_type(s.underlying).max))
        s.state = "done"

    def shape_type(self, elem, extents, where, ref) -> ArrayType:
        """The array type of `elem` with `extents`. `where` decides the extents admitted: `_` in
        a view (a parameter or a view variable); a size parameter in a view parameter; in a
        kernel parameter (`kparam`), a shape expression of the shape symbols (SPEC-02 K-1, K-6);
        in a function body, an I64 expression of size parameters (SPEC-04 LS-62, LS-66)."""
        if len(extents) > MAX_RANK:
            raise Refused("an array of rank %d: cint-core-1 admits ranks 1 to 4 (SPEC-02 V-2), and no clause "
                          "gives the diagnostic" % len(extents), ref.pos)
        dims = [self._dim(x, where, ref) for x in extents]
        return ArrayType(elem, dims, True, where in ("param", "kparam", "view"))

    def _dim(self, extent, where, ref) -> Dim:
        if extent == "_":
            if where not in ("param", "view"):
                raise Refused("an extent `_` outside a view parameter (SPEC-04 LS-66)", ref.pos)
            return Dim()
        if where in ("param", "kparam") and isinstance(extent, P.Name) and extent.name in self.size_params:
            return Dim(size=extent.name)
        t = self.infer(extent)
        if t is None:
            self.settle(extent, "I64")
        elif t != "I64":
            error("C2001", extent.start, "an extent has type I64, not %s (SPEC-04 LS-62)" % type_name(t))
        if self.is_const(extent):
            self.fold(extent)
            n = extent.const.value
            if n < 0:
                if not self.box09:
                    raise Refused("a negative extent: no clause gives its diagnostic", extent.start)
                # Decision 2026-10-05, box 09 ruling R10.
                error("C2015", extent.start, "extent %d is negative (SPEC-04 LS-62)" % n)
            return Dim(n)
        if where == "kparam" and self._of_size_params(extent):
            return Dim(expr=extent)
        if where == "local" and self.ctx == "function" and self._of_size_params(extent):
            return Dim(expr=extent)
        if where == "local" and self.ctx == "kernel":
            raise Refused("a private array whose extent is not a compile-time constant (SPEC-04 LS-110, C5029): no "
                          "clause gives the position", extent.start)
        if where == "view":
            raise Refused("a view variable whose declared extent is not a compile-time constant or `_`: no clause "
                          "gives its run-time check", extent.start)
        error("C6004", extent.start, "an extent here is a compile-time constant%s (SPEC-04 LS-62)"
              % (" or an I64 expression of size parameters" if self.ctx == "function" else ""))

    def module_var_type(self, sym, use_pos=None):
        """Resolves a visible module variable's annotation without evaluating its initializer."""
        if sym.type is not None:
            return sym.type
        if sym.state == "typing":
            raise Refused("a cyclic module-variable type dependency is outside the surface cint_ref implements",
                          use_pos or sym.pos)
        saved = self.ctx, self.scopes, self.result, self.loops, self.size_params
        sym.state = "typing"
        self.ctx, self.scopes, self.result, self.loops, self.size_params = "module", [{}], VOID, [], {}
        try:
            sym.type = self.resolve_type(sym.decl.type, "modvar")
            return sym.type
        finally:
            self.ctx, self.scopes, self.result, self.loops, self.size_params = saved
            sym.state = None

    def _of_size_params(self, e) -> bool:
        """`e` is an expression of size parameters and constants (SPEC-04 LS-62)."""
        for x in walk(e):
            if isinstance(x, (P.Call, P.Field, P.Index, P.StrLit)):
                return False
            if isinstance(x, P.Name) and getattr(x, "sym", None) is not None and \
                    x.sym.kind not in ("sizeparam", "const"):
                return False
        return True

    # -- module -----------------------------------------------------------------------
    def check(self) -> Program:
        m = self.m
        self._imports()
        self.script_stmt = next((s for s in m.statements if not isinstance(s, P.VarDecl)), None)
        # Module-level names, registered in source order, so that a duplicate is reported at
        # its later declaration (C3001; SPEC-04 LS-112, LS-228).
        decls = [(s.name_pos, "struct", s) for s in m.structs] + [(c.name_pos, "const", c) for c in m.consts] \
            + [(f.name_pos, "func", f) for f in m.functions] + [(k.name_pos, "kernel", k) for k in m.kernels] \
            + [(e.name_pos, "errorset", e) for e in m.errors] + [(d.name_pos, "memory", d) for d in m.memories]
        if self.script_stmt is None:
            decls += [(s.name_pos, "modvar", s) for s in m.statements]
        for _, kind, d in sorted(decls, key=lambda x: (x[0].line, x[0].column)):
            sym = Sym(kind, d.name, None, d.name_pos, d, exported=d.exported)
            if kind == "struct":
                self.structs[d.name] = sym.type = StructType(d.name, d)
            elif kind == "errorset":
                self.errsets[d.name] = sym.type = ErrorSet(d.name, self.name + "." + d.name, d)
            elif kind in ("modvar", "memory"):
                sym.key = (self.name, d.name)
            self._global(d.name, d.name_pos, sym)
        self.ctx, self.scopes = "module", [{}]
        for e in m.errors:
            self.build_set(self.errsets[e.name])
        for s in m.structs:
            st = self.structs[s.name]
            for f in s.fields:
                if f.name in st.fields:
                    error("C3001", f.pos, "duplicate field `%s`" % f.name)
                st.fields[f.name] = self.resolve_type(f.type, "field")
        self._check_struct_cycles()
        for d in m.memories:
            self.check_memory_decl(d)
        for f in m.functions:
            self.ensure_signature(f)
        for k in m.kernels:
            self.kernel_signature(k)
        is_script = self.is_script = self.script_stmt is not None
        module_vars = []
        if is_script:
            self.script_locals = {s.name for s in m.statements if isinstance(s, P.VarDecl)}
            if "main" in self.globals and self.globals["main"].kind == "func":
                error("C3011", self.globals["main"].pos, "a module with top-level statements must not define `main`",
                      notes=self.script_notes("move the top-level statements into `main`"))
        else:
            for s in m.statements:
                # The type is resolved here, so that a function declared before the variable
                # can read it (SPEC-04 LS-112; ref/OPEN.md REF-OQ-27).
                self.ctx, self.scopes = "module", [{}]
                self.module_var_type(self.globals[s.name])
            main = self.globals.get("main")
            result = main.decl.result if main is not None and main.kind == "func" else None
            if result is not None and not (isinstance(result, P.UnionType) and result.value is None):
                # SPEC-04 LS-221 admits `void main()` and `E!void main()` only (D-19; ref/OPEN.md REF-OQ-28).
                error("C2066", main.decl.result.pos, "`main` returns no value: write `void main()` (SPEC-04 LS-221)")
        seen_tests = set()
        for t in m.tests:
            if t.name in seen_tests:
                error("C3020", t.pos, "duplicate test name %r" % t.name)
            seen_tests.add(t.name)

        # Check every item in source order.
        items = ([("const", c) for c in m.consts] + [("func", f) for f in m.functions]
                 + [("test", t) for t in m.tests] + [("static_assert", s) for s in m.static_asserts]
                 + [("kernel", k) for k in m.kernels])
        if not is_script:
            items += [("modvar", s) for s in m.statements]
        items.sort(key=lambda it: _item_pos(it[1]))
        for kind, item in items:
            if kind == "const":
                self.eval_global_const(self.globals[item.name])
            elif kind == "func":
                self.check_function(item)
            elif kind == "kernel":
                self.check_kernel(item)
            elif kind == "test":
                self.check_body("test", VOID, [], item.body.stmts)
            elif kind == "static_assert":
                self.ctx, self.scopes = "module", [{}]
                self.check_stmt(item)
            else:
                module_vars.append(self.check_module_var(item))
        if is_script:
            self.check_body("script", VOID, [], m.statements)
        functions = {f.name: f for f in m.functions}
        script_pos = m.statements[0].pos if m.statements else None
        self.module_vars = module_vars
        return Program(self.path, functions, self.structs, module_vars, is_script,
                       m.statements if is_script else [], m.tests, script_pos, self.name,
                       kernels={k.name: k for k in m.kernels}, memories=list(self.memories))

    def check_memory_decl(self, d: P.VarDecl):
        """`Arena(T, capacity) name;` or `Pool(T, capacity) name;` at module level, created with
        the context (SPEC-03 M-20; SPEC-04 LS-113, LS-219): `capacity` is an `I64` constant
        expression (C6004 otherwise) of at least 0 (C6006 otherwise; ref/OPEN.md REF-OQ-45)."""
        sym = self.globals[d.name]
        self.ctx, self.scopes, self.result, self.loops = "module", [{}], VOID, []
        t = self.resolve_type(d.type, "memory")
        cap = d.type.capacity
        self.expect(cap, "I64")
        if not self.is_const(cap):
            error("C6004", cap.start, "the capacity of %s `%s` is a constant expression (SPEC-03 M-20)"
                  % (d.type.name.lower(), d.name))
        self.fold(cap)
        n = cap.const.value
        if n < 0:
            error("C6006", cap.start, "the capacity of %s `%s` is %d, below 0 (SPEC-03 M-20)"
                  % (d.type.name.lower(), d.name, n))
        if d.init is not None:
            error("C1050", d.init_pos, "an arena or pool declaration has no initializer (SPEC-03 M-20)")
        sym.type = d.ty = t
        self.memories.append((sym.key, t, n))

    def _global(self, name, pos, sym):
        if name in self.selected:
            error("C3002", pos, "`%s` collides with a name selected from module `%s` (SPEC-04 LS-228)"
                  % (name, self.selected[name].name))
        if name in self.globals or name in self.prelude or name in self.types:
            error("C3001", pos, "`%s` is already declared" % name)
        self.globals[name] = sym

    def _imports(self):
        """Import aliases and selected names become module-level names (SPEC-04 LS-228)."""
        for imp in self.m.imports:
            unit = self.units[imp.target]
            if unit.is_script:
                # SPEC-04 LS-220, with the LS-312 note on the imported module.
                error("C3010", imp.path_pos, "module `%s` is a script and cannot be imported" % unit.name,
                      notes=unit.script_notes("move the reusable declarations of `%s` into an importable module"
                                              % unit.name))
            if imp.selected is None:
                self._global(imp.alias, imp.alias_pos, Sym("module", imp.alias, None, imp.alias_pos, imp, unit=unit))
                continue
            for name, pos in imp.selected:
                if name in self.globals or name in self.prelude or name in self.types:
                    error("C3002", pos, "the selected name `%s` collides with another name (SPEC-04 LS-228)" % name)
                self.globals["\0"] = Sym("module", "", None, pos, imp, unit=unit)
                sym = self.qualified("\0", name, pos)
                del self.globals["\0"]
                self.globals[name] = sym          # the declaration itself, as the importer sees it
                self.selected[name] = unit

    def ensure_signature(self, f, use_pos=None):
        """Types early annotation calls without relying on a later signature pass."""
        state = getattr(f, "_signature_state", None)
        if state == "done":
            return
        if state == "typing":
            raise Refused("a cyclic function-signature dependency is outside the surface cint_ref implements",
                          use_pos or f.name_pos)
        saved = self.ctx, self.scopes, self.result, self.loops, self.size_params
        f._signature_state = "typing"
        self.ctx, self.scopes, self.result, self.loops, self.size_params = "module", [{}], VOID, [], {}
        try:
            self._signature(f)
            f._signature_state = "done"
        finally:
            self.ctx, self.scopes, self.result, self.loops, self.size_params = saved
            if f._signature_state != "done":
                f._signature_state = None

    def _signature(self, f):
        """Parameter and result types; size parameters (SPEC-04 LS-114, LS-117, LS-119)."""
        self.size_params = {}
        for name, pos in f.size_params:
            if name in self.size_params or name in self.globals or name in self.prelude or name in self.types:
                error("C3001", pos, "`%s` is already declared" % name)
            self.size_params[name] = pos
        f.param_types = []
        for p in f.params:
            t = self.resolve_type(p.type, "param")
            if isinstance(t, ArrayType):
                t.writable = p.mode == "inout"
                t.view = True
            elif p.mode == "inout" and not isinstance(t, (StructType, MemType)):
                error("C2064", getattr(p, "start", p.pos), "an `inout` parameter is an array or a struct; a scalar "
                      "is returned instead (SPEC-04 LS-119)")
            f.param_types.append(t)
        bound = {d.size for t in f.param_types if isinstance(t, ArrayType) for d in t.dims if d.size is not None}
        for name, pos in f.size_params:
            if name not in bound:
                error("C2065", pos, "size parameter `%s` is bound by no view parameter (SPEC-04 LS-117)" % name)
        if isinstance(f.result, P.UnionType):
            f.result_type = self.union_type(f.result)
        else:
            f.result_type = VOID if f.result is None else self.resolve_type(f.result, "result")
        self.size_params = {}

    def _check_struct_cycles(self):
        """C2055 at the field that closes a cycle, structs and fields in declaration order."""
        state = {}

        def visit(st):
            state[st] = 1
            for f in st.decl.fields:
                t = st.fields[f.name]
                if isinstance(t, ArrayType):
                    t = t.elem
                if isinstance(t, StructType) and state.get(t) != 2:
                    if state.get(t) == 1:
                        error("C2055", f.pos, "struct `%s` contains itself" % t.name)
                    visit(t)
            state[st] = 2
        for st in self.structs.values():
            if state.get(st) != 2:
                visit(st)

    def eval_global_const(self, sym: Sym, use_pos=None):
        if sym.state == "done":
            return
        if sym.state == "busy":
            error("C6005", use_pos or sym.pos, "constant `%s` depends on itself" % sym.name)
        sym.state = "busy"
        saved = (self.ctx, self.scopes, self.result, self.loops)
        self.ctx, self.scopes, self.result, self.loops = "module", [{}], VOID, []
        d = sym.decl
        sym.type = self.const_type(d)
        sym.value = self.const_value(d, sym.type)
        self.ctx, self.scopes, self.result, self.loops = saved
        sym.state = "done"

    def const_type(self, d):
        t = self.resolve_type(d.type, "const")
        if is_aggregate(t):
            raise Refused("a constant of struct or array type is outside the surface that cint_ref implements", d.pos)
        return t

    def const_value(self, d, t):
        self.expect(d.init, t)
        if not self.is_const(d.init):
            error("C6004", d.init.start, "the initializer of `%s` is not a constant expression" % d.name)
        self.fold(d.init)
        return d.init.const

    def check_module_var(self, d: P.VarDecl):
        sym = self.globals[d.name]
        self.ctx, self.scopes = "module", [{}]
        t = sym.type
        if d.init is None and holds_error_value(t):
            error("C2054", d.pos, "no error value is 0, so a declaration that zero-fills %s needs an initializer "
                  "(SPEC-04 LS-91, LS-314)" % type_name(t))
        if isinstance(t, ErrorSet):
            raise Refused("a module-level variable of an error set: SPEC-09 CONF-11 rule 10 writes `state.global` "
                          "lines for integer and Bool variables only (ref/OPEN.md REF-OQ-44)", d.pos)
        if d.init is None:
            return (d.name, t, None)
        if is_aggregate(t):
            raise Refused("a module-level struct or array initializer is outside the surface cint_ref implements",
                          d.pos)
        self.expect(d.init, t)
        if not self.is_const(d.init):
            error("C6004", d.init.start, "a module-level variable needs a constant initializer (SPEC-04 LS-113)",
                  notes=self.script_notes("give `%s` a constant initializer, or compute the value inside "
                                          "`void main() { ... }`" % d.name))
        self.fold(d.init)
        return (d.name, t, d.init.const.value)

    def check_function(self, f: P.FuncDecl):
        params = [(name, pos, Sym("sizeparam", name, "I64", pos)) for name, pos in f.size_params]
        for p, t in zip(f.params, f.param_types):
            params.append((p.name, p.pos, Sym("param", p.name, t, p.pos, mode=p.mode)))
        self.size_params = dict(f.size_params)
        self.check_body("function", f.result_type, params, f.body.stmts, f)
        self.size_params = {}
        r = f.result_type
        # An `E!void` function, like a `void` one, succeeds at its closing brace (LS-190).
        if r != VOID and not (isinstance(r, ErrorUnion) and r.value == VOID) and can_complete_list(f.body.stmts):
            error("C4001", f.name_pos, "function `%s` can reach its closing brace without returning a value" % f.name)

    # -- kernels (SPEC-02 K-1 to K-12; SPEC-04 LS-244 to LS-251) ----------------------------
    def kernel_signature(self, k):
        """Shape symbols and parameter types (SPEC-02 K-1, K-2, K-5; SPEC-04 LS-246). A
        parameter extent is a shape expression of SPEC-02 K-1 over the shape symbols."""
        if getattr(k, "param_types", None) is not None:
            return
        saved = self.ctx, self.scopes, self.result, self.loops, self.size_params
        self.ctx, self.result, self.loops, self.size_params = "module", VOID, [], {}
        try:
            scope = {}
            for name, pos in k.size_params:
                if name in self.size_params or name in self.globals or name in self.prelude or name in self.types:
                    error("C3001", pos, "`%s` is already declared" % name)
                self.size_params[name] = pos
                scope[name] = Sym("sizeparam", name, "I64", pos)
            self.scopes = [scope]
            types = []
            for p in k.params:
                at = getattr(p, "start", p.pos)
                t = self.resolve_type(p.type, "kparam")
                if isinstance(t, ArrayType):
                    if not is_int_type(t.elem):
                        error("C5028", at, "a kernel array parameter has an integer element type, not %s "
                              "(SPEC-04 LS-246)" % type_name(t.elem))
                    for x in p.type.dims:
                        if shape_key(x) is None:
                            raise Refused("an extent that is not a shape expression of SPEC-02 K-1", x.start)
                    t.writable = p.mode != "in"
                else:
                    if not is_int_type(t):
                        error("C5028", at, "a kernel scalar parameter has an integer type, not %s (SPEC-04 LS-246)"
                              % type_name(t))
                    if p.mode == "inout":
                        error("C2064", at, "a scalar `inout` kernel parameter is not admitted in cint-core-1 "
                              "(SPEC-02 K-2)")
                types.append(t)
            bound = {d.size for t in types if isinstance(t, ArrayType) for d in t.dims if d.size is not None}
            for name, pos in k.size_params:
                if name not in bound:
                    error("C2065", pos, "shape symbol `%s` is bound by no array parameter extent (SPEC-02 K-5)" % name)
            k.param_types = types
        finally:
            self.ctx, self.scopes, self.result, self.loops, self.size_params = saved

    def check_kernel(self, k):
        """The static rules of a kernel: the iteration space (K-8, K-12), `where` constraints
        (K-1), the body restrictions (K-11; C5020 to C5026), the write rule and coverage (K-10,
        K-10a; C5031) and the reductions (R-1). A rule without a diagnostic is refused."""
        self.kernel_signature(k)
        k.qualname = self.name + "." + k.name
        self.size_params = dict(k.size_params)
        self.ctx, self.result, self.loops = "kernel", VOID, []
        self.scopes = [{name: Sym("sizeparam", name, "I64", pos) for name, pos in k.size_params}]
        if k.over is None:
            self._element_form(k)
        if not any(p.mode != "in" for p in k.params):
            raise Refused("a kernel with no `out` or `inout` parameter (SPEC-02 K-3): no clause gives the "
                          "diagnostic", k.name_pos)
        keys = []
        for v, pos, extent in k.over:
            keys.append(shape_key(extent))
            if keys[-1] is None:
                raise Refused("an `over` extent that is not a shape expression of SPEC-02 K-1", extent.start)
            t = self.infer(extent)
            if t is None:
                self.settle(extent, "I64")
            elif t != "I64":
                error("C2001", extent.start, "an extent has type I64, not %s (SPEC-04 LS-62)" % type_name(t))
        for p, t in zip(k.params, k.param_types):
            if isinstance(t, ArrayType) and p.mode != "in" and (
                    t.rank < len(keys) or [shape_key(x) for x in p.type.dims[:len(keys)]] != keys):
                raise Refused("the leading extents of `%s` are not the `over` extents (SPEC-02 K-8): no clause gives "
                              "the diagnostic" % p.name, p.pos)
        for c in k.where:
            if not (isinstance(c, P.Compare) and c.op in ("<=", "<", ">=", ">", "==")
                    and shape_key(c.left) is not None and shape_key(c.right) is not None):
                raise Refused("a `where` constraint that is not a comparison of shape expressions (SPEC-02 K-1)",
                              c.start)
            self.condition(c)
            self.fold(c)
        params = [(name, pos, Sym("sizeparam", name, "I64", pos)) for name, pos in k.size_params]
        params += [(v, pos, Sym("loopvar", v, "I64", pos)) for v, pos, _ in k.over]
        params += [(p.name, p.pos, Sym("param", p.name, t, p.pos, mode=p.mode))
                   for p, t in zip(k.params, k.param_types)]
        self.kernel, self.kreduce = k, {}
        try:
            self.check_body("kernel", VOID, params, k.body.stmts, k)
            k.reductions = [self.kreduce[p.name] for p in k.params if p.name in self.kreduce]
            for p, t in zip(k.params, k.param_types):
                if p.mode == "out" and not isinstance(t, ArrayType) and p.name not in self.kreduce:
                    raise Refused("scalar `out` `%s` is the target of no `reduce` statement (SPEC-02 R-1): no clause "
                                  "gives the diagnostic" % p.name, p.pos)
            self._coverage(k)
        finally:
            self.kernel = None
            self.size_params = {}

    def _element_form(self, k):
        """Rewrite an element-form body as the indexed form that defines it (SPEC-02 K-12): one
        fresh `over` variable per dimension, and each whole-array parameter `a` written or read
        as `a[v1, ..., vr]`."""
        params = {p.name: (p, t) for p, t in zip(k.params, k.param_types)}

        def written(s):
            if isinstance(s, P.Assign) and s.op == "=" and isinstance(s.target, P.Name):
                p, t = params.get(s.target.name, (None, None))
                if isinstance(t, ArrayType) and p.mode != "in":
                    return p
            return None
        stmts = k.body.stmts
        if not stmts or any(written(s) is None for s in stmts):
            raise Refused("a kernel body without an `over` clause that is not whole-array assignments to `out` or "
                          "`inout` parameters (SPEC-02 K-12): no clause gives the diagnostic", k.name_pos)
        first = written(stmts[0])
        keys = [shape_key(x) for x in first.type.dims]
        fresh = ["\0e%d" % j for j in range(len(keys))]
        k.over = [(fresh[j], first.pos, first.type.dims[j]) for j in range(len(keys))]

        def element(name):
            p, _ = params[name.name]
            if [shape_key(x) for x in p.type.dims] != keys:
                raise Refused("an element-form kernel over arrays of different shapes (SPEC-02 K-12): no clause gives "
                              "the diagnostic", name.start)
            items = [P.Name(name.pos, name.pos, v) for v in fresh]
            return P.Index(name.pos, name.start, name, items[0], items)

        def rewrite(e):
            if isinstance(e, P.Name):
                return element(e) if isinstance(params.get(e.name, (None, None))[1], ArrayType) else e
            if isinstance(e, (P.Lit, P.BoolLit, P.TypeProp, P.TypeArg)):
                return e
            if isinstance(e, (P.Unary, P.Convert)):
                e.operand = rewrite(e.operand)
            elif isinstance(e, (P.Binary, P.Compare, P.Logical)):
                e.left, e.right = rewrite(e.left), rewrite(e.right)
            elif isinstance(e, P.Cond):
                e.cond, e.a, e.b = rewrite(e.cond), rewrite(e.a), rewrite(e.b)
            elif isinstance(e, P.Call) and e.qual is None:
                for a in e.args:
                    a.value = rewrite(a.value)
            else:
                raise Refused("an element-form expression that is not elementwise (SPEC-02 K-12)", e.start)
            return e
        for s in stmts:
            s.target = element(s.target)
            s.value = rewrite(s.value)

    def _write_rule(self, target, root):
        """SPEC-02 K-10, SPEC-04 LS-248: a write to an array `out` or `inout` parameter is
        `p[v1, ..., vd, j...]`, with the `over` variables in order as its leading indices."""
        over = [v for v, _, _ in self.kernel.over]
        ok = (isinstance(target, P.Index) and target.obj is root and len(target.items) == root.sym.type.rank
              and not any(isinstance(x, P.Slice) for x in target.items)
              and all(isinstance(x, P.Name) and x.name == v for x, v in zip(target.items, over)))
        if not ok:
            error("C5031", target.start, "a write to `%s` outside the work-item's owned block: its leading indices "
                  "are the `over` variables in order (SPEC-04 LS-248)" % root.name)

    def _coverage(self, k):
        """SPEC-02 K-10a: every element of the owned block of each array `out` is written on
        every path that completes, by pattern 1 or 2; otherwise C5031, at the first write to
        the parameter, or at the parameter when nothing writes it."""
        d = len(k.over)
        for p, t in zip(k.params, k.param_types):
            if p.mode != "out" or not isinstance(t, ArrayType):
                continue
            block = [shape_key(x) for x in p.type.dims[d:]]
            if not any(_covers(s, p.name, d, block, []) for s in k.body.stmts):
                at = _first_write(k.body.stmts, p.name) or p.pos
                error("C5031", at, "the writes to `%s` do not cover its owned block by the patterns of SPEC-02 K-10a "
                      "(SPEC-04 LS-248)" % p.name)

    def _dispatch(self, e, k):
        """A dispatch statement (SPEC-04 LS-251; SPEC-02 K-4, K-7, A-8): arguments of the
        parameter types, writable places for `out` and `inout`, static shapes (C2012) and the
        same variable bound to a written parameter and another (C5010)."""
        if self.ctx == "kernel":
            error("C5023", e.start, "a kernel body does not dispatch a kernel (SPEC-04 LS-250)")
        named = next((a for a in e.args if a.name is not None), None)
        if named is not None:
            raise Refused("a named argument in a dispatch is outside the surface cint_ref implements", named.pos)
        self.kernel_signature(k)
        if len(e.args) != len(k.params):
            error("C2022", e.start, "`%s` takes %d arguments" % (k.name, len(k.params)))
        args = [a.value for a in e.args]
        for p, t, a in zip(k.params, k.param_types, args):
            if isinstance(t, ArrayType):
                self.expect_array(a, ArrayType(t.elem, [Dim() for _ in t.dims]))
            elif p.mode == "in":
                self.expect(a, t)
                continue
            else:
                got = self.infer(a)
                if got is not None and got != t:
                    error("C2001", a.start, "expected %s, found %s (SPEC-02 K-4)" % (type_name(t), type_name(got)))
            if p.mode != "in":
                if not self._writable_place(a):
                    error("C2067", a.start, "the argument of `%s` parameter `%s` is not a writable place "
                          "(SPEC-04 LS-245)" % (p.mode, p.name))
                self._note_write(a)
        for a in args:
            self.fold(a)
        self._kernel_static_shapes(k, args)
        self._kernel_static_alias(k, args)
        e.kind = "kernel"
        e.kernel = k
        e.values = args
        e.ty = VOID
        return VOID

    def _kernel_static_shapes(self, k, args):
        """SPEC-02 K-7: when every array argument's shape is known statically, the K-6 binding
        runs at compile time and a mismatch is C2012 at the argument."""
        arrays = [(p, t, a) for p, t, a in zip(k.params, k.param_types, args) if isinstance(t, ArrayType)]
        if any(a.ty.extents is None for _, _, a in arrays):
            return
        sizes = {}
        for _, t, a in arrays:
            for d, n in zip(t.dims, a.ty.extents):
                if d.size is not None:
                    sizes.setdefault(d.size, n)
        for p, t, a in arrays:
            for dim, (d, n) in enumerate(zip(t.dims, a.ty.extents)):
                want = shape_value(d, sizes)
                if want is None:
                    return           # an extent that overflows I64: the entry check reports it
                if want != n:
                    error("C2012", a.start, "extent %d where %d is required: dimension %d of `%s` (SPEC-02 K-6, K-7)"
                          % (n, want, dim, p.name))

    def _kernel_static_alias(self, k, args):
        """SPEC-02 A-8: the same variable, or provably the same view, bound to an `out` or
        `inout` parameter and to another array parameter is C5010 at the later argument;
        any other pair is checked at dispatch entry. Pairs are visited as for a call."""
        n = len(args)
        for i in range(n):
            for x in range(i + 1, n):
                if not (isinstance(k.param_types[i], ArrayType) and isinstance(k.param_types[x], ArrayType)):
                    continue
                if k.params[i].mode == "in" and k.params[x].mode == "in":
                    continue
                same = overlap(place_path(args[i]), place_path(args[x])) == "overlap"
                if not same:
                    a, b = self.static_view(args[i]), self.static_view(args[x])
                    same = a is not None and b is not None and views.classify(a, b) == "identical"
                if same:
                    error("C5010", args[x].start, "this argument is the same storage as argument %d, and one of them "
                          "is bound to an `out` or `inout` parameter (SPEC-02 A-8)" % (i + 1))

    def check_body(self, ctx, result, params, stmts, owner=None):
        self.ctx, self.result, self.scopes, self.loops = ctx, result, [{}], []
        self.writes, self.callees = set(), set()
        for name, pos, sym in params:
            self.declare(name, pos, sym)
        self.scopes.append({})
        for s in stmts:
            self.check_stmt(s)
        if owner is not None:
            self.func_writes[owner] = (self.writes, self.callees)

    # -- expressions ----------------------------------------------------------------------
    def expect(self, e, t, narrow="C2001"):
        """`e` where a value of type `t` is expected. A value of an error set that `t` holds
        converts to `t` (SPEC-04 LS-97); one of a set that `t` does not hold is `narrow`,
        C2042 at `return` and C2001 elsewhere."""
        if isinstance(t, ArrayType):
            return self.expect_array(e, t)
        if isinstance(e, P.ArrayLit):
            error("C2001", e.start, "expected %s, found an array literal" % type_name(t))
        return self.accept(e, self.infer(e), t, narrow)

    def accept(self, e, got, t, narrow="C2001"):
        """The rest of `expect`, for `e` already inferred as `got`."""
        if isinstance(got, ArrayType):
            error("C2001", e.start, "expected %s, found the array %s" % (type_name(t), type_name(got)))
        if got is None:
            self.settle(e, t)
        elif got == VOID:
            error("C2001", e.start, "a call of a void function has no value")
        elif got != t:
            if isinstance(got, ErrorSet) and isinstance(t, ErrorSet):
                if t.holds(got):
                    return t
                error(narrow, e.start, "a value of %s is not a %s: %s does not hold every value of %s (SPEC-04 LS-97)"
                      % (got.name, t.name, t.name, got.name))
            error("C2001", e.start, "expected %s, found %s" % (type_name(t), type_name(got)))
        return t

    def expect_array(self, e, t: ArrayType):
        """`e` is an array or view of `t`'s rank whose elements have `t`'s type (any, when
        `t.elem` is None); static extents must agree dimension by dimension (C2012, SPEC-04
        LS-70, LS-117). Returns `e`'s type."""
        if isinstance(e, P.StrLit):
            # BOOT-02: a string literal is a read-only view of its bytes where an `in U8`
            # view is expected (SPEC-09 5.5; ref/OPEN.md REF-OQ-30).
            if t.elem not in ("U8", None) or t.writable or t.rank != 1:
                raise Refused("a string literal where no `in U8` view is expected (Str is outside the "
                              "surface cint_ref implements)", e.start)
            got = array_of("U8", [len(e.parts[0])], False, True)
        elif isinstance(e, P.ArrayLit):
            got = self._array_lit(e, t)
        else:
            got = self.infer(e)
        if not isinstance(got, ArrayType):
            error("C2001", e.start, "expected the array %s, found %s"
                  % (type_name(t), type_name(got) if got is not None else "a literal"))
        if t.elem is not None and got.elem is not t.elem and got.elem != t.elem:
            error("C2001", e.start, "expected elements of type %s, found %s" % (type_name(t.elem), type_name(got.elem)))
        if got.rank != t.rank:
            error("C2001", e.start, "expected an array of rank %d, found rank %d" % (t.rank, got.rank))
        for k, (dg, dt) in enumerate(zip(got.dims, t.dims)):
            if dg.extent is not None and dt.extent is not None and dg.extent != dt.extent:
                error("C2012", e.start, "extent %d where %d is required%s" % (
                    dg.extent, dt.extent, " in dimension %d" % k if t.rank > 1 else ""))
        e.ty = got
        return got

    def _array_lit(self, e, t: ArrayType) -> ArrayType:
        """An array literal where an array of `t`'s rank and element type is expected: one
        item per index of the first dimension, nested for each further dimension; a count
        that differs from a static extent, or from the count of the first literal at the
        same depth, is C2011 at that literal (SPEC-04 LS-67)."""
        if t.elem is None:
            raise Refused("an array literal where no element type is given", e.start)
        extents = [None] * t.rank

        def visit(lit, k):
            n = len(lit.items)
            want = t.dims[k].extent if t.dims[k].extent is not None else extents[k]
            if want is not None and n != want:
                error("C2011", lit.start, "%d elements where the extent is %d (SPEC-04 LS-67)" % (n, want))
            extents[k] = n
            for x in lit.items:
                if k + 1 < t.rank:
                    if not isinstance(x, P.ArrayLit):
                        error("C2001", x.start, "expected a nested array literal for dimension %d" % (k + 1))
                    visit(x, k + 1)
                else:
                    self.expect(x, t.elem)
        visit(e, 0)
        extents = [n if n is not None else d.extent for n, d in zip(extents, t.dims)]
        if any(n is None for n in extents):
            # An empty literal leaves the inner extents to the static type, which has none.
            raise Refused("an empty array literal of rank above 1 whose inner extents are not static", e.start)
        e.extents = extents
        e.elem = t.elem
        e.ty = array_of(t.elem, extents, False, False)
        return e.ty

    def _infer_ArrayLit(self, e):
        raise Refused("an array literal where no array type gives its element type (SPEC-04 LS-67)", e.start)

    def default_type(self, e):
        t = self.infer(e)
        if t is None:
            self.settle(e, "I64")
            return "I64"
        if t == VOID:
            error("C2001", e.start, "a call of a void function has no value")
        return t

    def condition(self, e, code="C2002"):
        t = self.infer(e)
        if t != BOOL:
            error(code, e.start, "a condition must have type Bool; write `x != 0`")

    def require_int(self, t, e):
        """An operator or built-in defined on integers only (C2103, SPEC-05 X-10)."""
        if not is_int_type(t):
            error("C2103", e.start, "the operator is defined on integer types, not on %s" % type_name(t))

    def infer(self, e):
        t = getattr(self, "_infer_" + type(e).__name__)(e)
        if isinstance(t, ErrorUnion) and not getattr(e, "_consumed", False):
            # SPEC-04 LS-95: `try`, `catch` and `_ =` consume an error union, and nothing else does.
            error("C2040", e.start, "the error union %s is not consumed by `try`, `catch` or `_ =` (SPEC-04 LS-95)"
                  % t.name)
        return t

    def _infer_Lit(self, e):
        return None

    def _infer_ErrorLit(self, e):
        return None                     # typed by its context (SPEC-04 LS-60)

    def _infer_Try(self, e):
        """`try e` (SPEC-04 LS-129): `e` is an error union whose set the function's error set
        holds; in a test block, an error of any set (LS-315). Its value is the success value."""
        if self.ctx == "kernel":
            error("C5022", e.pos, "a kernel body does not use `try` or error unions (SPEC-04 LS-250)")
        if any(k == "defer" for k, _ in self.loops):
            error("C4030", e.pos, "`try` would leave the deferred statement (SPEC-04 LS-187)")
        e.operand._consumed = True
        u = self.infer(e.operand)
        if not isinstance(u, ErrorUnion):
            error("C2043", e.pos, "`try` applies to an error union, not to %s (SPEC-04 LS-129)"
                  % (type_name(u) if u is not None else "a literal"))
        e.target_set = None             # in a test, the error leaves with its own set (BX12-30)
        if self.ctx != "test":
            r = self.result
            if not isinstance(r, ErrorUnion):
                raise Refused("`try` where the result is not an error union: LS-129 needs the function's error set to "
                              "hold the error, and no clause gives the diagnostic (ref/OPEN.md REF-OQ-44)", e.pos)
            if not r.set.holds(u.set):
                error("C2042", e.pos, "%s, the error set of this function, does not hold every value of %s "
                      "(SPEC-04 LS-129)" % (r.set.name, u.set.name))
            e.target_set = r.set
        e.ty = u.value
        return u.value

    def _infer_Catch(self, e):
        """`e catch value` and `e catch (name) { ... }` (SPEC-04 LS-130, LS-131): the success
        value, or on an error the fallback, or the block, which must not complete (C4002)."""
        e.left._consumed = True
        u = self.infer(e.left)
        if not isinstance(u, ErrorUnion):
            raise Refused("`catch` applied to an expression that is not an error union: no clause gives the "
                          "diagnostic (ref/OPEN.md REF-OQ-44)", e.pos)
        if u.value == VOID:
            raise Refused("`catch` on an `E!void` result: LS-130 and LS-131 give `catch` the success type, which "
                          "`E!void` has not (ref/OPEN.md REF-OQ-44)", e.pos)
        if e.block is None:
            self.expect(e.value, u.value)
        else:
            self.scopes.append({})
            self.declare(e.bind, e.bind_pos, Sym("errbind", e.bind, u.set, e.bind_pos))
            self.check_block(e.block.stmts)
            self.scopes.pop()
            if can_complete_list(e.block.stmts):
                error("C4002", e.block.pos, "the `catch` block can complete normally: end it with `return`, `break` "
                      "or `continue` (SPEC-04 LS-131)")
        e.ty = u.value
        return u.value

    def _infer_BoolLit(self, e):
        e.ty = BOOL
        return BOOL

    def _infer_StrLit(self, e):
        raise Refused("a string value outside a print statement or an `in U8` argument is outside the surface "
                      "cint_ref implements", e.start)

    def _infer_Index(self, e):
        """`a[i, j, ...]`: one item per dimension, an `I64` index or a slice (SPEC-04 LS-64,
        LS-65, LS-161). Every index gives an element; fewer indices than the rank, or any
        slice, give a view of the same storage with the permission of `a` (LS-65, LS-162)."""
        t = self.infer(e.obj)
        if isinstance(t, MemType):
            return self._deref(e, t)
        if isinstance(e.obj, P.Index) and getattr(e.obj, "partial", False):
            error("C2010", e.pos, "a partial index is indexed directly; write `m[i, j]` (SPEC-04 LS-65)")
        if not isinstance(t, ArrayType):
            error("C2103", e.pos, "indexing needs an array or a view, found %s"
                  % (type_name(t) if t is not None else "a literal"))
        if len(e.items) > t.rank:
            raise Refused("%d indices for an array of rank %d: no clause gives the diagnostic"
                          % (len(e.items), t.rank), e.pos)
        dims = []
        for k, item in enumerate(e.items):
            if isinstance(item, P.Slice):
                dims.append(self._slice(item, t.dims[k]))
                continue
            ti = self.infer(item)
            if ti is None:
                self.settle(item, "I64")
            elif ti != "I64":
                error("C2001", item.start, "an index has type I64, not %s; convert it with `as I64` (SPEC-04 LS-64)"
                      % type_name(ti))
        e.view = len(e.items) < t.rank or bool(dims)
        e.partial = e.view and not dims
        if not e.view:
            e.ty = t.elem
            return e.ty
        if self.ctx == "kernel":
            raise Refused("a slice or a partial index in a kernel body is outside the surface cint_ref implements",
                          e.pos)
        e.ty = ArrayType(t.elem, dims + t.dims[len(e.items):], t.writable, True)
        return e.ty

    def _deref(self, e, t: MemType):
        """`p[h]`, the value in a pool's slot, a place; `a[h]`, the rank-1 view of an arena
        allocation (SPEC-03 M-14a, M-15; ref/OPEN.md REF-OQ-45). The handle is checked at the
        `[` (SPEC-03 M-17)."""
        if self.ctx == "kernel":
            raise Refused("an arena or pool in a kernel body is outside the surface cint_ref implements", e.pos)
        if len(e.items) != 1 or isinstance(e.items[0], P.Slice):
            raise Refused("a dereference with other than one handle: no clause gives the diagnostic", e.pos)
        arena = t.kind == "Arena"
        self.expect(e.index, HandleType(t.elem, arena))
        root = place_root(e.obj)
        writable = root is not None and self._writable_place(e.obj)
        e.deref = t.kind
        e.view = arena
        e.partial = False
        if arena:
            e.ty = ArrayType(t.elem, [Dim()], writable, True)
        else:
            e.ty = t.elem
        return e.ty

    def _slice(self, item, dim) -> Dim:
        """A slice item (SPEC-04 LS-161): `I64` bounds and a nonzero constant `by` step
        (C2053). Returns the dimension of the view, with its extent when every bound and
        the dimension's extent are known at compile time."""
        for b in (item.lo, item.hi):
            if b is None:
                continue
            tb = self.infer(b)
            if tb is None:
                self.settle(b, "I64")
            elif tb != "I64":
                error("C2001", b.start, "a slice bound has type I64, not %s (SPEC-04 LS-161)" % type_name(tb))
        item.step_value = 1
        if item.step is not None:
            ts = self.infer(item.step)
            if ts is None:
                self.settle(item.step, "I64")
            elif ts != "I64":
                error("C2001", item.step.start, "the step has type %s, not I64" % type_name(ts))
            if not self.is_const(item.step):
                error("C2053", item.step.start, "`by` takes a nonzero compile-time constant (SPEC-04 LS-161)")
            self.fold(item.step)
            if item.step.const.value == 0:
                error("C2053", item.step.start, "`by` takes a nonzero compile-time constant (SPEC-04 LS-161)")
            item.step_value = item.step.const.value
        bounds = []
        for b in (item.lo, item.hi):
            if b is None:
                bounds.append(None)
            elif self.is_const(b):
                self.fold(b)
                bounds.append(b.const.value)
            else:
                return Dim()
        if dim.extent is None:
            return Dim()
        valid, _, length, _, _ = views.slice_dim(dim.extent, bounds[0], bounds[1], item.inclusive, item.step_value)
        return Dim(length) if valid else Dim()

    def _infer_TypeArg(self, e):
        error("C3007", e.start, "the type name `%s` is not a value" % e.name)

    def _infer_TypeProp(self, e):
        if e.type_name in INT_TYPES and e.prop in ("min", "max"):
            e.ty = e.type_name
            return e.ty
        raise Refused("the property %s.%s is outside the scalar surface" % (e.type_name, e.prop), e.start)

    def _infer_Name(self, e):
        if self.box12 and e.name == "ArithError" and not any(e.name in s for s in self.scopes):
            error("C3007", e.start, "the error set name `ArithError` is not a value")
        if self.memory and e.name == "AllocError" and not any(e.name in s for s in self.scopes):
            error("C3007", e.start, "the error set name `AllocError` is not a value")
        sym = self.lookup(e.name, e.start)
        if sym.kind == "errorset":
            error("C3007", e.start, "the error set name `%s` is not a value" % e.name)
        if sym.kind in ("func", "kernel"):
            error("C4010", e.start, "`%s` names a %s without calling it" % (e.name, "function" if sym.kind == "func"
                                                                               else "kernel"))
        if sym.kind == "struct":
            error("C3007", e.start, "the struct name `%s` is not a value" % e.name)
        if sym.kind == "module":
            error("C3007", e.start, "the module name `%s` is not a value" % e.name)
        if self.ctx == "kernel" and sym.kind == "modvar":
            error("C5025", e.start, "a kernel body does not read or write the module-level variable `%s` (SPEC-04 "
                  "LS-250)" % e.name)
        if sym.kind == "param" and sym.mode == "out" and not getattr(e, "_write", False):
            raise Refused("a read of `out` parameter `%s` (SPEC-02 K-2): no clause gives the diagnostic" % e.name,
                          e.start)
        e.sym = sym
        e.ty = sym.type
        return e.ty

    def _infer_Unary(self, e):
        if e.op == "!":
            t = self.infer(e.operand)
            if t != BOOL:
                error("C2002", e.operand.start, "`!` needs a Bool operand; write `x == 0`")
            e.ty = BOOL
            return BOOL
        t = self.infer(e.operand)
        if t is None:
            return None
        self.require_int(t, e.operand)
        e.ty = t
        return t

    def _infer_Binary(self, e):
        if e.op in SHIFTS:
            tl = self.infer(e.left)
            tk = self.infer(e.right)
            if tk is None:
                self.settle(e.right, "I64")
            else:
                self.require_int(tk, e.right)
            if tl is None:
                return None
            self.require_int(tl, e.left)
            e.ty = tl
            return tl
        tl = self.infer(e.left)
        tr = self.infer(e.right)
        if tl is None and tr is None:
            return None
        if tl is None:
            self.require_int(tr, e.right)
            self.settle_rule4(e.left, tr)
            t = tr
        elif tr is None:
            self.require_int(tl, e.left)
            self.settle_rule4(e.right, tl)
            t = tl
        else:
            if tl != tr:
                error("C2001", e.right.start, "operands of `%s` have types %s and %s" % (e.op, type_name(tl), type_name(tr)))
            t = tl
        self.require_int(t, e)
        e.ty = t
        return t

    def _infer_Compare(self, e):
        tl = self.infer(e.left)
        tr = self.infer(e.right)
        if isinstance(tl, ArrayType) or isinstance(tr, ArrayType):
            error("C2013", e.start, "arrays are not compared with `%s`; `equal(a, b)` compares them (SPEC-04 LS-71)"
                  % e.op)
        if tl is None and tr is None:
            self.settle(e.left, "I64")
            self.settle(e.right, "I64")
            t = "I64"
        elif tl is None:
            self.settle_rule4(e.left, tr)
            t = tr
        elif tr is None:
            self.settle_rule4(e.right, tl)
            t = tl
        elif isinstance(tl, ErrorSet) and isinstance(tr, ErrorSet) and tl is not tr:
            # The operand of the narrower set converts to the wider one (SPEC-04 LS-97).
            if tl.holds(tr):
                t = tl
            elif tr.holds(tl):
                t = tr
            else:
                error("C2001", e.right.start, "operands of `%s` have the error sets %s and %s, and neither holds the "
                      "other (SPEC-04 LS-97)" % (e.op, tl.name, tr.name))
        else:
            if tl != tr:
                error("C2001", e.right.start, "operands of `%s` have types %s and %s" % (e.op, type_name(tl), type_name(tr)))
            t = tl
        if t in (VOID,) or isinstance(t, (StructType, HandleType, MemType)):
            # Handles, arenas and pools are not compared, as structs are not (ref/OPEN.md REF-OQ-45).
            error("C2103", e.start, "values of type %s cannot be compared" % type_name(t))
        if t == BOOL and e.op not in ("==", "!="):
            error("C2103", e.start, "Bool supports only `==` and `!=`")
        if isinstance(t, ErrorSet) and e.op not in ("==", "!="):
            error("C2103", e.start, "error-set values support only `==` and `!=` (SPEC-04 LS-314)")
        e.operand_type = t
        e.ty = BOOL
        return BOOL

    def _infer_Logical(self, e):
        self.condition(e.left)
        self.condition(e.right)
        e.ty = BOOL
        return BOOL

    def _infer_Cond(self, e):
        self.condition(e.cond)
        ta = self.infer(e.a)
        tb = self.infer(e.b)
        if ta is None and tb is None:
            return None
        if ta is None:
            self.settle(e.a, tb)
            t = tb
        elif tb is None:
            self.settle(e.b, ta)
            t = ta
        else:
            if ta != tb:
                error("C2001", e.b.start, "the branches of `?:` have types %s and %s" % (type_name(ta), type_name(tb)))
            t = ta
        e.ty = t
        return t

    def _infer_Convert(self, e):
        if e.op == "as?" and self.ctx == "kernel":
            error("C5022", e.start, "a kernel body does not use error unions; `as?` returns one (SPEC-04 LS-250)")
        target = self.resolve_type(e.target)
        # A literal operand, parenthesized or not, converts from its value in Z
        # (SPEC-01 IM-26, decision 2026-10-03, slice 2 patch D-21 option 1).
        src_lit = isinstance(e.operand, P.Lit)
        if src_lit:
            if e.operand.kind == "frac":
                raise Refused("a fraction literal (fixed point) is outside the scalar surface", e.operand.start)
            e.operand.ty = Z
            src = Z
        else:
            src = self.infer(e.operand)
            if src is None:
                self.settle(e.operand, "I64")
                src = "I64"
        if e.round_mode is not None:
            error("C2009", e.pos, "a `round` clause is permitted only where the conversion rounds")
        if target == BOOL:
            if is_int_type(src) or src == Z:
                error("C2056", e.start, "there is no conversion from integers to Bool; write `x != 0`")
            error("C2008", e.start, "there is no conversion from %s to Bool" % type_name(src))
        if isinstance(target, StructType) or isinstance(src, StructType) or src == VOID:
            error("C2008", e.start, "there is no conversion from %s to %s" % (type_name(src), type_name(target)))
        if isinstance(target, ErrorSet) or isinstance(src, ErrorSet):
            raise Refused("a conversion to or from an error-set value: LS-89 converts enums, and no clause says "
                          "whether error sets convert (ref/OPEN.md REF-OQ-44)", e.start)
        if src == BOOL and e.op != "as":
            error("C2008", e.start, "Bool converts to integers only with `as`")
        e.src_type = src
        e.target_type = target
        if e.op == "as?":
            # The result-returning form of `as`: `ArithError!T` (SPEC-01 IM-188; SPEC-04 LS-96).
            e.ty = ErrorUnion(ARITH_ERROR, target)
            return e.ty
        e.ty = target
        return target

    def _named_set(self, obj):
        """The error set that `obj`, the left side of `Set.name`, names: a declared or selected
        set, `alias.Set` of an imported module, or `ArithError`; else None."""
        if not self.box12:
            return None
        if isinstance(obj, P.Name) and not obj.paren and not any(obj.name in s for s in self.scopes):
            sym = self.globals.get(obj.name)
            if sym is not None and sym.kind == "errorset":
                return sym.type
            if sym is None and obj.name == "ArithError":
                return ARITH_ERROR
            if sym is None and obj.name == "AllocError" and self.memory:
                return ALLOC_ERROR
        if isinstance(obj, P.Field) and not obj.paren and self.is_alias(obj.obj):
            sym = self.globals[obj.obj.name].unit.globals.get(obj.field)
            if sym is not None and sym.kind == "errorset":
                return self.qualified(obj.obj.name, obj.field, obj.pos).type
        return None

    def set_value(self, s: ErrorSet, name, e) -> ErrVal:
        """The value called `name` in `s` (SPEC-04 LS-97): C3005 when `s` has none, C2044 when
        several member sets declare the name, which is then written with its own set."""
        found = s.named(name)
        if not found:
            error("C3005", e.pos, "error set %s has no value `%s`" % (s.name, name))
        if len(found) > 1:
            error("C2044", e.start, "%s holds %s; write the value with its set (SPEC-04 LS-97)"
                  % (s.name, " and ".join("%s.%s" % (v.set.name, v.name) for v in found)))
        return found[0]

    def _infer_Field(self, e):
        s = self._named_set(e.obj)
        if s is not None:
            # `Set.name`: a value of the set (SPEC-04 LS-92, LS-97).
            e.errval = self.set_value(s, e.field, e)
            e.ty = s
            return s
        if self.is_alias(e.obj):
            # `alias.NAME`: an exported constant or module-level variable (SPEC-04 LS-225, LS-227).
            sym = self.qualified(e.obj.name, e.field, e.pos)
            if sym.kind == "func":
                error("C4010", e.pos, "`%s` names a function without calling it" % e.field)
            if sym.kind == "struct":
                error("C3007", e.pos, "the struct name `%s` is not a value" % e.field)
            if sym.kind == "kernel":
                error("C4010", e.pos, "`%s` names a kernel without calling it" % e.field)
            if self.ctx == "kernel" and sym.kind == "modvar":
                error("C5025", e.start, "a kernel body does not read or write the module-level variable `%s.%s` "
                      "(SPEC-04 LS-250)" % (e.obj.name, e.field))
            e.sym = sym
            e.ty = sym.type
            return e.ty
        t = self.infer(e.obj)
        if not isinstance(t, StructType):
            error("C2103", e.pos, "a field access needs a struct value, found %s" % type_name(t))
        if e.field not in t.fields:
            error("C3005", e.pos, "struct %s has no field `%s`" % (t.name, e.field))
        e.ty = t.fields[e.field]
        if isinstance(e.ty, ArrayType) and _through_pool(e.obj):
            raise Refused("an array field of a pool element: no clause says how a view of it is checked "
                          "(ref/OPEN.md REF-OQ-45)", e.pos)
        return e.ty

    def _infer_Call(self, e):
        name = e.callee
        if e.qual is not None and self.memory and not self.is_alias(P.Name(e.start, e.start, e.qual)):
            recv = P.Name(e.start, e.start, e.qual)
            local = any(e.qual in s for s in self.scopes)
            if local or (e.qual in self.globals and self.globals[e.qual].kind != "module"):
                if isinstance(self.infer(recv), MemType):
                    return self._member(e, recv)
        if e.qual is not None:
            if not self.is_alias(P.Name(e.start, e.start, e.qual)):
                raise Refused("a member call is outside the surface cint_ref implements", e.pos)
            sym = self.qualified(e.qual, name, e.pos)
            if sym.kind == "func":
                return self._user_call(e, sym.decl)
            if sym.kind == "kernel":
                return self._dispatch(e, sym.decl)
            if sym.kind == "struct":
                return self._constructor(e, sym.type)
            error("C3007", e.pos, "`%s.%s` is not a function" % (e.qual, name))
        local = any(name in s for s in self.scopes)
        if not local and name in self.globals:
            sym = self.globals[name]
            if sym.kind == "func":
                return self._user_call(e, sym.decl)
            if sym.kind == "kernel":
                return self._dispatch(e, sym.decl)
            if sym.kind == "struct":
                return self._constructor(e, sym.type)
        if not local and name == "len" and "len" not in self.globals:
            return self._len(e)
        if not local and self.box09:
            if name in REDUCTIONS or (name in ("min", "max") and self._array_form(e)):
                return self._reduction(e)
            if name in VIEW_FUNCTIONS or name in ("copy", "fill", "extent", "size"):
                return self._array_builtin(e)
        if not local and self.box12 and name in RESULT_BUILTINS:
            return self._result_builtin(e)
        if not local and name in BUILTIN_SIGS:
            return self._builtin(e)
        if not local and name in self.prelude:
            raise Refused("the built-in `%s` is outside the scalar surface that cint_ref implements" % name, e.start)
        sym = self.lookup(name, e.start)
        error("C3007", e.start, "`%s` is not a function" % name)

    def _member(self, e, recv):
        """A member of an arena or a pool (SPEC-04 LS-145, LS-306; SPEC-03 M-14 to M-20;
        ref/OPEN.md REF-OQ-45): `alloc(n)`, `alloc_result(n)`, `child(n)` and `reset()` of an
        arena, `insert(v)`, `insert_result(v)` and `remove(h)` of a pool. Each changes the
        arena or pool, so it is named through a module-level name, a local or an `inout`
        parameter (C2067 otherwise). `n` is an `I64`."""
        t = recv.ty
        if self.ctx == "kernel":
            raise Refused("an arena or pool in a kernel body is outside the surface cint_ref implements", e.pos)
        members = ARENA_MEMBERS if t.kind == "Arena" else POOL_MEMBERS
        name = e.callee
        if name not in members:
            raise Refused("`%s` is not a member of %s: no clause gives the diagnostic" % (name, t.name), e.pos)
        named = next((a for a in e.args if a.name is not None), None)
        if named is not None:
            error("C2063", named.pos, "`%s` takes no named arguments" % name)
        if len(e.args) != members[name]:
            error("C2022", e.start, "`%s.%s` takes %d argument%s" % (recv.name, name, members[name],
                                                                     "" if members[name] == 1 else "s"))
        if not self._writable_place(recv):
            error("C2067", recv.start, "`%s` names %s through an `in` parameter, so `%s` cannot change it "
                  "(ref/OPEN.md REF-OQ-45)" % (recv.name, t.name, name))
        args = [a.value for a in e.args]
        if name in ("alloc", "alloc_result", "child"):
            self.expect(args[0], "I64")
            self.fold(args[0])
        elif name in ("insert", "insert_result"):
            self.expect(args[0], t.elem)
            self.fold(args[0])
        elif name == "remove":
            self.expect(args[0], HandleType(t.elem, False))
        if name in ("alloc", "insert"):
            e.ty = HandleType(t.elem, t.kind == "Arena")
        elif name in ("alloc_result", "insert_result"):
            e.ty = ErrorUnion(ALLOC_ERROR, HandleType(t.elem, t.kind == "Arena"))
        elif name == "child":
            e.ty = MemType("Arena", t.elem)
        else:
            e.ty = VOID
        e.kind = "member"
        e.recv = recv
        e.values = args
        return e.ty

    def _array_form(self, e) -> bool:
        """`min` and `max` are reductions when their last argument is an array: `min(xs)`,
        `min(init, xs)` (SPEC-04 LS-145); two scalars select the scalar form."""
        if not 1 <= len(e.args) <= 2 or any(a.name is not None for a in e.args):
            return False
        last = e.args[-1].value
        if isinstance(last, P.ArrayLit):
            return True
        if not isinstance(last, (P.Name, P.Index, P.Field, P.Call)):
            return False
        return isinstance(self.infer(last), ArrayType)

    def _reduction(self, e):
        """The reductions of SPEC-04 LS-145 over an array or view (SPEC-01 IM-77, IM-87):
        `sum(xs)`, `sum(R, xs)`, `fold_checked(op, init, xs)`, `sum_wrap(xs)`, `sum_sat(xs)`,
        `count(mask)`, `min(xs)`, `max(xs)`, `min(init, xs)`, `max(init, xs)` and
        `dot(R, a, b)`. In a kernel body a reduction is a `reduce` statement."""
        name = e.callee
        if self.ctx == "kernel":
            raise Refused("the built-in `%s` in a kernel body is outside the surface cint_ref implements; a kernel "
                          "reduces with a `reduce` statement (SPEC-02 R-1)" % name, e.start)
        named = next((a for a in e.args if a.name is not None), None)
        if named is not None:
            error("C2063", named.pos, "`%s` takes no named arguments" % name)
        args = [a.value for a in e.args]
        target = None
        if name in ("sum", "dot") and args and isinstance(args[0], P.TypeArg):
            ta = args.pop(0)
            if ta.name not in INT_TYPES:
                error("C3007", ta.start, "the type argument of `%s` is an integer type" % name)
            target = ta.name
        elif name == "dot":
            error("C3007", args[0].start if args else e.start, "the first argument of `dot` is its result type "
                  "(SPEC-04 LS-145)")
        op = None
        if name == "fold_checked":
            if not args or not (isinstance(args[0], P.Name) and args[0].name in ("add", "mul")):
                error("C3007", args[0].start if args else e.start, "the first argument of `fold_checked` is an "
                      "operation name, `add` or `mul` (SPEC-04 LS-146)")
            op = args.pop(0).name
        counts = {"dot": (2,), "fold_checked": (2,), "min": (1, 2), "max": (1, 2)}.get(name, (1,))
        if len(args) not in counts:
            error("C2022", e.start, "`%s` takes %s" % (name, " or ".join(
                "%d array argument%s" % (n, "s" if n > 1 else "") for n in counts)))
        arrays = args if name == "dot" else args[-1:]
        init = args[0] if name != "dot" and len(args) == 2 else None
        types = []
        for a in arrays:
            t = self.infer(a)
            if not isinstance(t, ArrayType):
                error("C2103", a.start, "`%s` needs an array or a view, found %s"
                      % (name, type_name(t) if t is not None else "a literal"))
            types.append(t)
        elem = types[0].elem
        if name == "count":
            if elem != BOOL:
                error("C2001", arrays[0].start, "`count` takes a Bool array, not elements of type %s (SPEC-04 LS-145)"
                      % type_name(elem))
            result = "I64"
        else:
            if not is_int_type(elem):
                error("C2103", arrays[0].start, "`%s` is defined on integer elements, not on %s" % (name, type_name(elem)))
            result = target or elem
        if name == "dot":
            if types[1].elem != elem:
                error("C2001", arrays[1].start, "the arguments of `dot` have elements of types %s and %s"
                      % (type_name(elem), type_name(types[1].elem)))
            for a, t in zip(arrays, types):
                if t.rank != 1:
                    raise Refused("`dot` of an array of rank %d: it is defined on rank 1 (SPEC-04 LS-145), and no "
                                  "clause gives the diagnostic" % t.rank, a.start)
        if init is not None:
            self.expect(init, elem)
        e.kind = "reduce"
        e.rname = {"min": "reduce_min", "max": "reduce_max"}.get(name, name)
        e.op = op
        e.init = init
        e.arrays = arrays
        e.values = ([init] if init is not None else []) + arrays
        e.elem = elem
        e.target = result
        e.ty = result
        return result

    def _array_builtin(self, e):
        """The array built-ins of SPEC-04 LS-145 that box 09 adds: the view functions
        `transpose(a)`, `reverse(a, d)` and `reshape(a, e1, ...)`, then `extent(a, d)`,
        `size(a)`, `copy(dst, src)` and `fill(dst, v)`."""
        name = e.callee
        named = next((a for a in e.args if a.name is not None), None)
        if named is not None:
            error("C2063", named.pos, "`%s` takes no named arguments" % name)
        args = [a.value for a in e.args]
        n = {"transpose": 1, "reverse": 2, "extent": 2, "size": 1, "copy": 2, "fill": 2}.get(name)
        if (n is not None and len(args) != n) or (name == "reshape" and len(args) < 2):
            error("C2022", e.start, "`%s` takes %s arguments" % (name, n if n is not None else "two or more"))
        if self.ctx == "kernel" and name not in ("extent", "size"):
            raise Refused("the built-in `%s` in a kernel body is outside the surface cint_ref implements" % name,
                          e.start)
        if name in ("copy", "fill"):
            return self._copy_fill(e, args)
        a = args[0]
        t = self.infer(a)
        if not isinstance(t, ArrayType):
            error("C2103", a.start, "`%s` needs an array or a view, found %s"
                  % (name, type_name(t) if t is not None else "a literal"))
        if name in ("extent", "reverse"):
            d = args[1]
            self._constant_dim(d, t, name)
        if name in ("extent", "size"):
            e.kind = "shape"
            e.values = args
            e.ty = "I64"
            return "I64"
        if name == "transpose":
            if t.rank != 2:
                raise Refused("`transpose` of an array of rank %d: it is defined on rank 2 (SPEC-04 LS-145), and no "
                              "clause gives the diagnostic" % t.rank, a.start)
            dims = [t.dims[1], t.dims[0]]
        elif name == "reverse":
            dims = list(t.dims)
        else:
            if len(args) - 1 > MAX_RANK:
                raise Refused("`reshape` to rank %d: cint-core-1 admits ranks 1 to 4 (SPEC-02 V-2)" % (len(args) - 1),
                              args[MAX_RANK + 1].start)
            dims = []
            for x in args[1:]:
                tx = self.infer(x)
                if tx is None:
                    self.settle(x, "I64")
                elif tx != "I64":
                    error("C2001", x.start, "an extent has type I64, not %s (SPEC-04 LS-62)" % type_name(tx))
                if self.is_const(x):
                    self.fold(x)
                    if x.const.value < 0:
                        raise Refused("`reshape` to a negative extent: no clause gives the diagnostic", x.start)
                    dims.append(Dim(x.const.value))
                else:
                    dims.append(Dim())
        e.kind = "view"
        e.view_of = a
        e.values = args
        e.ty = ArrayType(t.elem, dims, t.writable, True)
        return e.ty

    def _constant_dim(self, d, t, name):
        """The dimension argument of `extent` and `reverse`: an `I64` constant below the rank."""
        td = self.infer(d)
        if td is None:
            self.settle(d, "I64")
        elif td != "I64":
            error("C2001", d.start, "a dimension has type I64, not %s" % type_name(td))
        if not self.is_const(d):
            raise Refused("a dimension of `%s` that is not a compile-time constant: no clause gives its fault" % name,
                          d.start)
        self.fold(d)
        if not 0 <= d.const.value < t.rank:
            raise Refused("dimension %d of an array of rank %d: no clause gives the diagnostic"
                          % (d.const.value, t.rank), d.start)

    def _copy_fill(self, e, args):
        """`copy(dst, src)` and `fill(dst, v)` (SPEC-04 LS-70, LS-145): `dst` is a writable
        array or view (C2067); `copy` needs `src` of the same element type and rank, static
        extents that agree (C2012), and storage that is disjoint from `dst` or the same view
        (C5010 when known at compile time, SPEC-04 LS-70)."""
        dst, src = args
        td = self.infer(dst)
        if not isinstance(td, ArrayType):
            error("C2103", dst.start, "`%s` needs an array or a view, found %s"
                  % (e.callee, type_name(td) if td is not None else "a literal"))
        if not self._writable_place(dst):
            error("C2067", dst.start, "the destination of `%s` is not a writable place (SPEC-04 LS-145)" % e.callee)
        self._note_write(dst)
        if e.callee == "copy":
            self.expect_array(src, ArrayType(td.elem, [Dim(d.extent) for d in td.dims]))
            self._fold_place(dst)
            self._fold_place(src)
            self._static_copy_alias(dst, src)
        else:
            self.expect(src, td.elem)
        e.kind = e.callee
        e.values = args
        e.ty = VOID
        return VOID

    def _bind_args(self, e, names, what):
        """Map positional then named arguments onto `names`; returns [(index, expr)] in written order."""
        order, filled = [], {}
        positional = True
        for a in e.args:
            if a.name is None:
                if not positional:
                    error("C2069", a.pos, "positional arguments come before named arguments")
                idx = len(filled)
                if idx >= len(names):
                    error("C2022", e.start, "too many arguments for %s" % what)
            else:
                positional = False
                if a.name not in names:
                    error("C2063", a.pos, "%s has no parameter `%s`" % (what, a.name))
                idx = names.index(a.name)
                if idx in filled:
                    error("C2062", a.pos, "`%s` receives more than one argument" % a.name)
            if isinstance(a.value, P.TypeArg):
                error("C3007", a.pos, "the type name `%s` is not a value; only built-ins take a type argument"
                      % a.value.name)
            filled[idx] = a.value
            order.append((idx, a.value))
        return order, filled

    def _len(self, e):
        """`len(a)` of a rank-1 array or view: its extent, an `I64` (SPEC-04 LS-145)."""
        if len(e.args) != 1 or e.args[0].name is not None:
            error("C2022", e.start, "`len` takes one argument")
        a = e.args[0].value
        if isinstance(a, P.StrLit):
            t = self.expect_array(a, ArrayType(None, [Dim()], False, True))
        else:
            t = self.infer(a)
        if not isinstance(t, ArrayType):
            error("C2103", a.start, "`len` needs an array or a view, found %s"
                  % (type_name(t) if t is not None else "a literal"))
        if t.rank != 1:
            error("C2014", e.start, "`len` of an array of rank %d; use `extent(a, d)` or `size(a)` (SPEC-04 LS-145)"
                  % t.rank)
        e.kind = "len"
        e.arg = a
        e.ty = "I64"
        return "I64"

    def _user_call(self, e, f):
        if self.ctx == "kernel":
            raise Refused("a call in a kernel body is outside the surface cint_ref implements (SPEC-02 K-11)", e.start)
        order, filled = self._bind_args(e, [p.name for p in f.params], "`%s`" % f.name)
        if len(filled) != len(f.params):
            error("C2022", e.start, "`%s` takes %d arguments" % (f.name, len(f.params)))
        self.ensure_signature(f, e.start)
        for idx, a in order:
            self.expect(a, f.param_types[idx])
            if f.params[idx].mode == "inout" and not self._writable_place(a):
                error("C2067", a.start, "the argument of `inout` parameter `%s` is not a writable place "
                      "(SPEC-04 LS-119)" % f.params[idx].name)
        for a in filled.values():
            self._fold_place(a)                # constant indices and bounds decide C5010
        self._static_shapes(f, filled)
        self._static_alias(f, filled)
        self.callees.add(f)
        if not getattr(e, "_sited", False):    # `min` and `max` may type an argument twice
            e._sited = True
            self.call_sites.append((e, f, filled))
        e.kind = "user"
        e.func = f
        e.order = order
        e.ty = f.result_type
        return e.ty

    def _writable_place(self, a) -> bool:
        """`a` is a place that may be written: rooted at a variable, an `inout` (or, in a
        kernel, `out`) parameter, or an `inout` view variable (SPEC-04 LS-119, LS-162)."""
        root = place_root(a)
        if root is None:
            return False
        sym = root.sym
        if sym.kind == "param":
            return sym.mode in ("inout", "out")
        if sym.kind == "view":
            return sym.mode == "inout"
        return sym.kind in ("var", "modvar", "memory")

    def _note_write(self, a):
        """Record a write through the place `a` to module-level state (SPEC-04 LS-122)."""
        root = place_root(a)
        if root is None:
            return
        sym = root.sym
        if sym.kind == "view":
            sym = sym.base_sym
        if sym is not None and sym.kind == "modvar":
            self.writes.add(sym.key)

    def _fold_place(self, a):
        """Fold the constant index items and slice bounds along a place, so that place_path
        and static_view can decide C5010."""
        x = a
        while True:
            if isinstance(x, P.Index):
                for item in x.items:
                    if isinstance(item, P.Slice):
                        self.fold(item.lo)
                        self.fold(item.hi)
                    else:
                        self.fold(item)
                x = x.obj
            elif isinstance(x, P.Field) and getattr(x, "sym", None) is None:
                x = x.obj
            elif isinstance(x, P.Call) and getattr(x, "kind", None) == "view":
                for v in x.values[1:]:
                    self.fold(v)
                x = x.view_of
            else:
                return

    def static_view(self, e):
        """The view descriptor (buffer key, origin, shape, strides) of an array expression
        known at compile time: an owned variable of static shape, then constant indices,
        slices with constant bounds and the view functions; else None (SPEC-02 A-5, A-8)."""
        if isinstance(e, P.Name):
            sym = getattr(e, "sym", None)
            if sym is None or sym.kind not in ("var", "modvar") or not isinstance(sym.type, ArrayType):
                return None
            shape = sym.type.extents
            if shape is None:
                return None
            return (sym.key or id(sym), 0, tuple(shape), views.row_major(shape))
        if isinstance(e, P.Index) and getattr(e, "view", False):
            base = self.static_view(e.obj)
            if base is None:
                return None
            key, origin, shape, strides = base
            new_shape, new_strides = [], []
            for k, item in enumerate(e.items):
                if isinstance(item, P.Slice):
                    lo = _const_or(item.lo)
                    hi = _const_or(item.hi)
                    if lo is False or hi is False:
                        return None
                    valid, first, length, _, _ = views.slice_dim(shape[k], lo, hi, item.inclusive, item.step_value)
                    if not valid:
                        return None
                    origin += first * strides[k]
                    new_shape.append(length)
                    new_strides.append(strides[k] * item.step_value)
                else:
                    c = _const_or(item)
                    if c is False or not 0 <= c < shape[k]:
                        return None
                    origin += c * strides[k]
            return (key, origin, tuple(new_shape) + shape[len(e.items):], tuple(new_strides) + strides[len(e.items):])
        if isinstance(e, P.Call) and getattr(e, "kind", None) == "view":
            base = self.static_view(e.view_of)
            if base is None:
                return None
            key, origin, shape, strides = base
            if e.callee == "transpose":
                return (key, origin, shape[::-1], strides[::-1])
            if e.callee == "reverse":
                d = e.values[1].const.value
                s = list(strides)
                if shape[d] > 0:
                    origin += strides[d] * (shape[d] - 1)
                s[d] = -strides[d]
                return (key, origin, shape, tuple(s))
            extents = [_const_or(x) for x in e.values[1:]]
            if False in extents or views.count(extents) != views.count(shape) or strides != views.row_major(shape):
                return None
            return (key, origin, tuple(extents), views.row_major(extents))
        return None

    def _static_copy_alias(self, dst, src):
        """SPEC-04 LS-70: the source and destination of a copy are disjoint by the tests T0 to
        T2 of SPEC-02 A-5, or the same view (T3); any other pair that is known at compile time
        is C5010 at the source."""
        a, b = self.static_view(dst), self.static_view(src)
        if a is not None and b is not None and views.classify(a, b) in ("overlap", "uncertain"):
            error("C5010", src.start, "the source shares storage with the destination and is not the same view "
                  "(SPEC-04 LS-70)")

    def _static_shapes(self, f, filled):
        """C2012 where two views bound to one size parameter have static extents that
        differ (SPEC-04 LS-117); the rest is checked at entry (E_SHAPE)."""
        bound = {}
        for idx in range(len(f.params)):
            t = f.param_types[idx]
            if not isinstance(t, ArrayType):
                continue
            a = filled[idx]
            for k, d in enumerate(t.dims):
                if d.size is None:
                    continue
                n = a.ty.dims[k].extent
                if d.size in bound:
                    if n is not None and bound[d.size] is not None and n != bound[d.size]:
                        error("C2012", a.start, "extent %d where %d is required: both views bind `%s` (SPEC-04 LS-117)"
                              % (n, bound[d.size], d.size))
                else:
                    bound[d.size] = n

    def _static_alias(self, f, filled):
        """C5010 for an `inout` argument that provably overlaps another argument (SPEC-04
        LS-121), reported at the later of the two; an overlap that depends on run-time
        values is checked at entry (E_ALIAS)."""
        n = len(f.params)
        for i in range(n):
            for x in range(i + 1, n):
                if not (is_aggregate(f.param_types[i]) and is_aggregate(f.param_types[x])):
                    continue
                if f.params[i].mode != "inout" and f.params[x].mode != "inout":
                    continue
                o = overlap(place_path(filled[i]), place_path(filled[x]))
                if o == "unknown":
                    a, b = self.static_view(filled[i]), self.static_view(filled[x])
                    if a is not None and b is not None and views.classify(a, b) in ("identical", "overlap"):
                        o = "overlap"
                if o == "overlap":
                    error("C5010", filled[x].start, "an `inout` argument overlaps another argument of the call "
                          "(SPEC-04 LS-121)")

    def _constructor(self, e, st: StructType):
        e.callee_struct = st
        names = list(st.fields)
        order, filled = self._bind_args(e, names, "struct %s" % st.name)
        if len(filled) != len(names):
            missing = [n for i, n in enumerate(names) if i not in filled]
            error("C2020", e.start, "struct %s: missing field(s) %s" % (st.name, ", ".join(missing)))
        for idx, a in order:
            self.expect(a, st.fields[names[idx]])
        e.kind = "struct"
        e.struct = st
        e.order = order
        e.ty = st
        return st

    def _builtin(self, e):
        kind, n, has_type, has_mode = BUILTIN_SIGS[e.callee]
        args = list(e.args)
        named = next((a for a in args if a.name is not None), None)
        if named is not None:
            error("C2063", named.pos, "`%s` takes no named arguments" % e.callee)
        if len(args) != n + has_type + has_mode:
            error("C2022", e.start, "`%s` takes %d arguments" % (e.callee, n + has_type + has_mode))
        target = None
        if has_type:
            ta = args.pop(0).value
            if not isinstance(ta, P.TypeArg) or ta.name not in INT_TYPES:
                error("C3007", ta.start, "the first argument of `%s` is an integer type" % e.callee)
            target = ta.name
        mode = None
        if has_mode:
            ma = args.pop().value
            if not isinstance(ma, P.Name) or ma.name not in ROUND_MODES:
                error("C3007", ma.start, "expected a rounding-mode name (SPEC-01 4.5)")
            mode = ma.name
        values = [a.value for a in args]
        count = None
        if e.callee in ("rotl", "rotr"):
            count = values.pop()
            tk = self.infer(count)
            if tk is None:
                self.settle(count, "I64")
            else:
                self.require_int(tk, count)
        e.kind = "builtin"
        e.values, e.count, e.mode, e.target = values, count, mode, target
        types = [self.infer(v) for v in values]
        typed = [t for t in types if t is not None]
        for t, v in zip(types, values):
            if t is not None and t != typed[0]:
                error("C2001", v.start, "arguments of `%s` have types %s and %s" % (e.callee, type_name(typed[0]), type_name(t)))
        if typed:
            op_t = typed[0]
            for t, v in zip(types, values):
                if t is None:
                    self.settle(v, op_t)
        elif kind == "T":
            return None                    # typed later by context (SPEC-04 4.2 rule 4.7)
        else:
            op_t = "I64"
            for v in values:
                self.settle(v, op_t)
        return self._builtin_result(e, op_t)

    def _result_builtin(self, e):
        """`add_result(a, b)` and the other result-returning forms (SPEC-01 IM-28, IM-30): the
        operands are typed as the checked form's, by LS-56 rule 7 and then rule 8 (`I64`),
        since the result `ArithError!T` is not the operand type; `shl_result` types its count
        as a shift count is typed (LS-57)."""
        if self.ctx == "kernel":
            error("C5022", e.start, "a kernel body does not use error unions; `%s` returns one (SPEC-04 LS-250)"
                  % e.callee)
        named = next((a for a in e.args if a.name is not None), None)
        if named is not None:
            error("C2063", named.pos, "`%s` takes no named arguments" % e.callee)
        if len(e.args) != 2:
            error("C2022", e.start, "`%s` takes 2 arguments" % e.callee)
        values = [a.value for a in e.args]
        for v in values:
            if isinstance(v, P.TypeArg):
                error("C3007", v.start, "the type name `%s` is not a value" % v.name)
        shift = e.callee == "shl_result"
        ops = values[:1] if shift else values
        types = [self.infer(v) for v in ops]
        typed = [t for t in types if t is not None]
        for t, v in zip(types, ops):
            if t is not None and t != typed[0]:
                error("C2001", v.start, "arguments of `%s` have types %s and %s"
                      % (e.callee, type_name(typed[0]), type_name(t)))
        op_t = typed[0] if typed else "I64"
        for t, v in zip(types, ops):
            if t is None:
                self.settle(v, op_t)
        if shift:
            tk = self.infer(values[1])
            if tk is None:
                self.settle(values[1], "I64")
            else:
                self.require_int(tk, values[1])
        if not is_int_type(op_t):
            error("C2103", e.start, "`%s` needs integer arguments" % e.callee)
        e.kind = "result"
        e.op = RESULT_BUILTINS[e.callee]
        e.values = values
        e.operand_type = op_t
        e.ty = ErrorUnion(ARITH_ERROR, op_t)
        return e.ty

    def _builtin_result(self, e, op_t):
        if not is_int_type(op_t):
            error("C2103", e.start, "`%s` needs integer arguments" % e.callee)
        kind = BUILTIN_SIGS[e.callee][0]
        t = int_type(op_t)
        if e.callee == "uabs" and not (t.signed and t.width <= 64):
            error("C2103", e.start, "`uabs` is defined on I8 to I64")
        if e.callee == "mul_full":
            # SPEC-01 2.4: Specified for I8..I512 and, with the same signedness (4.10),
            # U8..U32; U64 to U128 is Proposed.
            if not t.signed and t.width == 64:
                raise Refused("`mul_full` on U64 (result U128) is Proposed (SPEC-01 2.4)", e.start)
            if t.width > (512 if t.signed else 32):
                error("C2103", e.start, "`mul_full` is defined on I8 to I512 and U8 to U32 in cint-core-1")
        if e.callee in ("rotl", "rotr") and (t.signed or t.width > 64):
            error("C2103", e.start, "`%s` is defined on unsigned types only" % e.callee)
        e.operand_type = op_t
        e.ty = {"T": op_t, "U": "U%d" % t.width, "W": "%s%d" % ("I" if t.signed else "U", 2 * t.width), "R": e.target}[kind]
        return e.ty

    # -- settling untyped expressions ----------------------------------------------------
    def settle_rule4(self, e, t):
        """Settle `e` from the other operand of a binary operator (SPEC-01 IM-21 rule 4).

        The left operand of a literal-only shift is never typed by rule 4: it takes
        the context of the whole shift by rules 1 to 3, 5 and 6 (SPEC-01 IM-21,
        SPEC-04 LS-57; decision 2026-10-03, slice 2 patch D-9). An untyped
        operand that passes its context to such a shift therefore settles to I64
        (rule 6), and an operand of another type is C2001 (`I8 + I64`).
        """
        if _untyped_shift(e) is not None:
            self.settle(e, "I64")
            if t != "I64":
                error("C2001", e.start, "expected %s, found I64: the left operand of a literal-only shift is "
                      "typed I64 here, never by the other operand (SPEC-04 LS-57)" % type_name(t))
            return
        self.settle(e, t)

    def settle(self, e, t):
        if isinstance(e, P.Lit):
            if e.kind == "frac":
                if is_int_type(t):
                    error("C2004", e.start, "a fraction literal cannot have the integer type %s" % t)
                raise Refused("a fraction literal (fixed point) is outside the scalar surface", e.start)
            if not is_int_type(t):
                error("C2001", e.start, "expected %s, found an integer literal" % type_name(t))
            e.ty = t            # the range check (C2003) runs when the literal is evaluated
        elif isinstance(e, P.Unary):
            self.require_int(t, e)
            self.settle(e.operand, t)
            e.ty = t
        elif isinstance(e, P.Binary):
            self.require_int(t, e)
            self.settle(e.left, t)
            if e.op not in SHIFTS:
                self.settle(e.right, t)
            e.ty = t
        elif isinstance(e, P.Cond):
            self.settle(e.a, t)
            self.settle(e.b, t)
            e.ty = t
        elif isinstance(e, P.Call) and getattr(e, "kind", None) == "builtin":
            for v in e.values:
                self.settle(v, t)
            self._builtin_result(e, t)
        elif isinstance(e, P.ErrorLit):
            # `.name` takes the error set of its context (SPEC-04 LS-60, LS-92); any other
            # context is none for it (ref/OPEN.md REF-OQ-43).
            if not isinstance(t, ErrorSet):
                error("C2007", e.start, "the error value `.%s` needs an error-set context (SPEC-04 LS-60)" % e.name)
            e.errval = self.set_value(t, e.name, e)
            e.ty = t
        else:
            raise AssertionError("settle on a typed expression")

    # -- constant expressions (SPEC-01 3.2, 8.4) ---------------------------------------
    def is_const(self, e) -> bool:
        c = getattr(e, "_is_const", None)       # memoized: fold asks at every level of a chain
        if c is None:
            c = e._is_const = self._is_const(e)
        return c

    def _is_const(self, e) -> bool:
        if isinstance(e, (P.Lit, P.BoolLit, P.TypeProp, P.ErrorLit)):
            return True
        if isinstance(e, P.Name):
            return e.sym.kind == "const"
        if isinstance(e, P.Field) and getattr(e, "errval", None) is not None:
            return True                           # an error value, `Set.name`
        if isinstance(e, P.Field) and getattr(e, "sym", None) is not None:
            return e.sym.kind == "const"          # an imported constant, `alias.NAME`
        if isinstance(e, P.Unary):
            return self.is_const(e.operand)
        if isinstance(e, (P.Binary, P.Compare, P.Logical)):
            return self.is_const(e.left) and self.is_const(e.right)
        if isinstance(e, P.Cond):
            return self.is_const(e.cond) and self.is_const(e.a) and self.is_const(e.b)
        if isinstance(e, P.Convert):
            return e.op != "as?" and self.is_const(e.operand)
        if isinstance(e, P.Call) and e.kind == "builtin":
            return all(self.is_const(v) for v in e.values) and (e.count is None or self.is_const(e.count))
        return False

    def fold(self, e):
        if e is None:
            return
        self._check_clamp_bounds(e)
        self._fold(e, False)

    def _fold(self, e, conditional):
        """Evaluate every maximal constant subexpression of `e` at compile time (D-9).

        `conditional` is true below the right operand of `&&` or `||` and below an
        arm of `?:` of a non-constant expression, where run time may never
        evaluate the subexpression. Such a constant subexpression is not evaluated
        at compile time: it runs, and faults, only if reached (SPEC-01 IM-23,
        SPEC-04 LS-264; ref/OPEN.md REF-OQ-21). Its literals are still
        range-checked and its literal conversions decided, in source order.

        A constant expression is evaluated with the run-time rules, so `&&`, `||`
        and `?:` evaluate only the operands the result requires; a fault on that
        path is C6001, and a literal on it is range-checked when it is evaluated
        (REF-OQ-02). After a fault-free evaluation every literal and literal
        conversion is checked, those of skipped operands included, in source
        order (C2003; C6001 `E_NARROW` at the `as`, D-17; REF-OQ-20).
        """
        if self.is_const(e):
            if not conditional:
                from .exec import const_eval
                try:
                    e.const = const_eval(e)
                except FaultSignal as f:
                    r = f.record
                    raise CompileError(Diagnostic("C6001", r.position, "%s during constant evaluation" % r.code,
                                                  Fault(r.code, r.operation, r.operands, r.exact, r.limit)))
            for x in walk(e):
                if isinstance(x, P.Lit) and x.kind != "frac" and is_int_type(getattr(x, "ty", None)):
                    _range_check(x)
                elif isinstance(x, P.Convert) and getattr(x, "src_type", None) == Z:
                    _literal_conversion(x)
            return
        if isinstance(e, P.Logical):
            self._fold(e.left, conditional)
            self._fold(e.right, True)
        elif isinstance(e, P.Catch):
            # The fallback runs only on an error, as an operand that `?:` may skip
            # (ref/OPEN.md REF-OQ-43).
            self._fold(e.left, conditional)
            if e.value is not None:
                self._fold(e.value, True)
        elif isinstance(e, P.Cond):
            self._fold(e.cond, conditional)
            self._fold(e.a, True)
            self._fold(e.b, True)
        else:
            for child in children(e):
                self._fold(child, conditional)

    def _check_clamp_bounds(self, e):
        """SPEC-01 4.8: `clamp` with constant bounds `lo > hi` is a compile error."""
        for c in walk(e):
            if not (isinstance(c, P.Call) and getattr(c, "kind", None) == "builtin" and c.callee == "clamp"):
                continue
            lo, hi = c.values[1], c.values[2]
            if getattr(c, "clamp_checked", False) or not (self.is_const(lo) and self.is_const(hi)):
                continue
            c.clamp_checked = True
            from .exec import const_eval
            try:
                a, b = const_eval(lo).value, const_eval(hi).value
            except (FaultSignal, CompileError):
                continue                 # the fold of the bounds reports it
            if a > b:
                error("C6006", c.start, "`clamp` with constant bounds lo = %d > hi = %d (SPEC-01 4.8)" % (a, b))

    # -- statements -------------------------------------------------------------------
    def check_block(self, stmts):
        self.scopes.append({})
        for s in stmts:
            self.check_stmt(s)
        self.scopes.pop()

    def check_stmt(self, s):
        getattr(self, "_stmt_" + type(s).__name__)(s)

    def _stmt_Block(self, s):
        self.check_block(s.stmts)

    def _stmt_VarDecl(self, s):
        if s.is_const:
            t = self.const_type(s)
            value = self.const_value(s, t)
            self.declare(s.name, s.name_pos, Sym("const", s.name, t, s.name_pos, s, value, "done"))
            return
        if s.mode is not None:
            return self._view_decl(s)
        if getattr(s.type, "capacity", None) is not None:
            error("C4012", s.pos, "an arena or pool declaration is module-level, not a statement of a function, test "
                  "or block (SPEC-03 M-20)")
        t = self.resolve_type(s.type, "local")
        if isinstance(t, MemType) and s.init is None:
            raise Refused("an %s variable without an initializer: no clause gives the diagnostic (ref/OPEN.md "
                          "REF-OQ-45)" % t.name, s.name_pos)
        if self.ctx == "kernel" and is_aggregate(t):
            if isinstance(t, StructType) or not (is_int_type(t.elem) or t.elem == BOOL):
                raise Refused("a private struct in a kernel body is outside the surface cint_ref implements",
                              s.name_pos)
            width = 8 if t.elem == BOOL else int_type(t.elem).width
            if views.count(t.extents) * width > 8 * KERNEL_PRIVATE_BYTES:
                raise Refused("a private array of more than 4,096 bytes (SPEC-04 LS-110, C5029): no clause gives the "
                              "position", s.name_pos)
        if s.init is None:
            # A handle is not a scalar: zero-filled, it is the null handle (SPEC-03 M-12;
            # ref/OPEN.md REF-OQ-45).
            if not is_aggregate(t) and not isinstance(t, HandleType):
                error("C2050", s.name_pos, "a scalar variable needs an initializer")
            if holds_error_value(t):
                error("C2054", s.pos, "no error value is 0, so a declaration that zero-fills %s needs an "
                      "initializer (SPEC-04 LS-91, LS-314)" % type_name(t))
        else:
            self.expect(s.init, t)
            self.fold(s.init)
        s.ty = t
        self.declare(s.name, s.name_pos, Sym("var", s.name, t, s.name_pos, s))

    def _view_decl(self, s):
        """`in T[...] v = x;` or `inout T[...] v = x;`: a view variable bound to the storage
        of `x`, read-only or writable (SPEC-04 LS-69, LS-106, LS-107, LS-162)."""
        if self.ctx == "kernel":
            raise Refused("a view variable in a kernel body is outside the surface cint_ref implements", s.pos)
        t = self.resolve_type(s.type, "view")
        if not isinstance(t, ArrayType):
            raise Refused("a view variable of a type that is not an array (Str is outside the surface cint_ref "
                          "implements)", s.type.pos)
        got = self.expect_array(s.init, t)
        root = place_root(s.init)
        if root is None:
            raise Refused("a view of storage that no variable holds: no clause gives the diagnostic", s.init.start)
        if s.mode == "inout" and not self._writable_place(s.init):
            error("C2067", s.init.start, "an `inout` view of storage that is not writable (SPEC-04 LS-162)")
        self._fold_place(s.init)
        self.fold(s.init)
        dims = [dt if dt.extent is not None else dg for dt, dg in zip(t.dims, got.dims)]
        t = ArrayType(t.elem, dims, s.mode == "inout", True)
        sym = Sym("view", s.name, t, s.name_pos, s, mode=s.mode)
        sym.base = place_path(s.init)
        sym.base_sym = root.sym.base_sym if root.sym.kind == "view" else root.sym
        s.ty = t
        self.declare(s.name, s.name_pos, sym)

    def _place(self, target, compound=False):
        root = target
        while isinstance(root, (P.Field, P.Index)) and not self.is_alias(getattr(root, "obj", None)):
            root = root.obj
        if isinstance(root, P.TypeArg):
            error("C3007", root.start, "the type name `%s` is not a value" % root.name)
        if not isinstance(root, (P.Name, P.Field)):
            error("C2058", target.start, "not an assignable place")
        root._write = True
        t = self.infer(target)
        sym = root.sym
        if self.ctx == "kernel" and sym.kind == "param" and sym.mode in ("out", "inout"):
            if not isinstance(sym.type, ArrayType):
                raise Refused("an assignment to scalar `out` `%s`, which only its `reduce` statement writes (SPEC-02 "
                              "K-2): no clause gives the diagnostic" % sym.name, target.start)
            if compound:
                raise Refused("a compound assignment to an element of `%s` `%s` in a kernel body is outside the "
                              "surface cint_ref implements" % (sym.mode, sym.name), target.start)
            self._write_rule(target, root)
            root.wkey = "\0w" + sym.name       # the staged copy (SPEC-02 P-4)
            self.fold(target)
            return t
        if sym.kind == "errbind":
            raise Refused("an assignment to `%s`, the error a `catch` block binds: no clause says whether it can be "
                          "assigned (ref/OPEN.md REF-OQ-44)" % sym.name, target.start)
        if sym.kind == "view":
            if sym.mode != "inout":
                raise Refused("a write through the `in` view `%s`: no clause gives the diagnostic" % sym.name,
                              target.start)
            self._note_write(target)
        if sym.kind == "param" and sym.mode != "inout":
            error("C2060", target.start, "parameter `%s` cannot be assigned%s (SPEC-04 LS-120)"
                  % (sym.name, "" if root is target else ": it is not `inout`"))
        if sym.kind == "sizeparam":
            error("C2060", target.start, "size parameter `%s` cannot be assigned (SPEC-04 LS-120)" % sym.name)
        if sym.kind == "loopvar":
            error("C2052", target.start, "the loop variable `%s` is read-only" % sym.name)
        if sym.kind == "const":
            error("C2058", target.start, "constant `%s` cannot be assigned" % sym.name)
        if sym.kind == "modvar":
            self.writes.add(sym.key)
        if isinstance(t, MemType) and root is target:
            error("C2058", target.start, "`%s` names %s and cannot be rebound, as a view variable cannot (SPEC-04 "
                  "LS-107; ref/OPEN.md REF-OQ-45)" % (sym.name, "an arena" if t.kind == "Arena" else "a pool"))
        self.fold(target)
        return t

    def _stmt_Assign(self, s):
        t = self._place(s.target, compound=s.op != "=")
        if s.op == "=":
            if isinstance(t, ArrayType) and isinstance(s.value, P.StrLit):
                raise Refused("a string literal assigned to an array (Str is outside the surface cint_ref "
                              "implements)", s.value.start)
            self.expect(s.value, t)
            if isinstance(t, ArrayType):
                self._fold_place(s.value)
                self._static_copy_alias(s.target, s.value)
        else:
            op = s.op[:-1]
            self.require_int(t, s.target)
            if op in SHIFTS:
                tk = self.infer(s.value)
                if tk is None:
                    self.settle(s.value, "I64")
                else:
                    self.require_int(tk, s.value)
            else:
                self.expect(s.value, t)
            s.binop = op
        s.ty = t
        self.fold(s.value)

    def _stmt_IncDec(self, s):
        t = self._place(s.target, compound=True)
        self.require_int(t, s.target)
        s.ty = t

    def _stmt_ExprStmt(self, s):
        t = self.infer(s.expr)
        if t is None:
            self.settle(s.expr, "I64")
            t = s.expr.ty
        if t != VOID:
            call = s.expr.operand if isinstance(s.expr, P.Try) else s.expr
            error("C4011", s.expr.start, "the result of `%s` must be used or discarded with `_ =`" % call.callee)
        self.fold(s.expr)

    def _stmt_Discard(self, s):
        s.expr._consumed = True         # `_ =` discards the value and any error (SPEC-04 LS-132)
        t = self.infer(s.expr)
        if t is None:
            self.settle(s.expr, "I64")
        elif t == VOID:
            error("C2001", s.expr.start, "a call of a void function has no value")
        self.fold(s.expr)

    def _stmt_Print(self, s):
        if self.ctx == "kernel":
            error("C5020", s.pos, "a kernel body does not print (SPEC-04 LS-250)")
        self.check_holes(s.parts)

    def _stmt_Reduce(self, s):
        """`reduce target = op(e);` (SPEC-02 R-1 to R-3, section 8.2; SPEC-04 LS-249): `target`
        is a scalar `out` parameter, the target of one `reduce` statement, which is not
        inside a loop. A rule without a diagnostic is refused."""
        if self.ctx != "kernel":
            raise Refused("a `reduce` statement outside a kernel body: no clause gives the diagnostic", s.pos)
        if any(k == "loop" for k, _ in self.loops):
            raise Refused("a `reduce` statement inside a loop (SPEC-02 R-1): no clause gives the diagnostic", s.pos)
        sym = self.lookup(s.target, s.target_pos)
        if sym.kind != "param" or sym.mode != "out" or isinstance(sym.type, ArrayType):
            raise Refused("the target of a `reduce` statement is a scalar `out` parameter (SPEC-02 R-1): no clause "
                          "gives the diagnostic", s.target_pos)
        if s.target in self.kreduce:
            raise Refused("a second `reduce` statement for `%s` (SPEC-02 R-1): no clause gives the diagnostic"
                          % s.target, s.pos)
        c = s.call
        if not (isinstance(c, P.Call) and c.qual is None and c.callee in REDUCE_OPS
                and not any(c.callee in scope for scope in self.scopes)):
            raise Refused("a `reduce` statement whose operation is not one of SPEC-02 8.2: no clause gives the "
                          "diagnostic", c.start)
        named = next((a for a in c.args if a.name is not None), None)
        if named is not None:
            error("C2063", named.pos, "`%s` takes no named arguments" % c.callee)
        rt = sym.type
        args = [a.value for a in c.args]
        op = init = None
        if c.callee == "fold_checked":
            if len(args) != 3:
                error("C2022", c.start, "`fold_checked` takes an operation, an init and a contribution")
            if not (isinstance(args[0], P.Name) and args[0].name in ("add", "mul")):
                error("C3007", args[0].start, "the first argument of `fold_checked` is an operation name, `add` or "
                      "`mul` (SPEC-04 LS-146)")
            op = args[0].name
            if op == "mul":
                raise Refused("`fold_checked(mul, ...)` in a kernel (SPEC-02 8.2, O-17): no clause gives the "
                              "diagnostic", args[0].start)
            init = args[1]
            self.expect(init, rt)
            uniform = self.is_const(init) or (isinstance(init, P.Name) and init.sym.kind == "param"
                                               and init.sym.mode == "in" and not isinstance(init.sym.type, ArrayType))
            if not uniform:
                raise Refused("a `fold_checked` init that is neither a constant nor a scalar `in` parameter: no "
                              "clause gives the diagnostic (SPEC-02 8.2)", init.start)
            self.fold(init)
            x = args[2]
        else:
            if len(args) != 1:
                error("C2022", c.start, "`%s` in a `reduce` statement takes one contribution" % c.callee)
            x = args[0]
        if c.callee == "count":
            self.expect(x, BOOL)
            if rt != "I64":
                error("C2001", s.target_pos, "the target of `count` has type I64, not %s (SPEC-02 8.2)"
                      % type_name(rt))
            elem = BOOL
        elif c.callee == "sum":
            elem = self.infer(x)
            if elem is None:
                self.settle(x, rt)
                elem = rt
            if not is_int_type(elem):
                error("C2103", x.start, "`sum` is defined on integer contributions, not on %s" % type_name(elem))
        else:
            self.expect(x, rt)
            elem = rt
        self.fold(x)
        s.op = c.callee
        s.rname = {"min": "reduce_min", "max": "reduce_max"}.get(c.callee, c.callee)
        s.fold_op = op
        s.init = init
        s.value = x
        s.elem = elem
        s.rtype = rt
        s.index = [p.name for p in self.kernel.params].index(s.target)
        self.kreduce[s.target] = s

    def check_holes(self, parts, skipped=False):
        """`skipped` for an assert message, which is never evaluated (SPEC-04 LS-191): its
        holes fold as skipped operands, so only the literal checks of IM-23 and the constant
        `clamp` bounds apply (G-C2 review COR-7)."""
        for h in parts:
            if isinstance(h, bytes):
                continue
            t = self.default_type(h.expr)
            et = t
            if isinstance(t, ArrayType):
                # SPEC-04 LS-214: `[a, b, c]`, each element with the hole's spec applied.
                if t.rank != 1:
                    raise Refused("the rendering of an array of rank above 1 is outside the surface cint_ref "
                                  "implements", h.pos)
                et = t.elem
                if isinstance(et, StructType):
                    raise Refused("the rendering of a struct is Proposed (SPEC-04 LS-214)", h.pos)
            if isinstance(et, ErrorSet):
                raise Refused("the rendering of an error value: SPEC-04 LS-212 names it, and no clause gives its "
                              "text (ref/OPEN.md REF-OQ-44)", h.pos)
            try:
                spec = fmt.parse_spec(h.spec) if h.spec is not None else None
                fmt.check(spec, et, h.conv, h.lanes)
            except fmt.SpecError as x:
                error(x.code, h.pos, x.message)
            if spec is not None and spec.width is not None and spec.width >= 1 << 63:
                raise Refused("a format width of 2^63 or more does not fit I64, and its text passes the 1 GiB "
                              "of a print statement (rt/OPEN.md RT-OQ-23): cint_ref does not implement it", h.pos)
            h.ty = t
            h.parsed = spec
            if skipped:
                self._check_clamp_bounds(h.expr)
                self._fold(h.expr, True)
            else:
                self.fold(h.expr)

    def _stmt_If(self, s):
        self.condition(s.cond)
        self.fold(s.cond)
        self.check_block(s.then.stmts)
        if s.else_ is not None:
            self.check_stmt(s.else_)

    def _loop_body(self, s, body):
        self.loops.append(("loop", s.label))
        self.check_block(body.stmts)
        self.loops.pop()

    def _stmt_While(self, s):
        self.loops.append(("header", None))
        self.condition(s.cond)
        self.fold(s.cond)
        self.loops.pop()
        self._loop_body(s, s.body)

    def _stmt_ForC(self, s):
        self.scopes.append({})
        self.loops.append(("header", None))
        self.check_stmt(s.init)
        self.condition(s.cond)
        self.fold(s.cond)
        self.check_stmt(s.update)
        self.loops.pop()
        self._loop_body(s, s.body)
        self.scopes.pop()

    def _stmt_ForRange(self, s):
        # SPEC-01 2.2: both bounds are I64; a literal bound takes the other bound's type
        # (SPEC-04 8.4), which can then only be I64.
        self.loops.append(("header", None))
        for b, t in ((s.lo, self.infer(s.lo)), (s.hi, self.infer(s.hi))):
            if t is None:
                self.settle(b, "I64")
            elif t != "I64":
                error("C2001", b.start, "a range bound has type %s, not I64 (SPEC-01 2.2)" % type_name(t))
        self.fold(s.lo)
        self.fold(s.hi)
        s.step_value = 1
        if s.step is not None:
            ts = self.infer(s.step)
            if ts is None:
                self.settle(s.step, "I64")
            elif ts != "I64":
                error("C2001", s.step.start, "the step has type %s, not I64" % type_name(ts))
            if not self.is_const(s.step):
                error("C2053", s.step.start, "`by` takes a nonzero compile-time constant")
            self.fold(s.step)
            if s.step.const.value == 0:
                error("C2053", s.step.start, "`by` takes a nonzero compile-time constant")
            s.step_value = s.step.const.value
        self.loops.pop()
        self.scopes.append({})
        self.declare(s.var, s.var_pos, Sym("loopvar", s.var, "I64", s.var_pos))
        self._loop_body(s, s.body)
        self.scopes.pop()

    def _stmt_Switch(self, s):
        self.loops.append(("header", None))
        t = self.default_type(s.scrutinee)
        if not (is_int_type(t) or t == BOOL or isinstance(t, ErrorSet)):
            error("C4026", s.scrutinee.start, "a switch over %s" % type_name(t))
        self.fold(s.scrutinee)
        # A switch over an error set (SPEC-04 LS-182, LS-314) matches on the scrutinee's tag in
        # its own set, the tags 1 to n of its n values.
        es = s.error_set = t if isinstance(t, ErrorSet) else None
        intervals = []
        for c in s.cases:
            c.ranges = []
            for lo, hi in c.items:
                if t == BOOL and hi is not None:
                    error("C2103", lo.start, "a range of Bool values")
                if es is not None and hi is not None:
                    raise Refused("a range of error values: no clause orders the values of an error set "
                                  "(ref/OPEN.md REF-OQ-44)", lo.start)
                bounds = []
                for x in (lo, hi):
                    if x is None:
                        continue
                    self.expect(x, t)
                    if not self.is_const(x):
                        error("C6004", x.start, "a case item must be a constant expression")
                    self.fold(x)
                    bounds.append(es.tags[x.const.value] if es is not None else x.const.value)
                a, b = bounds[0], bounds[-1]
                if t == BOOL:
                    a = b = int(a)
                if a > b:
                    error("C4024", lo.start, "an empty case range")
                for (pa, pb) in intervals:
                    if a <= pb and pa <= b:
                        error("C4021", lo.start, "overlapping or duplicate case item")
                intervals.append((a, b))
                c.ranges.append((a, b))
        self.loops.pop()
        if s.default is None:
            if es is not None:
                lo_t, hi_t = 1, len(es.values)
            else:
                lo_t, hi_t = (0, 1) if t == BOOL else (int_type(t).min, int_type(t).max)
            covered = lo_t
            for a, b in sorted(intervals):
                if a > covered:
                    break
                covered = max(covered, b + 1)
            if covered <= hi_t:
                error("C4020", s.pos, "the switch is not exhaustive: add `default` or cover every value")
        s.exhaustive = True
        clauses = s.cases + ([s.default] if s.default else [])
        self.loops.append(("switch", None))
        for i, c in enumerate(clauses):
            for j, st in enumerate(c.body):
                if isinstance(st, P.Fallthrough) and (j != len(c.body) - 1 or i == len(clauses) - 1):
                    error("C4014", st.pos, "`fallthrough;` must be the last statement of a clause that has a next clause")
            self.scopes.append({})
            for st in c.body:
                if isinstance(st, P.Defer):
                    # A clause is not a block (SPEC-04 18.5 `case_clause`), and `fallthrough`
                    # joins clauses, so LS-185 does not say which exit runs the statement.
                    raise Refused("a `%s` statement directly in a switch clause: no clause says when it runs "
                                  "(ref/OPEN.md REF-OQ-44)" % st.kind, st.pos)
                self.check_stmt(st)
            self.scopes.pop()
        self.loops.pop()

    def _jump(self, s, kw, switches):
        """`break` or `continue`: C4013 when no loop (or, for `break`, switch) is its target, and
        C4030 when a deferred statement lies between the jump and its target (SPEC-04 LS-187).
        In a `catch` block inside the header of a loop or switch (its condition, bounds, update,
        scrutinee or case items) it is refused: no clause says whether that loop or switch is
        the target (ref/OPEN.md REF-OQ-44)."""
        leaves = False
        for k, lab in reversed(self.loops):
            if k == "defer":
                leaves = True
            elif k == "header":
                raise Refused("`%s` in a `catch` block in the header of a loop or switch: no clause says whether "
                              "it leaves that statement (ref/OPEN.md REF-OQ-44)" % kw, s.pos)
            elif (k == "loop" and (s.label is None or lab == s.label)) or (k == "switch" and switches
                                                                            and s.label is None):
                if leaves:
                    error("C4030", s.pos, "`%s` would leave the deferred statement (SPEC-04 LS-187)" % kw)
                return
        if s.label is not None:
            error("C4013", s.pos, "no enclosing loop labeled `%s`" % s.label)
        error("C4013", s.pos, "`break` outside a loop or switch" if switches else "`continue` outside a loop")

    def _stmt_Break(self, s):
        self._jump(s, "break", True)

    def _stmt_Continue(self, s):
        self._jump(s, "continue", False)

    def _stmt_Return(self, s):
        if self.ctx == "kernel":
            raise Refused("a `return` in a kernel body (SPEC-02 K-11): no clause gives the diagnostic", s.pos)
        if any(k == "defer" for k, _ in self.loops):
            error("C4030", s.pos, "`return` would leave the deferred statement (SPEC-04 LS-187)")
        if isinstance(self.result, ErrorUnion):
            return self._union_return(s, self.result)
        if self.result == VOID:
            if s.value is not None:
                error("C2066", s.value.start, "a void function returns no value")
            return
        if s.value is None:
            error("C2066", s.pos, "`return` needs a value of type %s" % type_name(self.result))
        self.expect(s.value, self.result, "C2042")
        self.fold(s.value)

    def _union_return(self, s, r):
        """`return` in a function whose result is `E!T` (SPEC-04 LS-94, LS-97): `.name`, or a
        value of an error set that `E` holds, returns that error (C2042 for a set it does not
        hold); any other value is the success value, of type `T` (ref/OPEN.md REF-OQ-43)."""
        v = s.value
        s.is_error = False
        if v is None:
            if r.value != VOID:
                error("C2066", s.pos, "`return` needs a value of type %s" % type_name(r.value))
            return
        if isinstance(v, P.ErrorLit):
            self.settle(v, r.set)
            s.is_error = True
        else:
            t = self.infer(v)
            if isinstance(t, ErrorSet):
                if not r.set.holds(t):
                    error("C2042", v.start, "a value of %s is not a value of %s, the error set of this function "
                          "(SPEC-04 LS-97)" % (t.name, r.set.name))
                s.is_error = True
            elif r.value == VOID:
                error("C2066", v.start, "an `E!void` function returns no success value")
            else:
                if t is None and any(isinstance(x, P.ErrorLit) for x in walk(v)):
                    raise Refused("a returned expression that holds a context-typed error value: no clause says "
                                  "whether it returns an error (ref/OPEN.md REF-OQ-44)", v.start)
                self.accept(v, t, r.value)
        s.set = r.set
        self.fold(v)

    def _stmt_Fallthrough(self, s):
        if not any(k == "switch" for k, _ in self.loops):
            error("C4014", s.pos, "`fallthrough;` outside a switch")

    def _stmt_Defer(self, s):
        """`defer` or `errdefer` and a statement (SPEC-04 LS-185 to LS-189, LS-316 to LS-322),
        checked where it is declared. A `return`, `break`, `continue` or `try` that would leave
        it is C4030 (`_jump`, `_stmt_Return`, `_infer_Try`); an `errdefer` where no error can
        leave its block is C4031."""
        inside = any(k == "defer" for k, _ in self.loops)
        if self.ctx == "kernel":
            if s.kind == "errdefer":
                raise Refused("an `errdefer` in a kernel body is C5021 by SPEC-04 LS-250 and C4031 by LS-322: no "
                              "clause says which is reported (ref/OPEN.md REF-OQ-44)", s.pos)
            error("C5021", s.pos, "a kernel body does not use `defer` (SPEC-04 LS-250)")
        if s.kind == "errdefer":
            if inside:
                error("C4031", s.pos, "an `errdefer` inside a deferred statement, which no error can leave "
                      "(SPEC-04 LS-322)")
            if self.ctx == "script":
                error("C4031", s.pos, "an `errdefer` in a script, whose implicit entry returns no error union "
                      "(SPEC-04 LS-322)")
            if self.ctx == "function" and not isinstance(self.result, ErrorUnion):
                error("C4031", s.pos, "an `errdefer` in a function whose result is not an error union "
                      "(SPEC-04 LS-322)")
        elif inside:
            raise Refused("a `defer` inside a deferred statement: no clause says when it runs (ref/OPEN.md "
                          "REF-OQ-44)", s.pos)
        self.loops.append(("defer", None))
        if isinstance(s.body, P.Block):
            self.check_block(s.body.stmts)
        else:
            self.check_stmt(s.body)
        self.loops.pop()

    def _stmt_Assert(self, s):
        if self.ctx == "kernel":
            raise Refused("an `assert` in a kernel body (SPEC-02 K-11): no clause gives the diagnostic", s.pos)
        t = self.infer(s.cond)
        if t != BOOL:
            error("C2002", s.cond.start, "an assert condition must have type Bool")
        self.fold(s.cond)
        if s.message is not None:
            self.check_holes(s.message, skipped=True)

    def _stmt_StaticAssert(self, s):
        t = self.infer(s.cond)
        if t != BOOL:
            error("C2002", s.cond.start, "a static_assert condition must have type Bool")
        if not self.is_const(s.cond):
            error("C6004", s.cond.start, "a static_assert condition must be a constant expression")

        self.fold(s.cond)
        if not s.cond.const.value:
            error("C6010", s.pos, "static_assert failed")


def children(e):
    if isinstance(e, P.Unary):
        return [e.operand]
    if isinstance(e, (P.Binary, P.Compare, P.Logical)):
        return [e.left, e.right]
    if isinstance(e, P.Cond):
        return [e.cond, e.a, e.b]
    if isinstance(e, (P.Convert, P.Try)):
        return [e.operand]
    if isinstance(e, P.Catch):
        return [e.left] + ([e.value] if e.value is not None else [])
    if isinstance(e, P.Field):
        return [] if getattr(e, "sym", None) is not None or getattr(e, "errval", None) is not None else [e.obj]
    if isinstance(e, P.Index):
        out = [e.obj]
        for item in e.items:
            if isinstance(item, P.Slice):
                out += [x for x in (item.lo, item.hi, item.step) if x is not None]
            else:
                out.append(item)
        return out
    if isinstance(e, P.ArrayLit):
        return list(e.items)
    if isinstance(e, P.Call):
        if e.kind == "len":
            return [e.arg]
        if e.kind == "builtin":
            return list(e.values) + ([e.count] if e.count is not None else [])
        if e.kind in ("user", "struct"):
            return [a for _, a in e.order]
        return list(getattr(e, "values", []))
    return []


def _untyped_shift(e):
    """The first literal-only shift that an untyped expression `e` passes its context type to."""
    if isinstance(e, P.Binary):
        if e.op in SHIFTS:
            return e
        return _untyped_shift(e.left) or _untyped_shift(e.right)
    if isinstance(e, P.Unary):
        return _untyped_shift(e.operand)
    if isinstance(e, P.Cond):
        return _untyped_shift(e.a) or _untyped_shift(e.b)
    if isinstance(e, P.Call) and getattr(e, "kind", None) == "builtin":
        for v in e.values:
            r = _untyped_shift(v)
            if r is not None:
                return r
    return None


def _through_pool(e) -> bool:
    """Whether the place `e` passes through a pool dereference `p[h]`."""
    while isinstance(e, (P.Field, P.Index)):
        if isinstance(e, P.Index) and getattr(e, "deref", None) == "Pool":
            return True
        e = e.obj
    return False


def place_root(e):
    """The Name a place expression is rooted at, or None (a call, a literal, a constant). A
    view function's result is rooted where its operand is."""
    while True:
        if isinstance(e, P.Field) and getattr(e, "sym", None) is not None:
            return e if e.sym.kind == "modvar" else None
        if isinstance(e, (P.Field, P.Index)):
            e = e.obj
        elif isinstance(e, P.Call) and getattr(e, "kind", None) == "view":
            e = e.view_of
        else:
            break
    if isinstance(e, P.Name) and getattr(e, "sym", None) is not None and \
            e.sym.kind in ("var", "param", "modvar", "loopvar", "sizeparam", "view", "memory"):
        return e
    return None


def place_path(e):
    """(root, segments) of a place, or None. A segment is a field (`f`, name), an element
    (`i`, the index values, None where not constant), or a view of what precedes it (`v`:
    a slice, a partial index, a view function, a view variable), after which nothing more
    is known at compile time."""
    segs = []
    while True:
        if isinstance(e, P.Field) and getattr(e, "sym", None) is None:
            segs.append(("f", e.field))
            e = e.obj
        elif isinstance(e, P.Index):
            if getattr(e, "view", False):
                segs = [("v", None)]
            else:
                segs.append(("i", tuple(x.const.value if x.const is not None else None for x in e.items)))
            e = e.obj
        elif isinstance(e, P.Call) and getattr(e, "kind", None) == "view":
            segs = [("v", None)]
            e = e.view_of
        else:
            break
    root = e.sym if isinstance(e, (P.Name, P.Field)) and getattr(e, "sym", None) is not None else None
    if root is not None and root.kind == "view":
        if root.base is None:
            return None
        return (root.base[0], root.base[1] + [("v", None)])
    if root is None or root.kind not in ("var", "param", "modvar"):
        return None
    segs.reverse()
    return (root.key or root, segs)


def overlap(a, b) -> str:
    """Whether two places overlap: `disjoint`, `overlap` (provable), or `unknown` (index
    values, or a view, decide at run time)."""
    if a is None or b is None or a[0] is not b[0] and a[0] != b[0]:
        return "disjoint"
    unknown = False
    for (ka, va), (kb, vb) in zip(a[1], b[1]):
        if ka == "v" or kb == "v":
            return "unknown"
        if ka != kb:
            return "disjoint"
        if ka == "f" and va != vb:
            return "disjoint"
        if ka == "i":
            if any(x is not None and y is not None and x != y for x, y in zip(va, vb)):
                return "disjoint"
            if None in va or None in vb:
                unknown = True
    n = min(len(a[1]), len(b[1]))
    if any(k == "v" for k, _ in a[1][n:] + b[1][n:]):
        return "unknown"                  # a view of a part of the other place may be empty
    return "unknown" if unknown else "overlap"


def _const_or(x):
    """None for an omitted bound, the value of a folded constant, or False."""
    if x is None:
        return None
    c = getattr(x, "const", None)
    return c.value if c is not None else False


def shape_key(e):
    """A key for a shape expression of SPEC-02 K-1: `ident`, an integer literal, or
    `ident + c` or `ident - c`, so that two are compared symbol for symbol (K-8); None
    for any other expression."""
    if isinstance(e, P.Name) and not e.paren:
        return ("n", e.name)
    if isinstance(e, P.Lit) and e.kind == "int":
        return ("l", e.value)
    if isinstance(e, P.Binary) and e.op in ("+", "-") and not e.paren and isinstance(e.left, P.Name) \
            and not e.left.paren and isinstance(e.right, P.Lit) and e.right.kind == "int":
        return ("b", e.op, e.left.name, e.right.value)
    return None


def shape_value(d, sizes):
    """The value of a kernel parameter extent under the shape symbol values `sizes`, in
    exact arithmetic; None where it leaves I64 or names a symbol `sizes` lacks."""
    if d.extent is not None:
        return d.extent
    if d.size is not None:
        return sizes.get(d.size)
    key = shape_key(d.expr)
    if key is None or key[0] != "b" or key[2] not in sizes:
        return None
    v = sizes[key[2]] + key[3] if key[1] == "+" else sizes[key[2]] - key[3]
    return v if -(1 << 63) <= v <= (1 << 63) - 1 else None


def _substatements(s):
    """`s` and every statement nested in it, in source order."""
    yield s
    inner = []
    if isinstance(s, P.Block):
        inner = s.stmts
    elif isinstance(s, P.If):
        inner = s.then.stmts + ([s.else_] if s.else_ is not None else [])
    elif isinstance(s, (P.While, P.ForRange)):
        inner = s.body.stmts
    elif isinstance(s, P.ForC):
        inner = [s.init] + s.body.stmts + [s.update]
    elif isinstance(s, P.Switch):
        inner = [x for c in s.cases + ([s.default] if s.default else []) for x in c.body]
    for x in inner:
        yield from _substatements(x)


def _first_write(stmts, name):
    """The position of the first write to `name` in source order, or None."""
    for s in stmts:
        for x in _substatements(s):
            if isinstance(x, P.Assign):
                root = x.target
                while isinstance(root, (P.Index, P.Field)):
                    root = root.obj
                if isinstance(root, P.Name) and root.name == name:
                    return x.target.start
    return None


def _may_exit(s) -> bool:
    """`s` may leave the enclosing loop body: a `break` or `continue` outside any nested
    loop, or a labeled one anywhere."""
    if isinstance(s, (P.Break, P.Continue)):
        return True
    if isinstance(s, (P.While, P.ForC, P.ForRange)):
        return any(isinstance(x, (P.Break, P.Continue)) and x.label is not None for x in _substatements(s))
    if isinstance(s, P.Block):
        return any(_may_exit(x) for x in s.stmts)
    if isinstance(s, P.If):
        return any(_may_exit(x) for x in s.then.stmts) or (s.else_ is not None and _may_exit(s.else_))
    if isinstance(s, P.Switch):
        return any(_may_exit(x) for c in s.cases + ([s.default] if s.default else []) for x in c.body)
    return False


def _covers_list(stmts, p, d, block, loops) -> bool:
    for s in stmts:
        if _covers(s, p, d, block, loops):
            return True
        if loops and _may_exit(s):
            return False
    return False


def _covers(s, p, d, block, loops) -> bool:
    """Whether statement `s`, when it runs, writes every element of the owned block of the
    array `out` parameter `p` by the patterns of SPEC-02 K-10a: a write with the block
    indices of the loop nest `loops` (pattern 2; none for pattern 1), unconditionally, in
    both branches of an `if`, or in every clause of a `switch` with a `default`; or a nest
    of `for j in 0..E` loops, one per block dimension, over its extent."""
    if isinstance(s, P.Assign):
        t = s.target
        if s.op != "=" or not (isinstance(t, P.Index) and isinstance(t.obj, P.Name) and t.obj.name == p):
            return False
        tail = t.items[d:]
        return len(loops) == len(block) == len(tail) and all(
            isinstance(x, P.Name) and x.name == v for x, v in zip(tail, loops))
    if isinstance(s, P.Block):
        return _covers_list(s.stmts, p, d, block, loops)
    if isinstance(s, P.If):
        return s.else_ is not None and _covers_list(s.then.stmts, p, d, block, loops) and \
            _covers(s.else_, p, d, block, loops)
    if isinstance(s, P.Switch):
        return s.default is not None and all(_covers_list(c.body, p, d, block, loops)
                                             for c in s.cases + [s.default])
    if isinstance(s, P.ForRange):
        k = len(loops)
        if k >= len(block) or s.step is not None or s.inclusive or not (
                isinstance(s.lo, P.Lit) and s.lo.value == 0) or shape_key(s.hi) != block[k]:
            return False
        return _covers_list(s.body.stmts, p, d, block, loops + [s.var])
    return False


def walk(e):
    """`e` and every subexpression, parents first, left to right."""
    stack = [e]
    while stack:
        x = stack.pop()
        yield x
        stack.extend(reversed(children(x)))


def _literal_conversion(conv):
    """A checked conversion whose source is a literal, decided at compile time (D-17)."""
    from .exec import const_eval
    try:
        const_eval(conv)
    except FaultSignal as f:
        r = f.record
        raise CompileError(Diagnostic("C6001", r.position, "%s during constant evaluation" % r.code,
                                      Fault(r.code, r.operation, r.operands, r.exact, r.limit)))


def _range_check(lit):

    t = int_type(lit.ty)
    if not t.contains(lit.value):
        error("C2003", getattr(lit, "lit_start", lit.start), "constant %d does not fit %s (%d ..= %d)" % (lit.value, lit.ty, t.min, t.max))


def _item_pos(item):
    p = getattr(item, "name_pos", None) or item.pos
    return (p.line, p.column)


def can_complete(s) -> bool:
    """Whether statement `s` can complete normally (for C4001). A `fallthrough;` that is not
    a direct statement of a clause runs as a no-op, so it completes; a clause whose last
    statement is `fallthrough;` continues into the next clause and does not complete
    (ref/OPEN.md REF-OQ-14, G-C2 review COR-3)."""
    if isinstance(s, (P.Return, P.Break, P.Continue)):
        return False
    if isinstance(s, P.Block):
        return can_complete_list(s.stmts)
    if isinstance(s, P.If):
        return s.else_ is None or can_complete_list(s.then.stmts) or can_complete(s.else_)
    if isinstance(s, P.Switch):
        clauses = s.cases + ([s.default] if s.default else [])
        if not getattr(s, "exhaustive", False):
            return True
        for c in clauses:
            continues = bool(c.body) and isinstance(c.body[-1], P.Fallthrough)
            if _breaks_out(c.body) or (not continues and can_complete_list(c.body)):
                return True
        return False
    return True       # a nested fallthrough; loops are conservatively taken to complete (see ref/OPEN.md)


def can_complete_list(stmts) -> bool:
    for s in stmts:
        if not can_complete(s):
            return False
    return True


def _breaks_out(stmts) -> bool:
    """An unlabeled `break` in `stmts` that targets the enclosing switch, a `catch` block's
    among them (SPEC-04 LS-131)."""
    for s in stmts:
        if isinstance(s, P.Break) and s.label is None:
            return True
        if isinstance(s, P.Block) and _breaks_out(s.stmts):
            return True
        if isinstance(s, P.If):
            if _breaks_out(s.then.stmts) or (s.else_ is not None and _breaks_out([s.else_])):
                return True
        if any(_breaks_out(b.stmts) for x in _stmt_exprs(s) for b in _catch_blocks(x)):
            return True
    return False


def _stmt_exprs(s):
    """The expressions statement `s` evaluates, apart from those of the statements nested in
    it and of loop and switch headers, where a `catch` block has no `break` (`Checker._jump`)."""
    if isinstance(s, P.VarDecl):
        return [s.init] if s.init is not None else []
    if isinstance(s, P.Assign):
        return [s.target, s.value]
    if isinstance(s, P.IncDec):
        return [s.target]
    if isinstance(s, (P.ExprStmt, P.Discard)):
        return [s.expr]
    if isinstance(s, P.Print):
        return [h.expr for h in s.parts if not isinstance(h, bytes)]
    if isinstance(s, (P.If, P.Assert)):
        return [s.cond]
    if isinstance(s, P.Return):
        return [s.value] if s.value is not None else []
    return []


def _catch_blocks(e):
    """The blocks of the `catch (name) { ... }` expressions in `e`, outside any of those blocks."""
    return [x.block for x in walk(e) if isinstance(x, P.Catch) and x.block is not None]


def check_module(module: P.Module, load=None) -> Program:
    """Check a program whose root module is `module`. `load(rel)` returns the parsed module
    at the module-relative path `rel` (`a/b.ci` for `import a.b;`), or None when it cannot be
    read (C3009); without `load`, a program with an import is C3009."""
    return check_program(module, load)


_DEVICE_NAMES = {"con", "prn", "aux", "nul"} | {"%s%d" % (d, k) for d in ("com", "lpt") for k in range(1, 10)}


def module_path_rule(rel: str, others=()) -> str | None:
    """The CINTC-12 rule that the module path `rel` breaks, or None (SPEC-09 CINTC-12, SPEC-04
    LS-225): every segment, the last without `.ci`, is an identifier (LS-15) and not a Windows
    device name, and no path of `others` (the program's module paths so far) equals it under
    ASCII case folding."""
    segs = (rel[:-3] if rel.endswith(".ci") else rel).split("/")
    for s in segs:
        if not s or len(s) > 255 or s[0].isdigit() or any(
                not (c.isascii() and (c.isalnum() or c == "_")) for c in s):
            return "every segment of a module path, without the final .ci, is an identifier (SPEC-04 LS-15)"
        if s.lower() in _DEVICE_NAMES:
            return "a segment of a module path is not a Windows device name"
    folded = rel.encode("utf-8").lower()
    if any(o != rel and o.encode("utf-8").lower() == folded for o in others):
        return "two module paths of one program are equal under ASCII case folding"
    return None


def _path_error(rel: str, rule: str):
    error("C3030", Position(rel, 1, 1), "%s (SPEC-09 CINTC-12)" % rule)


def check_program(root: P.Module, load=None) -> Program:
    """Check every module path (C3030, CINTC-12) before it is read, load every imported module
    breadth first in import order, find import cycles depth first (C3003), and check the
    modules in the depth-first post-order of their imports, so a module is checked after every
    module it imports (SPEC-04 LS-226, LS-229, LS-230). `prog.state` lists module state in
    module order, the load order (SPEC-09 CONF-11 rule 10), not in check order."""
    rule = module_path_rule(root.path)
    if rule is not None:
        _path_error(root.path, rule)
    mods = {root.path: root}
    order = [root]
    i = 0
    while i < len(order):
        m = order[i]
        i += 1
        for imp in m.imports:
            rel = "/".join(imp.segments) + ".ci"
            imp.target = rel
            if rel not in mods:
                rule = module_path_rule(rel, mods)
                if rule is not None:
                    _path_error(rel, rule)
                found = load(rel) if load is not None else None
                if found is None:
                    error("C3009", imp.path_pos, "cannot read the module file %s" % rel)
                mods[rel] = found
                order.append(found)
    post, color, stack, nxt = [], {root.path: 1}, [root], {root.path: 0}
    while stack:
        m = stack[-1]
        k = nxt[m.path]
        if k < len(m.imports):
            nxt[m.path] = k + 1
            imp = m.imports[k]
            c = color.get(imp.target, 0)
            if c == 1:
                error("C3003", imp.path_pos, "import cycle: module `%s` is imported, directly or through other "
                      "modules, by the module it imports (SPEC-04 LS-230)" % module_name(imp.target))
            if c == 0:
                color[imp.target] = 1
                nxt[imp.target] = 0
                stack.append(mods[imp.target])
            continue
        color[m.path] = 2
        post.append(m)
        stack.pop()
    units, programs = {}, {}
    for m in post:
        c = Checker(m, units)
        programs[m.path] = c.check()
        units[m.path] = c
    _check_module_state_args([units[m.path] for m in post])
    prog = programs[root.path]
    prog.state = [((u.name, name), t, v) for u in (units[m.path] for m in order) for name, t, v in u.module_vars]
    prog.memories = [x for u in (units[m.path] for m in order) for x in u.memories]
    return prog


def _check_module_state_args(checkers):
    """C5012: an argument that is, or is a view of, a module-level variable is passed to a
    function that writes that variable, directly or through a function it calls (SPEC-04
    LS-122). Write sets are by name, over the whole program; call sites in check order."""
    writes, callees = {}, {}
    for c in checkers:
        for f, (w, cs) in c.func_writes.items():
            writes[f], callees[f] = set(w), set(cs)
    changed = True
    while changed:
        changed = False
        for f in writes:
            for g in callees[f]:
                extra = writes.get(g, set()) - writes[f]
                if extra:
                    writes[f] |= extra
                    changed = True
    for c in checkers:
        for e, f, filled in c.call_sites:
            for idx in range(len(f.params)):
                path = place_path(filled[idx])
                if path is not None and isinstance(path[0], tuple) and path[0] in writes.get(f, ()):
                    error("C5012", filled[idx].start, "the argument is module-level variable `%s`, which `%s` "
                          "writes (SPEC-04 LS-122)" % (path[0][1], f.name))
