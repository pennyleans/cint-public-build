"""Parser for the surface of `cint-core-1` that `cint_ref` implements (SPEC-04 section 18).

Produces the syntax tree of `tree.py`, with explicit source positions.

The surface is the scalar language plus, from slice 2 task 2.8, rank-1
arrays, size parameters, rank-1 `in` and `inout` view parameters, indexing,
`import`, and the `profile` line (D-15 rows 6 to 8). Box 09 adds arrays of
rank 2 to 4, partial indexing, slices, array literals, local view variables,
kernels with their `over` and `where` clauses, and `reduce` statements
(docs/design/notes/2026-10-05-box09-arrays-views-kernels.md). Box 12 adds
error sets, error unions as function results, `try`, `catch`, `as?`, the
context-typed error value `.name`, `defer` and `errdefer`
(docs/design/notes/2026-10-05-box12-errors-cleanup-lifetimes.md), and its memory
lifetimes group adds the types `Arena(T)`, `Pool(T)` and `Handle(T)` and the
declarations `Arena(T, capacity) name;` and `Pool(T, capacity) name;` (SPEC-03
M-20, spelling Proposed; ref/OPEN.md REF-OQ-45). Constructs
outside it (fixed point, enums, declared lower bounds and the like) raise
`Refused`: cint_ref does not decide them.

`parse_module(text, path, box09=False)` parses the surface as it was before box
09 and refuses the box 09 constructs at the same tokens as then;
`box12=False` does the same for the box 12 constructs, and lexes `errdefer` as
an identifier, as before box 12; `memory=False` refuses the arena and pool forms
at the same tokens as before them. The `front` and check suites of compiler/tests
judge cintc, which implements box 09 after roadmap box 08 and box 12 with its
compiler step, against that surface.
"""
from __future__ import annotations

import re

from .faults import FAULT_CODES, Position, Refused, error
from .lexer import BOX12_KEYWORDS, KEYWORDS, Hole, Lexer, Token, string_parts, tokenize
from .tree import *  # noqa: F401,F403  (other modules use the nodes as P.<Node>)
from .types import INT_TYPES

ASSIGN_OPS = ("=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "|=", "^=",
              "+%=", "-%=", "*%=", "<<%=", "+|=", "-|=", "*|=")
CMP_OPS = ("==", "!=", "<", "<=", ">", ">=")
ARITH_OPS = ("*", "/", "%", "*%", "*|", "+", "-", "+%", "-%", "+|", "-|")
SHIFT_OPS = ("<<", ">>", "<<%")
BITWISE_OPS = ("&", "|", "^")
SCALAR_TYPE_NAMES = set(INT_TYPES) | {"Bool"}
MEMORY_TYPES = ("Arena", "Pool", "Handle")    # SPEC-03 3.2, spelling Proposed
RESERVED_IDENT = re.compile(r"[IUQT][0-9]+\Z")


# ---------------------------------------------------------------------------
# Token streams
# ---------------------------------------------------------------------------


# Block nesting: the limit of SPEC-09 CINTC-02, 256 blocks inside a function or test body,
# which itself is not counted; `else if` arms and `switch` clauses do not nest. A block at
# depth 257 is C9004 at its `{` (SPEC-04 LS-311; decision 2026-10-03, slice 2 patch D-5).
MAX_BLOCK_NESTING = 256
# Expression nesting (parentheses, nested expressions, unary operators) and blocks together:
# cint_ref's own limit, far above the SPEC-04 LS-311 minimum of 256, because parsing,
# checking and execution run on a large stack (exec.run_program). Beyond it, C9004.
MAX_NESTING = 4096

class _ListStream:
    def __init__(self, tokens):
        self.toks = tokens
        self.i = 0

    def peek(self, k=0) -> Token:
        j = min(self.i + k, len(self.toks) - 1)
        return self.toks[j]

    def next(self) -> Token:
        t = self.toks[self.i]
        if t.kind != "eof":
            self.i += 1
        return t


class _LazyStream(_ListStream):
    """Lexes a hole on demand; a lexing error becomes an `error` token that ends the expression."""

    def __init__(self, text, path, line, col, keywords=KEYWORDS):
        super().__init__([])
        self.lexer = Lexer(text, path, line, col, keywords)
        self.done = False

    def _fill(self, j):
        from .faults import CompileError
        while len(self.toks) <= j and not self.done:
            start = self.lexer.i
            try:
                t = self.lexer.next_token()
            except CompileError:
                t = Token("error", "", None, self.lexer.line, self.lexer.col, start, start, False)
                self.done = True
            t.path = self.lexer.path
            if t.kind == "eof":
                self.done = True
            self.toks.append(t)

    def peek(self, k=0):
        self._fill(self.i + k)
        return self.toks[min(self.i + k, len(self.toks) - 1)]

    def next(self):
        t = self.peek()
        if t.kind not in ("eof", "error"):
            self.i += 1
        return t


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

