import os, sys; sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # put ref/ on the path
"""The `catch` statement (ruling OQ-315 A; SPEC-04 LS-131a, LS-167): `call catch (e) { ... };`
stands as a statement when the call's result is `E!void`, and its block may complete; any other
error union is C4012 at the call, and any other expression before `catch` stays C4012."""
import unittest

from cint_ref.exec import run_program

SET = ("error E {\n    bad,\n    worse,\n}\n\n"
       "E!void f(I64 x) {\n    if (x < 0) {\n        return .bad;\n    }\n    if (x == 0) {\n        return .worse;\n    }\n}\n\n"
       "E!I64 g() {\n    return .worse;\n}\n\n")


def run(src):
    return run_program((SET + src).encode("utf-8"), "t.ci", "run")


def value(src):
    o = run(src)
    assert o.kind == "value", (o.kind, o.diagnostic, o.record, o.message)
    return o.value.value


def diag(src):
    o = run(src)
    assert o.kind == "compile-error", (o.kind, o.record, o.message)
    p = o.diagnostic.position
    return o.diagnostic.code, p.line - SET.count("\n"), p.column


class CatchStatement(unittest.TestCase):
    def test_block_completes(self):
        src = ("export I64 run() {\n    I64 n = 0;\n    for i in 0..4 {\n        f(i - 2) catch (e) {\n"
               "            if (e == E.bad) {\n                n += 10;\n            } else {\n                n += 1;\n"
               "            }\n        };\n        n += 100;\n    }\n    return n;\n}\n")
        self.assertEqual(value(src), 421)

    def test_block_leaves(self):
        src = ("export I64 run() {\n    I64 n = 0;\n    for i in 0..9 {\n        f(i - 1) catch (e) {\n"
               "            if (e == E.bad) {\n                continue;\n            }\n            return n + 1000;\n"
               "        };\n        n += 1;\n    }\n    return n;\n}\n")
        self.assertEqual(value(src), 1000)

    def test_success_skips_block(self):
        self.assertEqual(value("export I64 run() {\n    I64 n = 5;\n    f(3) catch (e) {\n        n = 0;\n    };\n"
                               "    return n;\n}\n"), 5)

    def test_value_union_is_c4012_at_the_call(self):
        self.assertEqual(diag("export void run() {\n    g() catch (e) {\n        return;\n    };\n}\n"), ("C4012", 2, 5))

    def test_not_a_call_is_c4012(self):
        self.assertEqual(diag("export void run() {\n    I64 v = 0;\n    v catch (e) {\n    };\n}\n"), ("C4012", 3, 5))

    def test_binding_is_local_to_the_block(self):
        self.assertEqual(diag("export void run() {\n    f(1) catch (e) {\n    };\n    E x = e;\n}\n")[0], "C3005")

    def test_expression_form_still_needs_a_leaving_block(self):
        self.assertEqual(diag("export void run() {\n    I64 v = g() catch (e) {\n    };\n}\n")[0], "C4002")


if __name__ == "__main__":
    unittest.main()
