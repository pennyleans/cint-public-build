"""CIF-1 operation-level fixtures (SPEC-01 13.1).

One fixture per line of a JSON Lines file, keys in the order `id`, `status`,
`op`, `args`, `expect` (an optional `derivation` string, used by hand-derived
anchors, follows `expect`). Every integer is a JSON string of decimal digits
with an optional leading `-`; JSON numbers are rejected, so no value passes
through a floating-point type. `check` evaluates a record through
`arith.op`, `fixed.op` / `fixed.convert` (fixed point) or `reduce.op`
(reductions) and lists the differences.
"""
from __future__ import annotations

import json
import re
from dataclasses import dataclass

from . import arith, fixed, reduce
from .arith import ROUND_MODES, Fault
from .types import BOOL, Value, type_from_ident

KEYS = ("id", "status", "op", "args", "expect")
EXPECT_KINDS = ("value", "fault", "compile_error", "reject")
FAULT_KEYS = ("code", "exact", "limit", "index", "operands")
_INT = re.compile(r"-?[0-9]+\Z")      # SPEC-01 13.1: decimal digits with an optional leading -
_ARRAY = re.compile(r"([A-Z][A-Za-z0-9]*)\[(0|[1-9][0-9]*)\]\Z")   # `I8[3]`, SPEC-01 13.1


class Unsupported(Exception):
    """A fixture that cint_ref's operation layer does not evaluate (decoders, program-level rows, Open cases)."""


class Invalid(ValueError):
    """A record that no correct generator writes (operation form, operand type, range or count)."""


def _no_numbers(text):
    raise ValueError("CIF-1 integers are JSON strings, not JSON numbers: %s" % text)


def _check_int(s, what):
    if not isinstance(s, str) or not _INT.match(s):
        raise ValueError("%s must be a decimal integer string: %r" % (what, s))
    return int(s)


@dataclass
class Record:
    id: str
    status: str
    op: str
    args: list
    expect: dict
    derivation: str | None = None

    @property
    def parts(self):
        return parse_op(self.op)

    @property
    def name(self):
        return self.parts[0]

    @property
    def form(self):
        return self.parts[1]

    @property
    def types(self):
        return self.parts[2]

    @property
    def mode(self):
        return self.parts[3]

    @property
    def outcome(self):
        return next(iter(self.expect))


def parse_op(op_id: str):
    """Split an operation identifier (SPEC-01 9.8) into (name, form, type components, mode)."""
    parts = op_id.split(".")
    if len(parts) < 3 or not op_id.isascii() or op_id != op_id.lower():
        raise ValueError("not an operation identifier of the form name.form.type: %r" % op_id)
    mode = None
    rest = parts[2:]
    if rest[-1] in ROUND_MODES:
        mode = rest.pop()
    types = []
    for t in rest:
        try:
            types.append(type_from_ident(t))
        except ValueError:
            try:
                types.append(fixed.q_from_ident(t).name)     # q16_16 is Q16.16
            except ValueError:
                types.append(t)      # e.g. pt5; kept as written, not evaluated
    return parts[0], parts[1], types, mode


def parse_line(line: str) -> Record:
    obj = json.loads(line, parse_float=_no_numbers, parse_int=_no_numbers,
                     parse_constant=_no_numbers, object_pairs_hook=_ordered)
    keys = list(obj)
    if keys[:5] != list(KEYS) or keys[5:] not in ([], ["derivation"]):
        raise ValueError("keys must be %s (then optionally derivation), found %s" % (", ".join(KEYS), keys))
    if obj["status"] not in ("S", "P"):
        raise ValueError("status is S or P")
    if not isinstance(obj["id"], str) or not isinstance(obj["op"], str) or not isinstance(obj["args"], list):
        raise ValueError("id and op are strings and args is an array")
    parse_op(obj["op"])
    for a in obj["args"]:
        _check_typed(a)
    exp = obj["expect"]
    if not isinstance(exp, dict) or len(exp) != 1 or next(iter(exp)) not in EXPECT_KINDS:
        raise ValueError("expect holds exactly one of %s" % ", ".join(EXPECT_KINDS))
    kind, payload = next(iter(exp.items()))
    if kind == "value":
        _check_typed(payload)
    elif kind == "fault":
        if not isinstance(payload, dict) or "code" not in payload or any(k not in FAULT_KEYS for k in payload):
            raise ValueError("a fault object has code and optionally exact, limit, index, operands")
        for k in ("exact", "limit", "index"):
            if k in payload:
                _check_int(payload[k], k)
        for a in payload.get("operands", []):
            _check_typed(a)
    elif kind == "compile_error":
        if not isinstance(payload, str):
            raise ValueError("compile_error holds a code string")
    elif payload is not True:
        raise ValueError("reject holds true")
    deriv = obj.get("derivation")
    if deriv is not None and not isinstance(deriv, str):
        raise ValueError("derivation is a string")
    return Record(obj["id"], obj["status"], obj["op"], obj["args"], exp, deriv)


