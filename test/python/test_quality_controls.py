"""Negative controls use isolated trees, never edits to the working repository."""

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import re
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('architecture', ROOT / 'scripts/check_architecture.py')
architecture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(architecture)


class ArchitectureControls(unittest.TestCase):
    def tree(self, files, reason=None):
        with tempfile.TemporaryDirectory(prefix='ossie-architecture-') as directory:
            root = Path(directory)
            for name, text in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            result = subprocess.run(
                [sys.executable, str(ROOT / 'scripts/check_architecture.py'), '--root', str(root)],
                capture_output=True,
                text=True,
            )
            if reason:
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(reason, result.stderr)
            else:
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_direct_forbidden(self):
        self.tree(
            {'src/compile/compile.cpp': '#include "ossie/state.hpp"\n', 'src/include/ossie/state.hpp': ''},
            'forbidden state/functions',
        )

    def test_indirect_forbidden(self):
        self.tree(
            {
                'src/include/ossie/compile.hpp': '#include <ossie/helper.hpp>\n',
                'src/include/ossie/helper.hpp': '#include "state.hpp"\n',
                'src/include/ossie/state.hpp': '',
            },
            'compile.hpp -> src/include/ossie/helper.hpp -> src/include/ossie/state.hpp',
        )

    def test_model_forbidden(self):
        self.tree(
            {'src/model/validate.cpp': '#include "ossie/functions.hpp"\n', 'src/include/ossie/functions.hpp': ''},
            'forbidden state/functions',
        )

    def test_header_cycle(self):
        self.tree(
            {'src/include/ossie/a.hpp': '#include "b.hpp"\n', 'src/include/ossie/b.hpp': '#include "a.hpp"\n'},
            'header cycle',
        )

    def test_relative_and_ast_and_comments(self):
        self.tree(
            {
                'src/compile/c.cpp': '// #include "ossie/state.hpp"\n/*\n#include "bad.hpp"\n*/\n#include "../include/ossie/model.hpp"\n#include <duckdb/parser/parser.hpp>\n',
                'src/include/ossie/model.hpp': '#include "duckdb.hpp"\n',
            }
        )

    def test_missing_reference(self):
        self.tree({'src/model/validate.cpp': '#include "ossie/missing.hpp"\n'}, 'unresolved project include')

    def test_angle_missing_reference(self):
        self.tree({'src/model/validate.cpp': '#include <missing.hpp>\n'}, 'unresolved project include')

    def test_indirect_runtime(self):
        self.tree(
            {
                'src/compile/c.cpp': '#include "ossie/helper.hpp"\n',
                'src/include/ossie/helper.hpp': '#include "duckdb/main/connection.hpp"\n',
            },
            'helper.hpp -> duckdb/main/connection.hpp',
        )

    def test_nonliteral_reference(self):
        self.tree({'src/compile/c.cpp': '#include MY_HEADER\n'}, 'nonliteral include')

    def test_zero_inputs(self):
        self.tree({}, 'zero project')

    def test_unscanned_source(self):
        self.tree(
            {'src/compile/c.cpp': '', 'CMakeLists.txt': 'set(EXTENSION_SOURCES src/compile/c.cpp src/missing.cpp)'},
            'unscanned=',
        )

    def test_undeclared_source(self):
        self.tree({'src/compile/c.cpp': '', 'CMakeLists.txt': 'set(EXTENSION_SOURCES src/other.cpp)'}, 'unlisted=')

    def test_empty_compiled_inventory(self):
        self.tree(
            {'src/include/ossie/model.hpp': '', 'CMakeLists.txt': 'set(EXTENSION_SOURCES )'},
            'empty compiled source inventory',
        )

    def test_compiler_io(self):
        self.tree({'src/compile/c.cpp': '#include "duckdb/main/client_context.hpp"\n'}, 'forbidden runtime include')

    def test_precise_parser_adapter(self):
        self.tree(
            {
                'src/model/parser.cpp': '#include "duckdb/main/client_context.hpp"\n#include "duckdb/common/file_system.hpp"\n'
            }
        )
        self.tree({'src/model/parser.cpp': '#include "duckdb/main/connection.hpp"\n'}, 'forbidden runtime include')

    def test_parser_exception_does_not_allow_state(self):
        self.tree(
            {'src/model/parser.cpp': '#include "ossie/state.hpp"\n', 'src/include/ossie/state.hpp': ''},
            'forbidden state/functions',
        )


class FormatControls(unittest.TestCase):
    def test_script_format_failure_and_recovery(self):
        with tempfile.TemporaryDirectory(prefix='ossie-format-') as directory:
            root = Path(directory)
            script = root / 'scripts/control.py'
            script.parent.mkdir()
            script.write_text('value=  {"a":1}\n')
            cmd = [
                sys.executable,
                str(ROOT / 'duckdb/scripts/format.py'),
                '--all',
                '--check',
                '--directories',
                'scripts',
            ]
            result = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('control.py', result.stdout + result.stderr)
            fixed = subprocess.run(
                [*cmd[:2], '--all', '--fix', '--noconfirm', '--directories', 'scripts'],
                cwd=root,
                capture_output=True,
                text=True,
            )
            self.assertEqual(fixed.returncode, 0, fixed.stdout + fixed.stderr)
            result = subprocess.run(cmd, cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class WiringControls(unittest.TestCase):
    def test_state_runner_is_required_by_sql_test_target(self):
        result = subprocess.run(['make', '-n', 'test_release_internal'], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('--target ossie_state_test', result.stdout)
        self.assertIn('ossie_state_test "', result.stdout)
        self.assertLess(result.stdout.index('ossie_state_test'), result.stdout.index('test/unittest'))

    def test_actual_ci_gate_fails_closed_for_each_dependency(self):
        text = (ROOT / '.github/workflows/MainDistributionPipeline.yml').read_text()
        section = text.split('  ci-gate:', 1)[1].split('\n  code-quality-check:', 1)[0]
        self.assertRegex(section, r'if:\s*always\(\)')
        jobs = [name.strip() for name in re.search(r'needs: \[([^]]+)\]', section)[1].split(',')]
        self.assertIn('project-controls', jobs)
        self.assertIn('artifact-load', jobs)
        script = textwrap.dedent(section.split('run: |\n', 1)[1])

        def execute(status):
            expanded = re.sub(r'\$\{\{\s*needs\.([\w-]+)\.result\s*\}\}', lambda match: status[match[1]], script)
            return subprocess.run(['bash', '-c', expanded], capture_output=True, text=True).returncode

        self.assertEqual(execute(dict.fromkeys(jobs, 'success')), 0)
        for job in jobs:
            for failed in ['failure', 'skipped', 'cancelled']:
                status = dict.fromkeys(jobs, 'success')
                status[job] = failed
                self.assertNotEqual(execute(status), 0, (job, failed))


if __name__ == '__main__':
    unittest.main()
