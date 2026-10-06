"""The front-end outcome `cint_ref` gives for a source file, in the text of compiler/tests/front.ci.

Slice 2 task 2.10a. `outcome(data, path)` runs `cint_ref`'s source-text check, lexer and
parser (ref/cint_ref/lexer.py, parser.py) on the surface with box 09 (`parse_module(text,
path, box09=True)`: arrays of rank 2 to 4, slices, array literals, local view variables,
kernels and `reduce` statements) and returns one of

    ("tree", text)               the parse succeeded; text is the canonical tree text
    ("error", code, line, col)   a compile diagnostic (C1002 to C9004)
    ("refused", line, col, why)  a construct outside the surface cint_ref implements

The tree text is the one front.ci writes for cintc's tree (see its header): one
s-expression per node, `-` for an absent child, `[...]` around a list, `@line:col`
positions, `^line:col` expression starts, ` p` for a parenthesized expression, and string
bytes as `x` and hexadecimal. Where cint_ref's tree omits a position that cintc keeps (a
static_assert message, the `..=` of a case range, the `!` of an error union, the name of a
`.name` error value), it is read from cint_ref's own tokens. The surface is that of box 09
and box 12: arrays of rank 2 to 4, slices, views, `copy`, the reductions, `reduce` and
kernels, and error sets, error unions, `try`, `catch`, `as?`, `.name`, `defer` and
`errdefer`, and arenas, pools and handles (`Arena(T)`, `Pool(T)`, `Handle(T)` and a
module-level `Arena(T, capacity) name;`) parse. The `_` extent of an `over` item, which
cint_ref parses as the name `_`, is `(_)`, as in a shape. This module shares no code with
cintc: it reads cint_ref's tree only.
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "ref"))

from cint_ref import lexer, parser  # noqa: E402
from cint_ref import tree as T  # noqa: E402
from cint_ref.faults import CompileError, Refused  # noqa: E402

# scan.ci identifier classes of the integer types (Token.aux), for `!lanes(T)`.
IDC = {"I8": 1, "I16": 2, "I32": 3, "I64": 4, "U8": 9, "U16": 10, "U32": 11, "U64": 12}


def at(p) -> str:
    return "@%d:%d" % (p.line, p.column)


def start(p) -> str:
    return "^%d:%d" % (p.line, p.column)


def hexs(b: bytes) -> str:
    return "x" + b.hex()


class Writer:
    def __init__(self, text: str, path: str):
        self.text, self.path = text, path
        self.lines = text.split("\n")
        self.toks = [t for t in lexer.tokenize(text, path) if t.kind != "eof"]
        self.index = None

    def char_at(self, p) -> str:
        line = self.lines[p.line - 1]
        return line[p.column - 1] if p.column - 1 < len(line) else ""

    def token_after(self, p, text):
        """The first token spelled `text` at or after position p."""
        for t in self.toks:
            if (t.line, t.col) >= (p.line, p.column) and t.text == text:
                return t
        raise ValueError("no %r after %s" % (text, p))

    def token_following(self, p):
        """The token after the one at position p; inside a hole, whose text the module's tokens
        do not split, the text from p is lexed for it."""
        if self.index is None:
            self.index = {(t.line, t.col): k for k, t in enumerate(self.toks)}
        k = self.index.get((p.line, p.column))
        if k is not None:
            return self.toks[k + 1]
        offset = sum(len(x) + 1 for x in self.lines[:p.line - 1]) + p.column - 1
        lx = lexer.Lexer(self.text[offset:], self.path, p.line, p.column)
        lx.next_token()
        return lx.next_token()

    def last_token_before(self, p, text):
        found = None
        for t in self.toks:
            if (t.line, t.col) >= (p.line, p.column):
                break
            if t.text == text:
                found = t
        return found

    def string_after_comma(self, p):
        """The message token of `static_assert(cond, "...")` whose keyword is at p."""
        depth = 0
        seen = False
        for t in self.toks:
            if (t.line, t.col) < (p.line, p.column):
                continue
            if t.kind == "op" and t.text in ("(", "["):
                depth += 1
            elif t.kind == "op" and t.text in (")", "]"):
                depth -= 1
            elif t.kind == "op" and t.text == "," and depth == 1:
                seen = True
            elif seen and t.kind == "string":
                return t
        raise ValueError("no static_assert message at %s" % p)

    # -------------------------------------------------------------- expressions

    def expr_pos(self, e) -> str:
        return at(e.pos) + start(e.start) + (" p" if e.paren else "")

    def expr(self, e) -> str:
        if isinstance(e, T.Lit):
            if self.char_at(e.pos) == "-":
                return "(neg %s%s)" % (e.kind, self.expr_pos(e))
            return "(lit %s%s)" % (e.kind, self.expr_pos(e))
        if isinstance(e, T.BoolLit):
            return "(bool %s%s)" % ("true" if e.value else "false", self.expr_pos(e))
        if isinstance(e, T.StrLit):
            return "(str %s%s)" % (hexs(b"".join(e.parts)), self.expr_pos(e))
        if isinstance(e, T.Name):
            return "(name %s%s)" % (e.name, self.expr_pos(e))
        if isinstance(e, T.TypeArg):
            return "(typearg %s%s)" % (e.name, self.expr_pos(e))
        if isinstance(e, T.TypeProp):
            return "(typeprop %s.%s%s)" % (e.type_name, e.prop, self.expr_pos(e))
        if isinstance(e, T.ErrorLit):
            # cintc's N_DOTNAME: the name token, starting at the `.`.
            t = self.token_following(e.pos)
            return "(dotname %s@%d:%d%s%s)" % (e.name, t.line, t.col, start(e.start), " p" if e.paren else "")
        if isinstance(e, T.Unary):
            return "(unary %s%s %s)" % (e.op, self.expr_pos(e), self.expr(e.operand))
        if isinstance(e, T.Try):
            return "(unary try%s %s)" % (self.expr_pos(e), self.expr(e.operand))
        if isinstance(e, T.Catch):
            name = "" if e.bind is None else " name:%s" % e.bind
            handler = self.expr(e.value) if e.block is None else self.block(e.block)
            return "(catch%s%s %s %s)" % (self.expr_pos(e), name, self.expr(e.left), handler)
        if isinstance(e, (T.Binary, T.Compare, T.Logical)):
            kind = {T.Binary: "bin", T.Compare: "cmp", T.Logical: "logic"}[type(e)]
            return "(%s %s%s %s %s)" % (kind, e.op, self.expr_pos(e), self.expr(e.left), self.expr(e.right))
        if isinstance(e, T.Cond):
            return "(cond %s %s %s %s)" % (self.expr_pos(e), self.expr(e.cond), self.expr(e.a), self.expr(e.b))
        if isinstance(e, T.Convert):
            rnd = " round:%s" % e.round_mode if e.round_mode else ""
            return "(conv %s%s%s %s %s)" % (e.op, self.expr_pos(e), rnd, self.expr(e.operand), self.type(e.target))
        if isinstance(e, T.Call):
            qual = " qual:%s" % e.qual if e.qual else ""
            args = " ".join(self.arg(a) for a in e.args)
            return "(call %s%s%s [%s])" % (e.callee, qual, self.expr_pos(e), args)
        if isinstance(e, T.Index):
            items = " ".join(self.index_item(x) for x in e.items)
            return "(index%s %s [%s])" % (self.expr_pos(e), self.expr(e.obj), items)
        if isinstance(e, T.ArrayLit):
            return "(array%s [%s])" % (self.expr_pos(e), " ".join(self.expr(x) for x in e.items))
        if isinstance(e, T.Field):
            return "(field %s%s %s)" % (e.field, self.expr_pos(e), self.expr(e.obj))
        raise ValueError("expression %r" % type(e).__name__)

    def index_item(self, x) -> str:
        """An index, or a slice `(slice OP@pos low high step)` (box 09, SPEC-04 LS-161)."""
        if isinstance(x, T.Slice):
            op = "..=" if x.inclusive else ".."
            return "(slice %s%s %s %s %s)" % (op, at(x.pos), self.opt(x.lo), self.opt(x.hi), self.opt(x.step))
        return self.expr(x)

    def arg(self, a) -> str:
        if a.name is None:
            return self.expr(a.value)
        return "(arg %s%s %s)" % (a.name, at(a.pos), self.expr(a.value))

    def opt(self, e) -> str:
        return "-" if e is None else self.expr(e)

    def type(self, t) -> str:
        """A shape of one extent is that extent (`_` for `[]` and `[_]`); a shape of rank 2
        to 4 (box 09) is `(shape [...])`, with `(_)` for each `_`."""
        mod = " mod:%s." % t.module if t.module else ""
        if t.shape is None:
            shape = "-"
        elif t.dims is not None and len(t.dims) > 1:
            shape = "(shape [%s])" % " ".join("(_)" if d == "_" else self.expr(d) for d in t.dims)
        elif t.shape == "_":
            shape = "_"
        else:
            shape = self.expr(t.shape)
        # Box 12: the inner type of `Arena(T)`, `Pool(T)` or `Handle(T)`, and a capacity.
        inner = "-" if getattr(t, "inner", None) is None else self.type(t.inner)
        cap = getattr(t, "capacity", None)
        cap = "" if cap is None else " " + self.expr(cap)
        return "(type %s%s%s %s %s%s)" % (t.name, at(t.pos), mod, shape, inner, cap)

    def result(self, r) -> str:
        """A function's result: a type, `E!T` or `E!void` (cintc's N_ERRUNION at the `!`), or void."""
        if r is None:
            return "-"
        if isinstance(r, T.UnionType):
            bang = self.token_after(r.set.pos, "!")
            value = "-" if r.value is None else self.type(r.value)
            return "(errunion @%d:%d %s %s)" % (bang.line, bang.col, self.type(r.set), value)
        return self.type(r)

    # -------------------------------------------------------------- statements

    def parts(self, parts) -> str:
        out = []
        for p in parts:
            if isinstance(p, bytes):
                out.append("(text %s)" % hexs(p))
                continue
            s = "(hole @%d:%d" % (p.pos.line, p.pos.column)
            if p.name is not None:
                s += " name:%s" % hexs(p.name.encode("utf-8"))
            if p.conv == "lanes":
                s += " conv:lanes:%d" % IDC[p.lanes]
            elif p.conv:
                s += " conv:%s" % p.conv
            if p.spec is not None:
                s += " spec:%s" % hexs(p.spec.encode("utf-8"))
            out.append(s + " " + self.expr(p.expr) + ")")
        return "[%s]" % " ".join(out)

    def var(self, d) -> str:
        flags = (" const" if d.is_const else "") + (" export" if d.exported else "")
        if d.mode is not None:
            flags += " view:%s" % d.mode     # `in` or `inout`: a local view variable (box 09)
        return "(var %s%s %s%s %s %s [])" % (d.name, at(d.name_pos), at(d.pos), flags, self.type(d.type),
                                             self.opt(d.init))

    def block(self, b) -> str:
        return "(block %s [%s])" % (at(b.pos), " ".join(self.stmt(s) for s in b.stmts))

    def label(self, s) -> str:
        return " label:%s" % s.label if s.label else ""

    def stmt(self, s) -> str:
        if isinstance(s, T.Block):
            return self.block(s)
        if isinstance(s, T.VarDecl):
            return self.var(s)
        if isinstance(s, T.Assign):
            return "(assign %s%s %s %s)" % (s.op, at(s.pos), self.expr(s.target), self.expr(s.value))
        if isinstance(s, T.IncDec):
            return "(incdec %s%s %s)" % (s.op, at(s.pos), self.expr(s.target))
        if isinstance(s, T.ExprStmt):
            return "(exprstmt %s %s)" % (at(s.pos), self.expr(s.expr))
        if isinstance(s, T.Discard):
            return "(discard %s %s)" % (at(s.pos), self.expr(s.expr))
        if isinstance(s, T.Print):
            return "(print %s %s)" % (at(s.pos), self.parts(s.parts))
        if isinstance(s, T.If):
            arms = []
            node = s
            while True:
                arms.append("(arm %s %s %s)" % (at(node.pos), self.expr(node.cond), self.block(node.then)))
                if isinstance(node.else_, T.If):
                    node = node.else_
                    continue
                tail = "-" if node.else_ is None else self.block(node.else_)
                break
            return "(if [%s] %s)" % (" ".join(arms), tail)
        if isinstance(s, T.While):
            return "(while %s%s %s %s)" % (at(s.pos), self.label(s), self.expr(s.cond), self.block(s.body))
        if isinstance(s, T.ForC):
            return "(for %s%s %s %s %s %s)" % (at(s.pos), self.label(s), self.stmt(s.init), self.expr(s.cond),
                                               self.stmt(s.update), self.block(s.body))
        if isinstance(s, T.ForRange):
            op = "..=" if s.inclusive else ".."
            return "(forr %s%s %s (bind %s%s -) - %s %s %s %s)" % (
                at(s.pos), self.label(s), op, s.var, at(s.var_pos), self.expr(s.lo), self.expr(s.hi),
                self.opt(s.step), self.block(s.body))
        if isinstance(s, T.Switch):
            cases = [self.case(c, "case") for c in s.cases]
            if s.default is not None:
                cases.append(self.case(s.default, "default"))
            return "(switch %s %s [%s])" % (at(s.pos), self.expr(s.scrutinee), " ".join(cases))
        if isinstance(s, (T.Break, T.Continue)):
            kind = "break" if isinstance(s, T.Break) else "continue"
            return "(%s %s%s)" % (kind, at(s.pos), self.label(s))
        if isinstance(s, T.Return):
            return "(return %s %s)" % (at(s.pos), self.opt(s.value))
        if isinstance(s, T.Fallthrough):
            return "(fallthrough %s)" % at(s.pos)
        if isinstance(s, T.Assert):
            msg = "-" if s.message is None else "(message %s)" % self.parts(s.message)
            return "(assert %s %s %s)" % (at(s.pos), self.expr(s.cond), msg)
        if isinstance(s, T.StaticAssert):
            return self.static_assert(s)
        if isinstance(s, T.Reduce):
            return "(reduce %s %s%s %s)" % (at(s.pos), s.target, at(s.target_pos), self.expr(s.call))
        if isinstance(s, T.Defer):
            return "(%s %s %s)" % (s.kind, at(s.pos), self.stmt(s.body))
        raise ValueError("statement %r" % type(s).__name__)

    def static_assert(self, s) -> str:
        msg = "-"
        if s.message is not None:
            t = self.string_after_comma(s.pos)
            msg = "(str %s@%d:%d^%d:%d)" % (hexs(s.message), t.line, t.col, t.line, t.col)
        return "(static_assert %s %s %s)" % (at(s.pos), self.expr(s.cond), msg)

    def case(self, c, kind) -> str:
        items = []
        for lo, hi in c.items:
            if hi is None:
                items.append(self.expr(lo))
            else:
                t = self.last_token_before(hi.start, "..=")
                items.append("(range ..=@%d:%d %s %s)" % (t.line, t.col, self.expr(lo), self.expr(hi)))
        return "(%s %s [%s] [%s])" % (kind, at(c.pos), " ".join(items), " ".join(self.stmt(s) for s in c.body))

    # -------------------------------------------------------------- declarations

    def sizes_params(self, x) -> tuple:
        sizes = " ".join("(size %s%s)" % (n, at(p)) for n, p in x.size_params)
        params = " ".join("(param %s %s%s %s -)" % (p.mode, p.name, at(p.pos), self.type(p.type)) for p in x.params)
        return sizes, params

    def over_extent(self, e) -> str:
        """`cint_ref` parses the extent `_` of an `over` item as the name `_`; cintc gives it
        the `_` of SPEC-04 18.4 `extent`, as in a shape: `(_)`."""
        if isinstance(e, T.Name) and e.name == "_" and not e.paren:
            return "(_)"
        return self.expr(e)

    def item(self, x) -> str:
        if isinstance(x, T.FuncDecl):
            sizes, params = self.sizes_params(x)
            return "(func %s%s%s %s [%s] [%s] [] - [] %s)" % (x.name, at(x.name_pos), " export" if x.exported else "",
                                                            self.result(x.result), sizes, params,
                                                            self.block(x.body))
        if isinstance(x, T.KernelDecl):
            # Box 09: no result, then the `over` items ([] for the element form) and the `where` list.
            sizes, params = self.sizes_params(x)
            over = " ".join("(over %s%s %s)" % (v, at(p), self.over_extent(e)) for v, p, e in x.over or [])
            where = " ".join(self.expr(c) for c in x.where)
            return "(kernel %s%s%s - [%s] [%s] [%s] [%s] [] %s)" % (
                x.name, at(x.name_pos), " export" if x.exported else "", sizes, params, over, where,
                self.block(x.body))
        if isinstance(x, T.StructDecl):
            fields = " ".join("(field %s%s %s - - [])" % (f.name, at(f.pos), self.type(f.type)) for f in x.fields)
            return "(struct %s%s%s [%s] [])" % (x.name, at(x.name_pos), " export" if x.exported else "", fields)
        if isinstance(x, T.ErrorDecl):
            s = "(error %s%s" % (x.name, at(x.name_pos)) + (" export" if x.exported else "")
            if x.underlying is not None:
                s += " " + x.underlying.name
            if x.operands is not None:
                return s + " = [%s])" % " ".join(self.type(t) for t in x.operands)
            return s + " [%s])" % " ".join("(name %s%s%s)" % (n, at(p), start(p)) for n, p in x.values)
        if isinstance(x, T.TestDecl):
            s = "(test %s%s" % (hexs(x.name.encode("utf-8")), at(x.pos))
            if x.expect_fault:
                s += " fault:%s" % x.expect_fault
            if x.expect_line is not None:
                s += " at:%d" % x.expect_line
            return s + " " + self.block(x.body) + ")"
        if isinstance(x, T.Import):
            s = "(import %s path:%s%s" % (at(x.pos), ".".join(x.segments), at(x.path_pos))
            if x.selected is None:
                s += " alias:%s%s" % (x.alias, at(x.alias_pos))
                return s + " [])"
            names = " ".join("(name %s%s%s)" % (n, at(p), start(p)) for n, p in x.selected)
            return s + " [%s])" % names
        if isinstance(x, T.StaticAssert):
            return self.static_assert(x)
        return self.stmt(x)

    def module(self, m) -> str:
        items = []
        if m.profile is not None:
            items.append(((0, 0), "(profile %s)" % hexs(m.profile.encode("utf-8"))))
        for x in m.imports:
            items.append(((x.pos.line, x.pos.column), self.item(x)))
        for group in (m.functions, m.kernels, m.structs, m.errors, m.consts, m.tests, m.statements, m.static_asserts,
                      m.memories):
            for x in group:
                p = x.name_pos if isinstance(x, (T.FuncDecl, T.KernelDecl, T.StructDecl, T.ErrorDecl)) else x.pos
                items.append(((p.line, p.column), self.item(x)))
        items.sort(key=lambda kv: kv[0])
        return "(module [%s])" % " ".join(text for _, text in items)


def outcome(data: bytes, path: str):
    """`cint_ref`'s front-end outcome for the bytes of one module at module-relative `path`."""
    old = sys.getrecursionlimit()
    sys.setrecursionlimit(max(old, 200000))
    try:
        text = lexer.decode_source(data, path)
        module = parser.parse_module(text, path, box09=True, box12=True)   # cintc's surface: boxes 09 and 12
        return ("tree", Writer(text, path).module(module))
    except CompileError as e:
        d = e.diagnostic
        return ("error", int(d.code[1:]), d.position.line, d.position.column)
    except Refused as e:
        p = e.position
        return ("refused", p.line if p else 0, p.column if p else 0, e.message)
    finally:
        sys.setrecursionlimit(old)