def _ordered(pairs):
    d = {}
    for k, v in pairs:
        if k in d:
            raise ValueError("duplicate key %r" % k)
        d[k] = v
    return d


def _check_typed(a):
    if not isinstance(a, dict) or "t" not in a or not isinstance(a["t"], str):
        raise ValueError("a typed value is an object with a type `t`: %r" % (a,))
    if "v" in a:
        if isinstance(a["v"], list):
            for x in a["v"]:
                _check_int(x, "array element")
        elif a["t"] == BOOL:
            if a["v"] not in ("true", "false"):
                raise ValueError("a Bool value is \"true\" or \"false\"")
        else:
            _check_int(a["v"], "v")
    elif "raw" in a:
        _check_int(a["raw"], "raw")
    else:
        raise ValueError("a typed value has `v` or `raw`: %r" % (a,))


def _value(a) -> Value:
    """A scalar typed value: an integer, a `Bool`, or a fixed-point raw value."""
    if "raw" in a:
        fixed.q_type(a["t"])
        return Value(a["t"], int(a["raw"]))
    if isinstance(a.get("v"), list) or "[" in a["t"]:
        raise ValueError("an array is not a scalar value: %r" % (a,))
    if a["t"] == BOOL:
        return Value(BOOL, a["v"] == "true")
    try:
        type_from_ident(a["t"].lower())
    except ValueError:
        raise Unsupported("type %s is not evaluated" % a["t"])
    return Value(a["t"], int(a["v"]))


def _array(a, elem: str) -> list:
    """The elements of `{"t": "I8[3]", "v": [...]}`; the length in the type must match."""
    m = _ARRAY.match(a.get("t", ""))
    if not m or not isinstance(a.get("v"), list):
        raise ValueError("expected an array argument: %r" % (a,))
    if m.group(1) != elem:
        raise ValueError("array element type %s differs from %s" % (m.group(1), elem))
    if int(m.group(2)) != len(a["v"]):
        raise ValueError("array type %s holds %d elements" % (a["t"], len(a["v"])))
    return [int(x) for x in a["v"]]


def evaluate(r: Record):
    name, form, types, mode = r.parts
    try:
        if name in reduce.NAMES:
            return _evaluate_reduction(r, name, form, types)
        if any(fixed.is_q_type(t) for t in types):
            return _evaluate_fixed(r, name, form, types, mode)
    except arith.OpenCase as x:
        raise Unsupported(str(x))
    if not all(_is_int_or_bool(t) for t in types):
        raise Unsupported("operation %s is outside the integer layer" % r.op)
    if form not in arith.FORMS:
        raise Unsupported("form %r is not an operator form of SPEC-01 4.1" % form)
    args = [_value(a) for a in r.args]
    try:
        if name == "as":
            if len(types) != 2:
                raise ValueError("a conversion names a source and a target type")
            return arith.op("as", form, types[0], *args, target=types[1])
        if name == "muldiv":
            return arith.op("muldiv", form, types[0], *args, mode=mode, target=types[1])
        result = arith.op(name, form, types[0], *args, mode=mode)
    except arith.OpenCase as x:
        raise Unsupported(str(x))
    except ValueError as x:
        # A form the specification does not have, an operand outside its type, a
        # wrong operand type or count: the fixture is wrong, not unsupported.
        raise Invalid("%s: %s" % (r.op, x))
    if len(types) == 2 and isinstance(result, Value) and result.type != types[1]:
        raise ValueError("%s: result type %s differs from the identifier's %s" % (r.op, result.type, types[1]))
    return result


def _evaluate_fixed(r: Record, name, form, types, mode):
    args = [_value(a) for a in r.args]
    if name == "as":
        if len(types) != 2 or len(args) != 1:
            raise ValueError("a conversion names a source and a target type and takes one operand")
        if args[0].type != types[0]:
            raise ValueError("operand type %s differs from %s" % (args[0].type, types[0]))
        return fixed.convert(form, types[0], types[1], args[0], mode=mode)
    if len(types) != 1:
        raise ValueError("%s names one fixed-point type" % r.op)
    return fixed.op(name, form, types[0], *args, mode=mode)


