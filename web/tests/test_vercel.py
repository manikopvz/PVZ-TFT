import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('package_vercel', Path(__file__).parents[1] / 'package_vercel.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class VercelPackageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'dist'
        self.source.mkdir()
        for name in ('index.html', 'index.js', 'index.wasm', 'index.data', 'LICENSE'):
            (self.source / name).write_bytes(b'payload-' + name.encode())

    def test_payload_mime_and_repackage_preserve_project_link(self):
        destination = self.root / 'vercel-dist'
        module.package(self.source, destination)
        output = destination / '.vercel' / 'output'
        for file in self.source.iterdir():
            self.assertEqual((output / 'static' / file.name).read_bytes(), file.read_bytes())
        config = json.loads((output / 'config.json').read_text())
        self.assertEqual(config['version'], 3)
        self.assertEqual(config['overrides']['index.wasm']['contentType'], 'application/wasm')
        project = destination / '.vercel' / 'project.json'
        project.write_text('keep project association')
        (output / 'static' / 'stale').write_text('old')
        module.package(self.source, destination)
        self.assertFalse((output / 'static' / 'stale').exists())
        self.assertEqual(project.read_text(), 'keep project association')

    def test_missing_payload_does_not_destroy_previous_build(self):
        destination = self.root / 'vercel-dist'
        module.package(self.source, destination)
        (self.source / 'index.wasm').unlink()
        with self.assertRaises(ValueError):
            module.package(self.source, destination)
        self.assertTrue((destination / '.vercel/output/static/index.wasm').exists())

    def test_reject_overlapping_paths(self):
        for destination in (self.source, self.source / 'child', self.root):
            with self.assertRaises(ValueError):
                module.package(self.source, destination)
