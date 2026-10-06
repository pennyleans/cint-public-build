"""Focused evidence parsing and generated C scanner regressions."""
import hashlib
import subprocess
import pathlib
import tempfile
import unittest

import parity


class EvidenceTests(unittest.TestCase):
    def test_braces_ignore_comments_strings_and_characters(self):
        text = b'void f() { /* {{{ */ char c = \'{\'; "}\\\"{"; // }}}\n { } }'
        self.assertEqual(parity.c_brace_depth(text), 2)

    def test_braces_honor_line_splices(self):
        self.assertEqual(parity.c_brace_depth(b'void f() { // }\\\n }\n }'), 1)

    def test_braces_reject_unbalanced_or_unterminated_input(self):
        for text in (b'}', b'{', b'/* {', b'"{', b"'{"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                parity.c_brace_depth(text)

    def test_reference_process_failure_is_not_an_outcome(self):
        result = subprocess.CompletedProcess(['reference'], 1, b'', b'traceback')
        with self.assertRaisesRegex(ValueError, 'process'):
            parity.reference_observation(result, b'')

    def test_reference_status_comes_from_rendered_outcome(self):
        for outcome, status, fields in (
            ('value', 0, 'stdout-bytes 0\nreturn I64 3\nfuel-consumed 1\n'),
            ('fault', 1, 'stdout-bytes 0\nfuel-consumed 2\nfault.code E_OVERFLOW\n'),
            ('compile-error', 2, 'diagnostic.code C9004\ndiagnostic.position x.ci:9:261\n'),
        ):
            raw = ('case x\nsource reference\noutcome ' + outcome + '\n' + fields).encode()
            result = subprocess.CompletedProcess(['reference'], 0, raw, b'')
            observation = parity.reference_observation(result, b'')
            self.assertEqual(observation['exit_status'], status)
            self.assertEqual(observation['outcome'], outcome)

    def test_reference_stdout_digest_and_length_are_checked(self):
        raw = ('case x\nsource reference\noutcome value\nstdout-bytes 2\nstdout-sha256 ' +
               hashlib.sha256(b'x\n').hexdigest() + '\nfuel-consumed 0\n').encode()
        result = subprocess.CompletedProcess(['reference'], 0, raw, b'')
        self.assertEqual(parity.reference_observation(result, b'x\n')['exit_status'], 0)
        for stdout in (b'x', b'y\n'):
            with self.subTest(stdout=stdout), self.assertRaises(ValueError):
                parity.reference_observation(result, stdout)

    def test_generated_modules_follow_current_manifest_and_reject_tampering(self):
        with tempfile.TemporaryDirectory() as directory:
            cache = pathlib.Path(directory)
            (cache / 'out').mkdir()
            stage = cache / 'out.stage-7'
            stage.mkdir()
            files = {'x.c': b'void f() {}', 'x.sites': b'sir 1\nsites x\n',
                     'cint-program.c': b'int main() {}'}
            digests = {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}
            manifest = ('cint-manifest-1\n' + ''.join(
                '%s %d %s\n' % (digests[name], len(data), name)
                for name, data in files.items())).encode()
            for name, data in files.items():
                (stage / name).write_bytes(data)
            (stage / 'MANIFEST').write_bytes(manifest)
            digest = hashlib.sha256(manifest).hexdigest()
            (cache / 'out/MANIFEST.ref').write_text('out.stage-7 ' + digest + '\n')
            record = {'identity': {'generated': {'manifest': digest, 'files': digests}},
                      'observations': {'cache': str(cache)}}
            result = parity.generated_modules(record)
            self.assertEqual(set(result), {'x.c', 'x.sites'})
            self.assertEqual(result['x.c']['brace_depth'], 1)
            self.assertEqual(result['x.sites']['sha256'], digests['x.sites'])
            (stage / 'x.sites').write_bytes(b'stale sir')
            with self.assertRaisesRegex(ValueError, 'digest mismatch'):
                parity.generated_modules(record)
            record['identity']['generated']['files'] = {}
            with self.assertRaisesRegex(ValueError, 'file sets differ'):
                parity.generated_modules(record)

    def test_frozen_inventory_covers_every_entry_without_seed_parity(self):
        inventory = parity.inventory()
        self.assertEqual(len(inventory['boot']), 3)
        self.assertEqual(len(inventory['boundary']), 18)
        self.assertEqual(len(inventory['waiting']), 2)
        self.assertEqual(len(inventory['excluded']), 16)
        entries = [row['entry'] for row in inventory['boundary']
                   if row['case'] == 'control/else_if_126']
        self.assertEqual(entries, ['first', 'middle', 'last', 'fallback'])
        self.assertTrue(all(row['status'].startswith('seed-only') for row in inventory['excluded']))

    def test_parity_selection_preserves_existing_test_inventory(self):
        import run as runner
        loader = unittest.TestLoader()
        original_names = loader.getTestCaseNames(runner.RunTests)
        names, suite = runner.parity_suite()
        self.assertEqual(len(names), 21)
        self.assertEqual(suite.countTestCases(), 21)
        selected = 'test_parity_control_else_if_126_middle'
        names, suite = runner.parity_suite([selected])
        self.assertEqual(names, [selected])
        self.assertEqual(suite.countTestCases(), 1)
        self.assertEqual(loader.getTestCaseNames(runner.RunTests), original_names)
        with self.assertRaisesRegex(ValueError, 'unknown parity case'):
            runner.parity_suite(['test_parity_missing'])


if __name__ == '__main__':
    unittest.main()