def _evaluate_reduction(r: Record, name, form, types):
    elem = types[0]
    target = types[1] if len(types) == 2 else None
    if len(types) not in (1, 2) or (len(types) == 2) != (name == "sum"):
        raise ValueError("%s: sum names the element and result types; other reductions only the element" % r.op)
    if len(r.args) == 2:
        v = _value(r.args[0])
        if v.type != elem:
            raise ValueError("init type %s differs from %s" % (v.type, elem))
        init = v.value
    elif len(r.args) == 1:
        init = None
    else:
        raise ValueError("a reduction takes [init,] sequence")
    return reduce.op(name, form, elem, _array(r.args[-1], elem), init=init, target=target)


def _is_int_or_bool(t):
    try:
        type_from_ident(t.lower())
        return True
    except ValueError:
        return False


def _typed_eq(expected: dict, actual: Value) -> bool:
    return expected["t"] == actual.type and _value(expected).value == actual.value


def check(r: Record) -> list:
    """Evaluate `r` and return a list of differences (empty when it agrees)."""
    kind, payload = next(iter(r.expect.items()))
    if kind in ("compile_error", "reject"):
        raise Unsupported("%s fixtures are program-level or decoder fixtures" % kind)
    result = evaluate(r)
    if kind == "value":
        if isinstance(result, Fault):
            return ["expected value %s %s, got fault %s" % (payload["t"], payload.get("v"), result.code)]
        if not _typed_eq(payload, result):
            return ["expected value %s %s, got %s" % (payload["t"], payload.get("v"), result.render())]
        return []
    if not isinstance(result, Fault):
        return ["expected fault %s, got value %s" % (payload["code"], result.render())]
    problems = []
    if payload["code"] != result.code:
        problems.append("expected code %s, got %s" % (payload["code"], result.code))
    if "exact" in payload and int(payload["exact"]) != result.exact:
        problems.append("expected exact %s, got %s" % (payload["exact"], result.exact))
    if "limit" in payload and (result.limit is None or int(payload["limit"]) != result.limit.value):
        problems.append("expected limit %s, got %s" % (payload["limit"], None if result.limit is None else result.limit.value))
    if "index" in payload and (result.index is None or int(payload["index"]) != result.index):
        problems.append("expected index %s, got %s" % (payload["index"], result.index))
    if "operands" in payload:
        exp_ops = payload["operands"]
        if len(exp_ops) != len(result.operands) or not all(_typed_eq(a, b) for a, b in zip(exp_ops, result.operands)):
            problems.append("operands differ: expected %s, got %s" % (exp_ops, [o.render() for o in result.operands]))
    return problems


def _typed_json(v: Value) -> dict:
    if v.type == BOOL:
        return {"t": BOOL, "v": "true" if v.value else "false"}
    if fixed.is_q_type(v.type):
        return {"t": v.type, "raw": str(v.value)}
    return {"t": v.type, "v": str(v.value)}


def render(id_: str, status: str, op_id: str, args: list, result, derivation: str | None = None) -> str:
    """One CIF-1 line for `result` (a Value or a Fault), keys in SPEC-01 13.1 order."""
    if isinstance(result, Fault):
        fault = {"code": result.code}
        if result.exact is not None:
            fault["exact"] = str(result.exact)
        if result.limit is not None:
            fault["limit"] = str(result.limit.value)
        if result.index is not None:
            fault["index"] = str(result.index)
        exp = {"fault": fault}
    else:
        exp = {"value": _typed_json(result)}
    obj = {"id": id_, "status": status, "op": op_id, "args": args, "expect": exp}
    if derivation is not None:
        obj["derivation"] = derivation
    return json.dumps(obj, separators=(",", ":"), ensure_ascii=True)


def render_record(r: Record) -> str:
    obj = {"id": r.id, "status": r.status, "op": r.op, "args": r.args, "expect": r.expect}
    if r.derivation is not None:
        obj["derivation"] = r.derivation
    return json.dumps(obj, separators=(",", ":"), ensure_ascii=True)


def read_file(path: str):
    """Yield (line number, Record) for every line of a CIF-1 file (LF only, UTF-8)."""
    with open(path, "rb") as f:
        data = f.read()
    if b"\r" in data:
        raise ValueError("%s: CIF-1 files use LF line ends" % path)
    text = data.decode("utf-8")
    for n, line in enumerate(text.split("\n"), start=1):
        if line == "":
            continue
        yield n, parse_line(line)
