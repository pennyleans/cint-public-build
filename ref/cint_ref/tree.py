"""Syntax tree of the surface `cint_ref` implements (SPEC-04 section 18).

Every expression node has `pos` (the operator token, or the token of a
primary; SPEC-04 17.1 and SPEC-01 9.2 use it as the canonical fault position)
and `start` (its first character, used for compile-error positions). The
checker adds `ty` (the static type) and `const` (the value of a constant
expression) to expression nodes.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from .faults import Position


class Node:
    pass


@dataclass(eq=False)
class TypeRef(Node):
    name: str
    pos: Position
    shape: object = None        # None, or the first extent: "_" or an expression (SPEC-04 LS-62, LS-66)
    module: str | None = None   # the import alias of a qualified struct name (`vec.V3`, SPEC-04 LS-225)
    dims: list | None = None    # every extent, in order (`I32[h, w]`); `shape` is the first
    inner: object = None        # the T of `Arena(T)`, `Pool(T)`, `Handle(T)` (SPEC-03 3.2)
    capacity: object = None     # the capacity expression of an arena or pool declaration (SPEC-03 M-20)


@dataclass(eq=False)
class Expr(Node):
    pos: Position
    start: Position

    def __post_init__(self):
        self.paren = False
        self.ty = None          # set by the checker
        self.const = None       # constant value, set by the checker for constant expressions


@dataclass(eq=False)
class Lit(Expr):
    value: object = None        # int, or Fraction for a fraction literal
    kind: str = "int"           # int, char or frac


@dataclass(eq=False)
class BoolLit(Expr):
    value: bool = False


@dataclass(eq=False)
class StrLit(Expr):
    parts: list = field(default_factory=list)


@dataclass(eq=False)
class Name(Expr):
    name: str = ""


@dataclass(eq=False)
class TypeArg(Expr):
    name: str = ""


@dataclass(eq=False)
class TypeProp(Expr):
    type_name: str = ""
    prop: str = ""


@dataclass(eq=False)
class Unary(Expr):
    op: str = ""
    operand: Expr = None


@dataclass(eq=False)
class Binary(Expr):
    op: str = ""
    left: Expr = None
    right: Expr = None


@dataclass(eq=False)
class Compare(Expr):
    op: str = ""
    left: Expr = None
    right: Expr = None


@dataclass(eq=False)
class Logical(Expr):
    op: str = ""
    left: Expr = None
    right: Expr = None


@dataclass(eq=False)
class Cond(Expr):
    cond: Expr = None
    a: Expr = None
    b: Expr = None


@dataclass(eq=False)
class Convert(Expr):
    op: str = ""
    operand: Expr = None
    target: TypeRef = None
    round_mode: str | None = None


@dataclass(eq=False)
class Arg(Node):
    name: str | None
    value: Expr
    pos: Position


@dataclass(eq=False)
class Call(Expr):
    callee: str = ""
    args: list = field(default_factory=list)
    qual: str | None = None     # the import alias of a qualified call (`vec.dot3(a, b)`)


@dataclass(eq=False)
class Index(Expr):
    """`obj[i, j, ...]` (SPEC-04 LS-64, LS-65, LS-161); `pos` is the `[` (SPEC-04 LS-278).
    `items` holds every index item, an expression or a `Slice`; `index` is the first."""
    obj: Expr = None
    index: Expr = None
    items: list | None = None

    def __post_init__(self):
        super().__post_init__()
        if self.items is None:
            self.items = [self.index]


@dataclass(eq=False)
class Slice(Node):
    """An index item `lo..hi`, `lo..=hi`, `..hi`, `lo..` or `..`, with an optional
    `by step` (SPEC-04 LS-161); `pos` is the `..` or `..=`."""
    lo: Expr | None
    hi: Expr | None
    inclusive: bool
    step: Expr | None
    pos: Position


@dataclass(eq=False)
class ArrayLit(Expr):
    """`[a, b, c]`, nested for rank 2 and above (SPEC-04 LS-67)."""
    items: list = field(default_factory=list)


@dataclass(eq=False)
class Field(Expr):
    obj: Expr = None
    field: str = ""


@dataclass(eq=False)
class ErrorLit(Expr):
    """`.name`, an error value typed by its context (SPEC-04 LS-60, LS-92); `pos` is the `.`."""
    name: str = ""


@dataclass(eq=False)
class Try(Expr):
    """`try e` (SPEC-04 LS-129); `pos` is the `try` keyword."""
    operand: Expr = None


@dataclass(eq=False)
class Catch(Expr):
    """`e catch value` or `e catch (name) { ... }` (SPEC-04 LS-130, LS-131); `pos` is the
    `catch` keyword. The block form has `bind`, `bind_pos` and `block`; the other, `value`."""
    left: Expr = None
    value: Expr | None = None
    bind: str | None = None
    bind_pos: Position | None = None
    block: Block | None = None


# Statements ------------------------------------------------------------------

@dataclass(eq=False)
class Stmt(Node):
    pos: Position


@dataclass(eq=False)
class Block(Stmt):
    stmts: list = field(default_factory=list)


@dataclass(eq=False)
class VarDecl(Stmt):
    type: TypeRef = None
    name: str = ""
    name_pos: Position = None
    init: Expr | None = None
    is_const: bool = False
    exported: bool = False
    init_pos: Position | None = None
    mode: str | None = None     # `in` or `inout` for a view variable (SPEC-04 LS-106)


@dataclass(eq=False)
class Assign(Stmt):
    target: Expr = None
    op: str = "="
    value: Expr = None


@dataclass(eq=False)
class IncDec(Stmt):
    target: Expr = None
    op: str = "++"


@dataclass(eq=False)
class ExprStmt(Stmt):
    expr: Expr = None


@dataclass(eq=False)
class Discard(Stmt):
    expr: Expr = None


@dataclass(eq=False)
class HoleExpr(Node):
    expr: Expr
    name: str | None       # source text for `{expr=}`
    conv: str | None
    lanes: str | None
    spec: str | None
    pos: Position


@dataclass(eq=False)
class Print(Stmt):
    parts: list = field(default_factory=list)   # bytes and HoleExpr


@dataclass(eq=False)
class If(Stmt):
    cond: Expr = None
    then: Block = None
    else_: Stmt | None = None


@dataclass(eq=False)
class While(Stmt):
    cond: Expr = None
    body: Block = None
    label: str | None = None


@dataclass(eq=False)
class ForC(Stmt):
    init: Stmt = None
    cond: Expr = None
    update: Stmt = None
    body: Block = None
    label: str | None = None


@dataclass(eq=False)
class ForRange(Stmt):
    var: str = ""
    var_pos: Position = None
    lo: Expr = None
    hi: Expr = None
    inclusive: bool = False
    step: Expr | None = None
    body: Block = None
    label: str | None = None


@dataclass(eq=False)
class Case(Node):
    items: list            # (lo, hi or None)
    body: list
    pos: Position


@dataclass(eq=False)
class Switch(Stmt):
    scrutinee: Expr = None
    cases: list = field(default_factory=list)
    default: Case | None = None


@dataclass(eq=False)
class Break(Stmt):
    label: str | None = None


@dataclass(eq=False)
class Continue(Stmt):
    label: str | None = None


@dataclass(eq=False)
class Return(Stmt):
    value: Expr | None = None


@dataclass(eq=False)
class Fallthrough(Stmt):
    pass


@dataclass(eq=False)
class Reduce(Stmt):
    """`reduce target = op(...);` in a kernel body (SPEC-02 R-1, SPEC-04 LS-249)."""
    target: str = ""
    target_pos: Position = None
    call: Expr = None


@dataclass(eq=False)
class Defer(Stmt):
    """`defer stmt;`, `defer { ... }`, or the same with `errdefer` (SPEC-04 LS-185, LS-189);
    `pos` is the keyword."""
    kind: str = "defer"         # defer or errdefer
    body: Stmt = None           # a Block or a simple statement


@dataclass(eq=False)
class Assert(Stmt):
    cond: Expr = None
    message: list | None = None


@dataclass(eq=False)
class StaticAssert(Stmt):
    cond: Expr = None
    message: bytes | None = None


# Declarations ------------------------------------------------------------------

@dataclass(eq=False)
class Param(Node):
    type: TypeRef
    name: str
    pos: Position
    mode: str = "in"            # `in` (the default) or `inout` (SPEC-04 LS-119); `out` in a kernel


@dataclass(eq=False)
class UnionType(Node):
    """`E!T` or `E!void`, the result type of a function (SPEC-04 LS-94, LS-95, LS-299)."""
    set: TypeRef
    value: TypeRef | None       # None for `E!void`
    pos: Position               # the first token of the set name


@dataclass(eq=False)
class ErrorDecl(Node):
    """`error Name [: U] { a, b }` or `error Name [: U] = A | B;` (SPEC-04 LS-92, LS-93, LS-97)."""
    name: str
    name_pos: Position
    underlying: TypeRef | None
    values: list | None         # [(name, position)] of a set that declares its values
    operands: list | None       # [TypeRef] of a combined set
    exported: bool = False
    pos: Position = None        # the `error` keyword


@dataclass(eq=False)
class FuncDecl(Node):
    name: str
    name_pos: Position
    result: TypeRef | UnionType | None      # None for void
    params: list
    body: Block
    exported: bool = False
    size_params: list = field(default_factory=list)   # [(name, position)] (SPEC-04 LS-117)


@dataclass(eq=False)
class KernelDecl(Node):
    """A kernel (SPEC-02 K-1, SPEC-04 LS-244): shape symbols, parameters with explicit
    modes, an optional `over` clause ([(name, position, extent)], None for the element
    form of K-12) and `where` constraints (comparisons)."""
    name: str
    name_pos: Position
    size_params: list           # [(name, position)]
    params: list
    over: list | None
    where: list
    body: Block
    exported: bool = False


@dataclass(eq=False)
class FieldDecl(Node):
    type: TypeRef
    name: str
    pos: Position


@dataclass(eq=False)
class StructDecl(Node):
    name: str
    name_pos: Position
    fields: list
    exported: bool = False


@dataclass(eq=False)
class TestDecl(Node):
    name: str
    pos: Position
    expect_fault: str | None
    body: Block
    expect_line: int | None = None   # `expect_fault E_NAME at N` (SPEC-04 LS-240)


@dataclass(eq=False)
class Import(Node):
    """`import a.b;`, `import a.b as x;` or `import a.b.{X, y};` (SPEC-04 LS-225 to LS-230)."""
    segments: list              # the identifiers of the module path
    pos: Position               # the `import` keyword
    path_pos: Position          # the first identifier of the module path
    alias: str | None           # the name the module is used by: the last segment or the `as` name
    alias_pos: Position | None
    selected: list | None = None    # [(name, position)] for the selected-names form


@dataclass
class Module:
    path: str
    functions: list = field(default_factory=list)
    structs: list = field(default_factory=list)
    consts: list = field(default_factory=list)       # top-level const declarations
    tests: list = field(default_factory=list)
    statements: list = field(default_factory=list)   # top-level statements and variable declarations
    static_asserts: list = field(default_factory=list)
    text: str = ""                                   # the source text (for notes, SPEC-04 LS-312)
    imports: list = field(default_factory=list)      # Import nodes, in source order
    profile: str | None = None                       # the `profile` line's name, if any
    kernels: list = field(default_factory=list)      # KernelDecl nodes (box 09)
    box09: bool = True                               # parsed with the box 09 surface (parser.parse_module)
    errors: list = field(default_factory=list)       # ErrorDecl nodes (box 12)
    box12: bool = True                               # parsed with the box 12 surface (parser.parse_module)
    memories: list = field(default_factory=list)     # module-level arena and pool declarations (VarDecl)
    memory: bool = True                              # parsed with the arena and pool surface
