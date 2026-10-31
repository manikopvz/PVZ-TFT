import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('package_assets', Path(__file__).resolve().parents[1]/'package_assets.py')
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)

class AssetPackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source, self.output = self.root/'Debug', self.root/'assets'
        for name in assets.RESOURCE_DIRS: (self.source/name).mkdir(parents=True)
        (self.source/'properties/resources.xml').write_text('<ResourceManifest/>')

    def test_personal_data_and_windows_binaries_are_not_published(self):
        (self.source/'images/plant.png').write_bytes(b'original-image-bytes')
        for filename in ['appdata/userdata.dat', 'appdata/cached/sound.wav', 'LawnProject.exe', 'bass.dll', 'src/private.txt']:
            path = self.source/filename; path.parent.mkdir(parents=True, exist_ok=True); path.write_text('private')
        records = assets.package(self.source, self.output)
        self.assertEqual((self.output/'images/plant.png').read_bytes(), b'original-image-bytes')
        self.assertEqual({x['path'] for x in records}, {'images/plant.png', 'properties/resources.xml'})
        for name in ['appdata','src','LawnProject.exe','bass.dll']: self.assertFalse((self.output/name).exists())
        self.assertEqual(records, json.loads((self.output/'asset-manifest.json').read_text()))

    def test_removed_resource_does_not_survive_a_rebuild(self):
        image = self.source/'images/removed.png'; image.write_bytes(b'old')
        assets.package(self.source, self.output); image.unlink()
        assets.package(self.source, self.output)
        self.assertFalse((self.output/'images/removed.png').exists())

    def test_missing_resources_fail_before_destroying_previous_build(self):
        assets.package(self.source, self.output)
        (self.source/'properties/resources.xml').unlink()
        with self.assertRaises(ValueError): assets.package(self.source, self.output)
        self.assertTrue((self.output/'properties/resources.xml').exists())

    def test_unrelated_output_is_not_overwritten(self):
        self.output.mkdir(); (self.output/'keep.txt').write_text('keep')
        with self.assertRaises(ValueError): assets.package(self.source, self.output)
        self.assertEqual((self.output/'keep.txt').read_text(), 'keep')

    def test_source_and_nested_outputs_are_rejected(self):
        for output in [self.source, self.source/'images/out', self.root]:
            with self.assertRaises(ValueError): assets.package(self.source, output)

if __name__ == '__main__': unittest.main()