class Parser:
    def __init__(self, stream, path: str, box09: bool = True, box12: bool = True, memory: bool = True):
        self.s = stream
        self.path = path
        self.box09 = box09
        self.box12 = box12
        self.memory = memory

    # helpers ------------------------------------------------------------
    def peek(self, k=0) -> Token:
        return self.s.peek(k)

    def next(self) -> Token:
        return self.s.next()

    def pos(self, t: Token) -> Position:
        return Position(self.path, t.line, t.col)

    def is_op(self, text, k=0) -> bool:
        t = self.peek(k)
        return t.kind == "op" and t.text == text

    def is_kw(self, text, k=0) -> bool:
        t = self.peek(k)
        return t.kind == "keyword" and t.text == text

    def expect_op(self, text, code=None, what=None) -> Token:
        t = self.peek()
        if t.kind == "op" and t.text == text:
            return self.next()
        error(code or "C1050", self.pos(t), "expected `%s`%s, found %s" % (text, " " + what if what else "",
                                                                           self.describe(t)))

    def expect_ident(self) -> Token:
        t = self.peek()
        if t.kind == "ident":
            return self.next()
        error("C1050", self.pos(t), "expected an identifier, found %s" % self.describe(t))

    @staticmethod
    def describe(t: Token) -> str:
        return "end of input" if t.kind == "eof" else "`%s`" % t.text

    def refuse(self, t: Token, what: str):
        raise Refused("%s is outside the surface that cint_ref implements" % what, self.pos(t))

    def decl_name(self) -> Token:
        t = self.expect_ident()
        if RESERVED_IDENT.match(t.text):
            error("C1010", self.pos(t), "`%s` is reserved for type names" % t.text)
        return t

    # types ----------------------------------------------------------------
    def is_type_start(self, k=0) -> bool:
        t = self.peek(k)
        if t.kind == "fixed_type":
            return True
        if t.kind != "ident":
            return False
        if t.text in SCALAR_TYPE_NAMES or t.text in ("Str", "T1", "T27", "PT5", "PT4", "Arena", "Pool", "Handle"):
            return True
        return self.peek(k + 1).kind == "ident"      # a struct type followed by a name

    def _bracket_end(self, k) -> int:
        """The index after the `]` matching the `[` at k, or 0 when the brackets do not close."""
        depth = 0
        while True:
            t = self.peek(k)
            if t.kind == "eof":
                return 0
            if t.kind == "op" and t.text == "[":
                depth += 1
            elif t.kind == "op" and t.text == "]":
                depth -= 1
                if depth == 0:
                    return k + 1
            k += 1

    def _paren_end(self, k) -> int:
        """The index after the `)` matching the `(` at k, or 0 when the parentheses do not close."""
        depth = 0
        while True:
            t = self.peek(k)
            if t.kind == "eof":
                return 0
            if t.kind == "op" and t.text == "(":
                depth += 1
            elif t.kind == "op" and t.text == ")":
                depth -= 1
                if depth == 0:
                    return k + 1
            k += 1

    def type_len(self, k=0) -> int:
        """The number of tokens of a type at k (`I64`, `vec.V3`, `Tok[8]`, `U8[_]`), or 0."""
        t = self.peek(k)
        if t.kind == "fixed_type":
            n = 1
        elif t.kind != "ident" or t.text == "_":
            return 0
        elif t.text not in SCALAR_TYPE_NAMES and self.is_op(".", k + 1) and self.peek(k + 2).kind == "ident":
            n = 3
        elif t.text in MEMORY_TYPES and self.is_op("(", k + 1):
            if not self.memory:
                self.refuse(t, "the type %s" % t.text)
            end = self._paren_end(k + 1)
            if end == 0:
                return 0
            n = end - k
        else:
            n = 1
        if self.is_op("[", k + n):
            end = self._bracket_end(k + n)
            if end == 0:
                return 0
            n = end - k
        return n

    def is_decl_ahead(self, k=0) -> bool:
        """A type followed by a name: a declaration (no expression has this form)."""
        n = self.type_len(k)
        return n > 0 and self.peek(k + n).kind == "ident"

    def union_len(self, k=0) -> int:
        """The number of tokens of an error union `E!T`, `E!void` or `E!(...)` at k (SPEC-04
        LS-299 `result_type`), or 0; always 0 without the box 12 surface."""
        if not self.box12 or self.peek(k).kind != "ident":
            return 0
        n = 3 if self.is_op(".", k + 1) and self.peek(k + 2).kind == "ident" else 1
        if not self.is_op("!", k + n):
            return 0
        j = k + n + 1
        if self.is_kw("void", j):
            return n + 2
        if self.is_op("(", j):
            depth = 0
            while True:
                t = self.peek(j)
                if t.kind == "eof":
                    return 0
                if t.kind == "op" and t.text == "(":
                    depth += 1
                elif t.kind == "op" and t.text == ")":
                    depth -= 1
                    if depth == 0:
                        return j + 1 - k
                j += 1
        m = self.type_len(j)
        return n + 1 + m if m else 0

    def function_ahead(self) -> bool:
        """A result type, a name, then `(` or `[`: a function declaration."""
        n = self.union_len() or (self.type_len() if self.is_decl_ahead() else 0)
        return n > 0 and self.peek(n).kind == "ident" and self.peek(n + 1).kind == "op" \
            and self.peek(n + 1).text in ("(", "[")

    def parse_type(self) -> TypeRef:
        t = self.peek()
        if t.kind == "fixed_type":
            self.refuse(t, "fixed point")
        if t.kind == "op" and t.text == "(":
            self.refuse(t, "a tuple type")
        if t.kind != "ident":
            error("C1050", self.pos(t), "expected a type, found %s" % self.describe(t))
        if self.memory and t.text in MEMORY_TYPES and self.is_op("(", 1):
            return self.parse_memory_type()
        if t.text in ("Str", "T1", "T27", "PT5", "PT4", "Arena", "Pool", "Handle"):
            self.refuse(t, "the type %s" % t.text)
        self.next()
        ref = TypeRef(t.text, self.pos(t))
        if self.is_op(".") and t.text not in SCALAR_TYPE_NAMES:
            # A struct name qualified by an import alias (SPEC-04 LS-225, LS-228).
            self.next()
            name = self.expect_ident()
            if self.is_op("."):
                self.refuse(self.peek(), "a type name qualified by more than one module")
            ref = TypeRef(name.text, self.pos(name), module=t.text)
            ref.start = self.pos(t)
        if self.is_op("!"):
            self.refuse(self.peek(), "an error union")
        if self.is_op("["):
            ref.dims = self.parse_shape()
            ref.shape = ref.dims[0]
        return ref

    def parse_memory_type(self) -> TypeRef:
        """`Arena(T)`, `Pool(T)` or `Handle(T)`, and in a declaration `Arena(T, capacity)` or
        `Pool(T, capacity)` (SPEC-03 M-20, spelling Proposed; SPEC-04 LS-300). `T` of a
        `Handle` may be `E[]`, the handle of an arena allocation (ref/OPEN.md REF-OQ-45)."""
        t = self.next()
        self.next()
        ref = TypeRef(t.text, self.pos(t))
        ref.inner = self.parse_type()
        ref.capacity = None
        if self.is_op(",") and t.text in ("Arena", "Pool"):
            self.next()
            ref.capacity = self.parse_expr()
        self.expect_op(")")
        if self.is_op("!"):
            self.refuse(self.peek(), "an error union")
        if self.is_op("["):
            ref.dims = self.parse_shape()
            ref.shape = ref.dims[0]
        return ref

    def parse_set_name(self) -> TypeRef:
        """An error set's name, `Name` or `alias.Name` (SPEC-04 LS-97, LS-299 `qualified`)."""
        t = self.expect_ident()
        ref = TypeRef(t.text, self.pos(t))
        if self.is_op("."):
            self.next()
            name = self.expect_ident()
            if self.is_op("."):
                self.refuse(self.peek(), "a type name qualified by more than one module")
            ref = TypeRef(name.text, self.pos(name), module=t.text)
            ref.start = self.pos(t)
        return ref

    def parse_union_type(self) -> UnionType:
        """`E!T` or `E!void`, the result type of a function (SPEC-04 LS-94, LS-95, LS-299)."""
        first = self.peek()
        s = self.parse_set_name()
        self.expect_op("!")
        if self.is_kw("void"):
            self.next()
            return UnionType(s, None, self.pos(first))
        value = self.parse_type()
        if value.shape is not None:
            self.refuse(self.peek(), "an array result")
        return UnionType(s, value, self.pos(first))

    def parse_shape(self) -> list:
        """A shape `[n]`, `[_]`, `[]` or `[h, w]` (SPEC-04 LS-62, LS-66): the extents, each
        `_` or an expression. Declared lower bounds are outside the surface cint_ref
        implements (T3)."""
        self.next()
        if self.is_op("]"):
            self.next()
            return ["_"]
        dims = []
        while True:
            if self.peek().kind == "ident" and self.peek().text == "_" and (self.is_op("]", 1) or self.is_op(",", 1)):
                self.next()
                dims.append("_")
            else:
                dims.append(self.parse_expr())
            if not self.is_op(","):
                break
            if not self.box09:
                self.refuse(self.peek(), "an array of rank above 1")
            self.next()
        if self.is_op("..") or self.is_op("..="):
            self.refuse(self.peek(), "a declared lower bound")
        self.expect_op("]")
        if self.is_op("["):
            self.refuse(self.peek(), "an array of arrays")
        return dims

    # module ---------------------------------------------------------------
    def parse_module(self, text: str = "") -> Module:
        m = Module(self.path, text=text)
        if self.is_kw("profile"):
            m.profile = self.parse_profile()
        while self.is_kw("import"):
            m.imports.append(self.parse_import())
        while self.peek().kind != "eof":
            t = self.peek()
            if t.kind == "keyword" and t.text == "import":
                error("C3012", self.pos(t), "import declarations come before every other item (SPEC-04 LS-226)")
            if t.kind == "keyword" and t.text == "profile":
                error("C1050", self.pos(t), "the `profile` line comes first in the module (SPEC-04 LS-299)")
            if t.kind == "op" and t.text == "@":
                self.refuse(t, "an attribute")
            exported = False
            if self.is_kw("export"):
                exported = True
                self.next()
                t = self.peek()
            if t.kind == "keyword" and t.text in ("enum", "error", "type", "kernel", "schedule") and \
                    not (t.text == "kernel" and self.box09) and not (t.text == "error" and self.box12):
                self.refuse(t, "`%s` declarations" % t.text)
            if self.is_kw("error"):
                m.errors.append(self.parse_error_decl(exported))
            elif self.is_kw("kernel"):
                m.kernels.append(self.parse_kernel(exported))
            elif self.is_kw("struct"):
                m.structs.append(self.parse_struct(exported))
            elif self.is_kw("test") and not exported:
                m.tests.append(self.parse_test())
            elif self.is_kw("static_assert") and not exported:
                m.static_asserts.append(self.parse_static_assert())
            elif self.is_kw("const"):
                d = self.parse_const()
                d.exported = exported
                m.consts.append(d)
            elif self.is_kw("void") or self.function_ahead():
                m.functions.append(self.parse_function(exported))
            elif self.is_kw("in") or self.is_kw("inout"):
                self.refuse(self.peek(), "a view result or a module-level view")
            elif exported:
                if self.is_decl_ahead():
                    d = self.parse_statement()
                    d.exported = True
                    if isinstance(d, VarDecl) and getattr(d.type, "capacity", None) is not None:
                        m.memories.append(d)
                    else:
                        m.statements.append(d)
                else:
                    error("C1050", self.pos(t), "`export` must precede a declaration")
            else:
                d = self.parse_statement()
                if isinstance(d, VarDecl) and getattr(d.type, "capacity", None) is not None:
                    m.memories.append(d)        # created with the context (SPEC-03 M-20; BX12-24)
                else:
                    m.statements.append(d)
        return m

    def parse_profile(self) -> str:
        """`profile "cint-core-1";` (SPEC-04 LS-299). cint_ref implements cint-core-1 only."""
        kw = self.next()
        t = self.peek()
        if t.kind != "string":
            error("C1050", self.pos(t), "expected the profile name as a string literal")
        self.next()
        name = b"".join(string_parts(t, allow_holes=False)).decode("utf-8")
        self.expect_op(";")
        if name != "cint-core-1":
            raise Refused("the profile %r is not cint-core-1, the profile cint_ref implements" % name, self.pos(kw))
        return name

    def parse_import(self) -> Import:
        """`import a.b;`, `import a.b as x;`, `import a.b.{X, y};` (SPEC-04 LS-299)."""
        kw = self.next()
        first = self.expect_ident()
        segments, last = [first.text], first
        selected = None
        while self.is_op("."):
            self.next()
            if self.is_op("{"):
                self.next()
                selected = []
                while True:
                    n = self.expect_ident()
                    selected.append((n.text, self.pos(n)))
                    if not self.is_op(","):
                        break
                    self.next()
                    if self.is_op("}"):
                        break
                self.expect_op("}")
                break
            last = self.expect_ident()
            segments.append(last.text)
        alias, alias_pos = (last.text, self.pos(last)) if selected is None else (None, None)
        if selected is None and self.is_kw("as"):
            self.next()
            a = self.decl_name()
            alias, alias_pos = a.text, self.pos(a)
        self.expect_op(";")
        return Import(segments, self.pos(kw), self.pos(first), alias, alias_pos, selected)

    def parse_error_decl(self, exported) -> ErrorDecl:
        """`error Name [: U] { a, b, ... }` or `error Name [: U] = A | B;` (SPEC-04 LS-92,
        LS-93, LS-97)."""
        kw = self.next()
        name = self.decl_name()
        underlying = None
        if self.is_op(":"):
            self.next()
            u = self.peek()
            if u.kind != "ident" or u.text not in INT_TYPES:
                error("C1050", self.pos(u), "expected the integer type of the error set, found %s" % self.describe(u))
            self.next()
            underlying = TypeRef(u.text, self.pos(u))
        values = operands = None
        if self.is_op("="):
            self.next()
            operands = [self.parse_set_name()]
            while self.is_op("|"):
                self.next()
                operands.append(self.parse_set_name())
            self.expect_op(";")
        else:
            self.expect_op("{")
            values = []
            while True:
                v = self.decl_name()
                values.append((v.text, self.pos(v)))
                if not self.is_op(","):
                    break
                self.next()
                if self.is_op("}"):
                    break
            self.expect_op("}")
        return ErrorDecl(name.text, self.pos(name), underlying, values, operands, exported, self.pos(kw))

    def parse_struct(self, exported) -> StructDecl:
        self.next()
        name = self.decl_name()
        self.expect_op("{")
        fields = []
        while not self.is_op("}"):
            if self.is_op("@"):
                self.refuse(self.peek(), "an attribute")
            ty = self.parse_type()
            fname = self.decl_name()
            if self.is_op(":"):
                self.refuse(self.peek(), "a bit field")
            if self.is_op("="):
                self.refuse(self.peek(), "a field default")
            self.expect_op(";")
            fields.append(FieldDecl(ty, fname.text, self.pos(fname)))
        self.expect_op("}")
        return StructDecl(name.text, self.pos(name), fields, exported)

    def parse_function(self, exported) -> FuncDecl:
        if self.is_kw("void"):
            self.next()
            result = None
        else:
            if self.is_kw("in") or self.is_kw("inout"):
                self.refuse(self.peek(), "a view result")
            if self.union_len():
                result = self.parse_union_type()
            else:
                result = self.parse_type()
                if result.shape is not None:
                    self.refuse(self.peek(), "an array result")
        name = self.decl_name()
        size_params = []
        if self.is_op("["):
            self.next()
            while True:
                s = self.decl_name()
                size_params.append((s.text, self.pos(s)))
                if not self.is_op(","):
                    break
                self.next()
            self.expect_op("]")
        self.expect_op("(")
        params = []
        while not self.is_op(")"):
            mode = "in"
            first = self.peek()
            if self.is_kw("out"):
                error("C5001", self.pos(self.peek()), "`out` is for kernels and extern declarations; a function "
                                                     "writes through `inout` (SPEC-04 LS-119)")
            if self.is_kw("in") or self.is_kw("inout"):
                mode = self.next().text
            ty = self.parse_type()
            pname = self.decl_name()
            if self.is_op("="):
                self.refuse(self.peek(), "a default argument")
            params.append(Param(ty, pname.text, self.pos(pname), mode))
            params[-1].start = self.pos(first)
            if not self.is_op(")"):
                self.expect_op(",")
        self.expect_op(")")
        if self.is_kw("where"):
            self.refuse(self.peek(), "a `where` clause")
        body = self.parse_block(counted=False)
        return FuncDecl(name.text, self.pos(name), result, params, body, exported, size_params)

    def parse_kernel(self, exported) -> KernelDecl:
        """`kernel name[s, ...](mode type name, ...) [over [v: e, ...]] [where c, ...] { ... }`
        (SPEC-02 K-1, SPEC-04 LS-244). Every parameter has an explicit mode."""
        self.next()
        name = self.decl_name()
        self.expect_op("[")
        size_params = []
        while True:
            s = self.decl_name()
            size_params.append((s.text, self.pos(s)))
            if not self.is_op(","):
                break
            self.next()
        self.expect_op("]")
        self.expect_op("(")
        params = []
        while not self.is_op(")"):
            first = self.peek()
            if not (self.is_kw("in") or self.is_kw("out") or self.is_kw("inout")):
                error("C1050", self.pos(first), "a kernel parameter has a mode: `in`, `out` or `inout` "
                                                "(SPEC-04 LS-244)")
            mode = self.next().text
            ty = self.parse_type()
            pname = self.decl_name()
            params.append(Param(ty, pname.text, self.pos(pname), mode))
            params[-1].start = self.pos(first)
            if not self.is_op(")"):
                self.expect_op(",")
        self.expect_op(")")
        over = None
        if self.is_kw("over"):
            self.next()
            self.expect_op("[")
            over = []
            while True:
                v = self.decl_name()
                self.expect_op(":")
                extent = self.parse_expr()
                if self.is_op("..="):
                    self.refuse(self.peek(), "a declared lower bound")
                over.append((v.text, self.pos(v), extent))
                if not self.is_op(","):
                    break
                self.next()
            self.expect_op("]")
        where = []
        if self.is_kw("where"):
            self.next()
            while True:
                where.append(self.parse_expr())
                if not self.is_op(","):
                    break
                self.next()
        body = self.parse_block(counted=False)
        return KernelDecl(name.text, self.pos(name), size_params, params, over, where, body, exported)

    def parse_test(self) -> TestDecl:
        kw = self.next()
        t = self.peek()
        if t.kind != "string":
            error("C1050", self.pos(t), "expected the test name as a string literal")
        self.next()
        name = b"".join(string_parts(t, allow_holes=False)).decode("utf-8")
        expect = line = None
        if self.is_kw("expect_fault"):
            self.next()
            f = self.expect_ident()
            if f.text not in FAULT_CODES:
                error("C1011", self.pos(f), "`%s` is not a fault name of SPEC-01 9.1" % f.text)
            expect = f.text
            if self.peek().kind == "ident" and self.peek().text == "at":
                # `expect_fault E_NAME at N`: N is a DEC_INT line number (SPEC-04 LS-240, LS-299).
                self.next()
                n = self.peek()
                if n.kind != "int" or not n.text[:1].isdigit() or n.text[:2] in ("0x", "0b", "0o", "0t"):
                    error("C1050", self.pos(n), "expected a decimal line number after `at`")
                self.next()
                line = n.value
        body = self.parse_block(counted=False)
        return TestDecl(name, self.pos(kw), expect, body, line)

    def parse_static_assert(self) -> StaticAssert:
        kw = self.next()
        self.expect_op("(")
        cond = self.parse_expr()
        message = None
        if self.is_op(","):
            self.next()
            t = self.peek()
            if t.kind != "string":
                error("C1050", self.pos(t), "expected a string literal")
            self.next()
            message = b"".join(string_parts(t, allow_holes=False))
        self.expect_op(")")
        self.expect_op(";")
        return StaticAssert(self.pos(kw), cond, message)

    def parse_const(self) -> VarDecl:
        kw = self.next()
        ty = self.parse_type()
        name = self.decl_name()
        init_pos = self.pos(self.expect_op("="))
        init = self.parse_expr()
        self.expect_op(";")
        return VarDecl(self.pos(kw), ty, name.text, self.pos(name), init, True, init_pos=init_pos)

    # nesting limits (SPEC-04 LS-311, SPEC-09 CINTC-02) ------------------------------
    def _enter(self, t: Token):
        self.nesting = getattr(self, "nesting", 0) + 1
        if self.nesting > MAX_NESTING:
            error("C9004", self.pos(t), "block and expression nesting deeper than cint_ref's limit of %d "
                                        "(SPEC-04 LS-311)" % MAX_NESTING)

    def _leave(self):
        self.nesting -= 1

    # statements -------------------------------------------------------------
    def parse_block(self, counted=True) -> Block:
        t = self.peek()
        if not (t.kind == "op" and t.text == "{"):
            error("C1050", self.pos(t), "expected `{`, found %s" % self.describe(t))
        self.next()
        depth = getattr(self, "blocks", 0) + (1 if counted else 0)
        if depth > MAX_BLOCK_NESTING:
            error("C9004", self.pos(t), "a block at depth %d: blocks nest at most %d deep (SPEC-09 CINTC-02, "
                                        "SPEC-04 LS-311)" % (depth, MAX_BLOCK_NESTING))
        saved, self.blocks = getattr(self, "blocks", 0), depth
        self._enter(t)
        stmts = []
        while not self.is_op("}"):
            if self.peek().kind == "eof":
                error("C1050", self.pos(self.peek()), "unterminated block: expected `}`, found end of input")
            stmts.append(self.parse_statement())
        self.next()
        self._leave()
        self.blocks = saved
        return Block(self.pos(t), stmts)

    def parse_statement(self) -> Stmt:
        t = self.peek()
        if t.kind == "op":
            if t.text == "{":
                return self.parse_block()
            if t.text == ";":
                error("C1040", self.pos(t), "there is no empty statement")
            if t.text == "(" and self.is_type_start(1) and self.peek(2).kind == "ident":
                self.refuse(t, "destructuring")
        if t.kind == "keyword":
            k = t.text
            if k == "const":
                return self.parse_const()
            if self.box09 and k in ("in", "inout"):
                return self.parse_view_decl()
            if self.box09 and k == "reduce":
                return self.parse_reduce()
            if self.box12 and k in ("defer", "errdefer"):
                return self.parse_defer()
            if k in ("var", "in", "inout", "defer", "do", "try", "reduce", "in_place", "kernel") and \
                    not (self.box12 and k == "try"):
                self.refuse(t, "`%s`" % k)
            if k == "expect_fault":
                error("C3021", self.pos(t), "`expect_fault` is permitted only in a test header")
            if k == "if":
                return self.parse_if()
            if k == "while":
                return self.parse_while(None)
            if k == "for":
                return self.parse_for(None)
            if k == "switch":
                return self.parse_switch()
            if k in ("break", "continue"):
                self.next()
                label = self.next().text if self.peek().kind == "ident" else None
                self.expect_op(";")
                return (Break if k == "break" else Continue)(self.pos(t), label)
            if k == "return":
                self.next()
                value = None if self.is_op(";") else self.parse_expr()
                self.expect_op(";")
                return Return(self.pos(t), value)
            if k == "fallthrough":
                self.next()
                self.expect_op(";")
                return Fallthrough(self.pos(t))
            if k == "assert":
                return self.parse_assert()
            if k == "static_assert":
                return self.parse_static_assert()
        if t.kind == "string":
            return self.parse_print()
        if t.kind == "bytes":
            self.refuse(t, "a byte-string literal")
        if t.kind == "ident" and self.is_op(":", 1) and self.peek(2).kind == "keyword" and self.peek(2).text in ("for", "while", "do"):
            label = self.next().text
            self.next()
            if self.is_kw("for"):
                return self.parse_for(label)
            if self.is_kw("while"):
                return self.parse_while(label)
            self.refuse(self.peek(), "`do`")
        if t.kind == "ident" and t.text == "_" and self.is_op("=", 1):
            self.next()
            self.next()
            e = self.parse_expr()
            self.expect_op(";")
            return Discard(self.pos(t), e)
        n = self.union_len()
        if n and self.peek(n).kind == "ident":
            self.refuse(t, "an error union outside a function result (SPEC-04 LS-95): no clause gives the "
                           "diagnostic")
        if t.kind == "fixed_type" or self.is_decl_ahead() or \
                (t.kind == "ident" and t.text in SCALAR_TYPE_NAMES and self.is_op("[", 1)):
            return self.parse_var_decl()
        s = self.parse_simple()
        self.expect_op(";")
        return s

    def parse_defer(self) -> Defer:
        """`defer` or `errdefer`, then a block or a simple statement and `;` (SPEC-04 LS-185,
        LS-189, LS-301 `defer_stmt`)."""
        kw = self.next()
        t = self.peek()
        if t.kind == "op" and t.text == "{":
            return Defer(self.pos(kw), kw.text, self.parse_block())
        if t.kind == "ident" and t.text == "_" and self.is_op("=", 1):
            self.next()
            self.next()
            body = Discard(self.pos(t), self.parse_expr())
        elif (t.kind == "ident" and not self.is_decl_ahead() and not self.union_len()) or self.is_kw("try"):
            body = self.parse_simple()
        else:
            self.refuse(t, "a deferred statement that is neither a block nor a simple statement (SPEC-04 LS-187, "
                           "LS-301): no clause gives the diagnostic")
        self.expect_op(";")
        return Defer(self.pos(kw), kw.text, body)

    def parse_var_decl(self) -> VarDecl:
        start = self.peek()
        ty = self.parse_type()
        name = self.decl_name()
        init = None
        init_pos = None
        if self.is_op("="):
            init_pos = self.pos(self.next())
            init = self.parse_expr()
        self.expect_op(";")
        return VarDecl(self.pos(start), ty, name.text, self.pos(name), init, False, init_pos=init_pos)

    def parse_view_decl(self) -> VarDecl:
        """`in T[_, ...] name = view;` or `inout ...`: a view variable (SPEC-04 LS-106, LS-69)."""
        start = self.next()
        ty = self.parse_type()
        name = self.decl_name()
        init_pos = self.pos(self.expect_op("="))
        init = self.parse_expr()
        self.expect_op(";")
        d = VarDecl(self.pos(start), ty, name.text, self.pos(name), init, False, init_pos=init_pos)
        d.mode = start.text
        return d

    def parse_reduce(self) -> Reduce:
        """`reduce target = op(...);` (SPEC-02 R-1, SPEC-04 LS-249)."""
        kw = self.next()
        target = self.expect_ident()
        self.expect_op("=")
        call = self.parse_expr()
        self.expect_op(";")
        return Reduce(self.pos(kw), target.text, self.pos(target), call)

    def _place_ahead(self) -> int:
        """Length of a place `IDENT { . IDENT | [ index ] }` at the cursor, or 0."""
        if self.peek().kind != "ident":
            return 0
        k = 1
        while True:
            if self.is_op(".", k) and self.peek(k + 1).kind == "ident":
                k += 2
            elif self.is_op("[", k):
                end = self._bracket_end(k)
                if end == 0:
                    return k
                k = end
            else:
                return k

    def parse_simple(self) -> Stmt:
        """A simple statement without its `;` (also used in `for` headers)."""
        t = self.peek()
        k = self._place_ahead()
        after = self.peek(k) if k else None
        if after is not None and after.kind == "op" and (after.text in ASSIGN_OPS or after.text in ("++", "--")):
            target = self.parse_postfix(allow_incdec=True)
            op = self.next()
            if op.text in ("++", "--"):
                return IncDec(self.pos(op), target, op.text)
            value = self.parse_expr()
            return Assign(self.pos(op), target, op.text, value)
        e = self.parse_expr()
        nt = self.peek()
        if nt.kind == "op" and nt.text in ASSIGN_OPS:
            error("C2058", e.start, "the left side of an assignment must be a variable or field")
        if not isinstance(e, Call) and not (isinstance(e, Try) and isinstance(e.operand, Call)):
            error("C4012", e.start, "only calls, assignments, increments, print statements and declarations may stand as statements")
        return ExprStmt(e.start, e)

    def parse_condition(self, keyword: Token, paren_code) -> Expr:
        if not self.is_op("("):
            error(paren_code, self.pos(self.peek()), "parentheses are required around the condition of `%s`" % keyword.text)
        self.next()
        cond = self.parse_expr()
        if self.is_op("="):
            error("C1041", cond.start, "assignment is a statement, not an expression: write `==` to compare")
        self.expect_op(")", paren_code)
        return cond

    def parse_body(self, code) -> Block:
        t = self.peek()
        if t.kind == "op" and t.text == ";":
            error("C1040", self.pos(t), "there is no empty statement")
        if not (t.kind == "op" and t.text == "{"):
            error(code, self.pos(t), "braces are required around the body")
        return self.parse_block()

    def parse_if(self) -> If:
        kw = self.next()
        cond = self.parse_condition(kw, "C1043")
        then = self.parse_body("C1043")
        else_ = None
        if self.is_kw("else"):
            self.next()
            if self.is_kw("if"):
                else_ = self.parse_if()
            else:
                else_ = self.parse_body("C1043")
        return If(self.pos(kw), cond, then, else_)

    def parse_while(self, label) -> While:
        kw = self.next()
        cond = self.parse_condition(kw, "C1043")
        body = self.parse_body("C1043")
        return While(self.pos(kw), cond, body, label)

    def parse_for(self, label) -> Stmt:
        kw = self.next()
        if self.is_op("("):
            self.next()
            if self.is_decl_ahead():
                st = self.peek()
                ty = self.parse_type()
                name = self.decl_name()
                init_pos = self.pos(self.expect_op("="))
                init = VarDecl(self.pos(st), ty, name.text, self.pos(name), self.parse_expr(), False,
                               init_pos=init_pos)
            else:
                init = self.parse_simple()
            self.expect_op(";")
            cond = self.parse_expr()
            if self.is_op("="):
                error("C1041", cond.start, "assignment is a statement, not an expression: write `==` to compare")
            self.expect_op(";")
            update = self.parse_simple()
            self.expect_op(")")
            body = self.parse_body("C1043")
            return ForC(self.pos(kw), init, cond, update, body, label)
        var = self.decl_name()
        if self.is_op(","):
            self.refuse(self.peek(), "`for i, x in` over a view")
        if not self.is_kw("in"):
            error("C1050", self.pos(self.peek()), "expected `in`")
        self.next()
        lo = self.parse_expr()
        if not (self.is_op("..") or self.is_op("..=")):
            self.refuse(self.peek(), "`for x in` over a view")
        inclusive = self.next().text == "..="
        hi = self.parse_expr()
        step = None
        if self.is_kw("by"):
            self.next()
            step = self.parse_expr()
        body = self.parse_body("C1043")
        return ForRange(self.pos(kw), var.text, self.pos(var), lo, hi, inclusive, step, body, label)

    def parse_switch(self) -> Switch:
        kw = self.next()
        self.expect_op("(")
        scrutinee = self.parse_expr()
        self.expect_op(")")
        self.expect_op("{")
        cases, default = [], None
        while not self.is_op("}"):
            t = self.peek()
            if self.is_kw("case"):
                if default is not None:
                    error("C4025", self.pos(t), "`default` must be the last clause of a switch")
                self.next()
                items = []
                while True:
                    lo = self.parse_or()
                    hi = None
                    if self.is_op(".."):
                        error("C4022", lo.start, "case ranges are inclusive: write `..=`")
                    if self.is_op("..="):
                        self.next()
                        hi = self.parse_or()
                    items.append((lo, hi))
                    if not self.is_op(","):
                        break
                    self.next()
                self.expect_op(":")
                body = self.parse_case_body(t)
                cases.append(Case(items, body, self.pos(t)))
            elif self.is_kw("default"):
                if default is not None:
                    error("C4025", self.pos(t), "a switch has at most one `default`")
                self.next()
                self.expect_op(":")
                default = Case([], self.parse_case_body(t), self.pos(t))
            else:
                error("C1050", self.pos(t), "expected `case`, `default` or `}`")
        self.next()
        return Switch(self.pos(kw), scrutinee, cases, default)

    def parse_case_body(self, case_tok) -> list:
        body = []
        while not (self.is_kw("case") or self.is_kw("default") or self.is_op("}")):
            if self.peek().kind == "eof":
                error("C1050", self.pos(self.peek()), "unterminated switch: expected `}`, found end of input")
            body.append(self.parse_statement())
        if not body:
            error("C4023", self.pos(case_tok), "a case clause has an empty body: merge its items into the next clause")
        return body

    def parse_assert(self) -> Assert:
        kw = self.next()
        self.expect_op("(")
        cond = self.parse_expr()
        message = None
        if self.is_op(","):
            self.next()
            t = self.peek()
            if t.kind != "string":
                error("C1050", self.pos(t), "expected a string literal")
            message = self.parse_string_parts()
        self.expect_op(")")
        self.expect_op(";")
        return Assert(self.pos(kw), cond, message)

    def parse_print(self) -> Print:
        t = self.peek()
        parts = self.parse_string_parts()
        self.expect_op(";")
        return Print(self.pos(t), parts)

    def parse_string_parts(self) -> list:
        """One or more adjacent string literals, with holes parsed."""
        parts = []
        while self.peek().kind == "string":
            tok = self.next()
            for p in string_parts(tok, allow_holes=True):
                if isinstance(p, Hole):
                    parts.append(self.parse_hole(p))
                elif p:
                    if parts and isinstance(parts[-1], bytes):
                        parts[-1] = parts[-1] + p
                    else:
                        parts.append(p)
        return parts

    def parse_hole(self, h: Hole) -> HoleExpr:
        hpos = Position(self.path, h.line, h.col)
        stream = _LazyStream(h.text, self.path, h.line, h.col, BOX12_KEYWORDS if self.box12 else KEYWORDS)
        sub = Parser(stream, self.path, box12=self.box12, memory=self.memory)
        if stream.peek().kind in ("eof", "error"):
            error("C1035", hpos, "empty interpolation hole")
        e = sub.parse_or()
        nxt = stream.peek()
        rest = h.text[nxt.offset:] if nxt.kind != "eof" else ""
        expr_text = h.text[:nxt.offset] if nxt.kind != "eof" else h.text
        if any(t.kind in ("string", "bytes") for t in stream.toks[:stream.i]):
            error("C1035", hpos, "a hole expression must not contain string literals")
        name = conv = lanes = spec = None
        if rest.startswith("="):
            name = expr_text
            rest = rest[1:]
        if rest.startswith("!"):
            m = re.match(r"!(bits|raw|ratio|lanes\((I8|I16|I32|I64|U8|U16|U32|U64)\))", rest)
            if not m:
                error("C1035", hpos, "unknown conversion in hole: %r" % rest)
            conv = m.group(1)
            if conv.startswith("lanes"):
                conv, lanes = "lanes", m.group(2)
            rest = rest[m.end():]
        if rest.startswith(":"):
            spec = rest[1:]
            rest = ""
        if rest:
            error("C1035", hpos, "malformed hole: unexpected %r (a conditional expression must be parenthesized)" % rest)
        return HoleExpr(e, name, conv, lanes, spec, hpos)

    # expressions ----------------------------------------------------------------
    def parse_expr(self) -> Expr:
        self._enter(self.peek())
        e = self.parse_cond()
        if self.is_kw("catch"):
            if not self.box12:
                self.refuse(self.peek(), "`catch`")
            e = self.parse_catch(e)
        self._leave()
        return e

    def parse_catch(self, left) -> Catch:
        """`left catch value` or `left catch (name) { ... }` (SPEC-04 LS-130, LS-131, LS-302)."""
        kw = self.next()
        if self.is_op("(") and self.peek(1).kind == "ident" and self.is_op(")", 2) and self.is_op("{", 3):
            self.next()
            name = self.decl_name()
            self.next()
            block = self.parse_block()
            return Catch(self.pos(kw), left.start, left, None, name.text, self.pos(name), block)
        return Catch(self.pos(kw), left.start, left, self.parse_cond())

    def parse_cond(self) -> Expr:
        c = self.parse_or()
        if self.is_op("?"):
            q = self.next()
            a = self.parse_expr()
            self.expect_op(":")
            b = self.parse_cond()
            return Cond(self.pos(q), c.start, c, a, b)
        return c

    def parse_or(self) -> Expr:
        return self._logical("||", self.parse_and, "&&")

    def parse_and(self) -> Expr:
        return self._logical("&&", self.parse_cmp, "||")

    def _logical(self, op, sub, other):
        first = left = sub()
        n = 1
        while self.is_op(op):
            t = self.next()
            right = sub()
            left = Logical(self.pos(t), first.start, op, left, right)
            n += 1
        if n > 1:
            for e in _chain_operands(left, Logical, op):
                if isinstance(e, Logical) and e.op == other and not e.paren:
                    error("C2101", first.start, "`&&` and `||` in one expression need parentheses")
        return left

    def parse_cmp(self) -> Expr:
        left = self.parse_bor()
        t = self.peek()
        if t.kind == "op" and t.text in CMP_OPS:
            self.next()
            right = self.parse_bor()
            e = Compare(self.pos(t), left.start, t.text, left, right)
            t2 = self.peek()
            if t2.kind == "op" and t2.text in CMP_OPS:
                error("C2102", left.start, "comparisons do not chain: write `a < b && b < c`")
            return e
        return left

    def parse_bor(self):
        return self._bitwise("|", self.parse_bxor)

    def parse_bxor(self):
        return self._bitwise("^", self.parse_band)

    def parse_band(self):
        return self._bitwise("&", self.parse_shift)

    def _bitwise(self, op, sub):
        first = left = sub()
        while self.is_op(op):
            t = self.next()
            right = sub()
            for x in (left if left is first else None, right):
                if x is None:
                    continue
                self._check_bitwise_operand(x, op, first)
            left = Binary(self.pos(t), first.start, op, left, right)
        return left

    def _check_bitwise_operand(self, x, op, first):
        if isinstance(x, Binary) and not x.paren:
            if x.op in ARITH_OPS:
                error("C2101", first.start, "a bitwise operator with an arithmetic operand needs parentheses")
            if x.op in BITWISE_OPS and x.op != op:
                error("C2101", first.start, "two different bitwise operators need parentheses")

    def parse_shift(self):
        first = left = self.parse_add()
        while self.peek().kind == "op" and self.peek().text in SHIFT_OPS:
            t = self.next()
            right = self.parse_add()
            for x in ((left,) if left is first else ()) + (right,):
                if isinstance(x, Binary) and not x.paren and x.op in ARITH_OPS:
                    error("C2101", first.start, "a shift with an arithmetic operand needs parentheses")
            left = Binary(self.pos(t), first.start, t.text, left, right)
        return left

    def parse_add(self):
        return self._arith(("+", "-", "+%", "-%", "+|", "-|"), self.parse_mul)

    def parse_mul(self):
        return self._arith(("*", "/", "%", "*%", "*|"), self.parse_conv)

    def _arith(self, ops, sub):
        first = left = sub()
        while self.peek().kind == "op" and self.peek().text in ops:
            t = self.next()
            right = sub()
            left = Binary(self.pos(t), first.start, t.text, left, right)
        return left

    def parse_conv(self):
        e = self.parse_unary()
        while (self.peek().kind == "op" and self.peek().text in ("as%", "as|", "as?")) or self.is_kw("as"):
            t = self.next()
            if isinstance(e, Unary) and not e.paren:
                error("C2101", e.start, "a unary operator followed by a conversion needs parentheses")
            if t.text == "as|" or (t.text == "as?" and not self.box12):
                self.refuse(t, "`%s` (Proposed)" % t.text)
            target = self.parse_type()
            mode = None
            if self.peek().kind == "ident" and self.peek().text == "round":
                self.next()
                mode = self.expect_ident().text
            e = Convert(self.pos(t), e.start, t.text, e, target, mode)
        return e

    def parse_unary(self):
        t = self.peek()
        if t.kind == "op" and t.text in ("-", "-%", "~", "!"):
            self.next()
            nt = self.peek()
            # A prefix `-` in operand position followed by a literal forms one negative literal,
            # whatever whitespace or comments separate them (SPEC-01 IM-25, SPEC-04 LS-28;
            # decision 2026-10-03, slice 2 patch D-21 option 1).
            if t.text == "-" and nt.kind in ("int", "char", "frac"):
                self.next()
                lit = Lit(self.pos(t), self.pos(t), -nt.value, nt.kind)
                return self.parse_postfix_after(lit)
            self._enter(t)
            operand = self.parse_unary()
            self._leave()
            return Unary(self.pos(t), self.pos(t), t.text, operand)
        if t.kind == "keyword" and t.text == "try":
            if not self.box12:
                self.refuse(t, "`try`")
            self.next()
            self._enter(t)
            operand = self.parse_unary()
            self._leave()
            return Try(self.pos(t), self.pos(t), operand)
        if t.kind == "op" and t.text in ("++", "--"):
            error("C1042", self.pos(t), "`%s` is a statement, not an expression" % t.text)
        return self.parse_postfix()

    def parse_postfix(self, allow_incdec=False):
        return self.parse_postfix_after(self.parse_primary(), allow_incdec)

    def parse_postfix_after(self, e, allow_incdec=False):
        while True:
            t = self.peek()
            if t.kind == "op" and t.text == "(":
                if not isinstance(e, Name) or e.paren:
                    self.refuse(t, "a call of a computed callee")
                e = Call(e.pos, e.start, e.name, self.parse_args())
            elif t.kind == "op" and t.text == "[" and not self.box09:
                # Rank-1 indexing (SPEC-04 LS-64), the surface before box 09.
                self.next()
                index = self.parse_expr()
                if self.is_op(".."):
                    self.refuse(self.peek(), "an array slice")
                if self.is_op("..="):
                    self.refuse(self.peek(), "an array slice")
                if self.is_kw("by"):
                    self.refuse(self.peek(), "a strided slice")
                if self.is_op(","):
                    self.refuse(self.peek(), "an index of an array of rank above 1")
                self.expect_op("]")
                e = Index(self.pos(t), e.start, e, index)
            elif t.kind == "op" and t.text == "[":
                # Indices and slices, one item per dimension (SPEC-04 LS-64, LS-65, LS-161).
                self.next()
                items = [self.parse_index_item()]
                while self.is_op(","):
                    self.next()
                    items.append(self.parse_index_item())
                self.expect_op("]")
                e = Index(self.pos(t), e.start, e, items[0], items)
            elif t.kind == "op" and t.text == "." and self.peek(1).kind == "ident":
                self.next()
                f = self.next()
                if self.is_op("("):
                    # `alias.f(...)`: a call qualified by an import alias (SPEC-04 LS-225).
                    if not isinstance(e, Name) or e.paren:
                        self.refuse(self.peek(), "a member call")
                    e = Call(self.pos(f), e.start, f.text, self.parse_args(), e.name)
                    continue
                e = Field(self.pos(f), e.start, e, f.text)
            else:
                break
        t = self.peek()
        if t.kind == "op" and t.text in ("++", "--") and not allow_incdec:
            error("C1042", self.pos(t), "`%s` is a statement, not an expression" % t.text)
        return e

    def parse_index_item(self):
        """An index expression, or a slice `[lo] .. [hi] [by k]` or `[lo] ..= hi [by k]`
        (SPEC-04 18.6 `index`, LS-161)."""
        lo = None
        if not (self.is_op("..") or self.is_op("..=")):
            lo = self.parse_expr()
            if not (self.is_op("..") or self.is_op("..=")):
                return lo
        op = self.next()
        inclusive = op.text == "..="
        hi = None
        if inclusive or not (self.is_op("]") or self.is_op(",") or self.is_kw("by")):
            hi = self.parse_expr()
        step = None
        if self.is_kw("by"):
            self.next()
            step = self.parse_expr()
        return Slice(lo, hi, inclusive, step, self.pos(op))

    def parse_args(self) -> list:
        """`( [arg {, arg}] )` with the cursor at `(`."""
        self.next()
        args = []
        while not self.is_op(")"):
            a = self.peek()
            if a.kind == "ident" and self.is_op("=", 1):
                self.next()
                self.next()
                args.append(Arg(a.text, self.parse_expr(), self.pos(a)))
            else:
                args.append(Arg(None, self.parse_expr(), self.pos(a)))
            if not self.is_op(")"):
                self.expect_op(",")
        self.next()
        return args

    def parse_primary(self):
        t = self.peek()
        p = self.pos(t)
        if t.kind in ("int", "char", "frac"):
            self.next()
            return Lit(p, p, t.value, t.kind)
        if t.kind == "string":
            # A string literal outside a print statement or assert message has no holes
            # (SPEC-04 LS-44, C1033); adjacent literals are one (LS-45).
            data = bytearray()
            while self.peek().kind == "string":
                data += b"".join(string_parts(self.next(), allow_holes=False))
            return StrLit(p, p, [bytes(data)])
        if t.kind == "keyword" and t.text in ("true", "false"):
            self.next()
            return BoolLit(p, p, t.text == "true")
        if t.kind == "ident":
            self.next()
            if t.text in SCALAR_TYPE_NAMES:
                if self.is_op(".") and self.peek(1).kind == "ident":
                    self.next()
                    prop = self.next()
                    return TypeProp(p, p, t.text, prop.text)
                return TypeArg(p, p, t.text)
            return Name(p, p, t.text)
        if t.kind == "fixed_type":
            self.refuse(t, "fixed point")
        if t.kind == "op" and t.text == "(":
            self.next()
            e = self.parse_expr()
            if self.is_op(","):
                self.refuse(self.peek(), "a tuple")
            self.expect_op(")")
            e.paren = True
            if isinstance(e, Lit) and not hasattr(e, "lit_start"):
                e.lit_start = e.start   # C2003 points at the literal, not at `(` (ref/OPEN.md REF-OQ-25)
            e.start = p
            return e
        if t.kind == "op" and t.text == "[":
            if not self.box09:
                self.refuse(t, "an array literal")
            # `[a, b, c]`, nested for rank 2 and above; a trailing comma is permitted (SPEC-04 LS-67).
            self.next()
            self._enter(t)
            items = []
            while not self.is_op("]"):
                items.append(self.parse_expr())
                if not self.is_op("]"):
                    self.expect_op(",")
            self.next()
            self._leave()
            return ArrayLit(p, p, items)
        if t.kind == "op" and t.text == ".":
            if not self.box12:
                self.refuse(t, "a context-typed enum, error or Round literal")
            # `.name`: an error value typed by its context (SPEC-04 LS-60, LS-92).
            self.next()
            return ErrorLit(p, p, self.expect_ident().text)
        if t.kind == "bytes":
            self.refuse(t, "a byte-string literal")
        error("C1050", p, "expected an expression, found %s" % self.describe(t))


def _chain_operands(e, cls, op):
    """Operands of a left-associated chain of `op`."""
    out = []
    while isinstance(e, cls) and e.op == op and not e.paren:
        out.append(e.right)
        e = e.left
    out.append(e)
    return out


def parse_module(text: str, path: str, box09: bool = True, box12: bool = True, memory: bool = True) -> Module:
    memory = memory and box12
    keywords = BOX12_KEYWORDS if box12 else KEYWORDS
    m = Parser(_ListStream(tokenize(text, path, keywords=keywords)), path, box09, box12, memory).parse_module(text)
    m.memory = memory
    m.box09 = box09
    m.box12 = box12
    return m

