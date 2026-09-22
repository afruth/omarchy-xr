from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'studio'))
from backend import Manager, validate
from workspace_presets import built_in_setups
from test_studio import FakeHypr

class WorkspacePresetsTests(unittest.TestCase):
    def test_geometry_and_independent_copies(self):
        items=built_in_setups()
        self.assertEqual(len(items),8)
        for item in items:
            validate(item['layout'])
        portrait=items[4]['layout']['monitors']
        self.assertEqual([(m['width'],m['height'],m['y']) for m in portrait],[(1080,1920,0),(1920,1080,420),(1080,1920,0)])
        self.assertEqual([(m['width'],m['height']) for m in items[7]['layout']['monitors']],[(3840,2160),(1440,2160)])
        for index in (5,6):
            self.assertGreater(items[index]['layout']['monitors'][0]['curvature'],0)
            self.assertEqual(items[index]['layout']['curvature'],0)
        items[0]['layout']['monitors'][0]['width']=400
        self.assertEqual(built_in_setups()[0]['layout']['monitors'][0]['width'],1920)

    def test_switch_live_save_copy_and_enforce_limits(self):
        with tempfile.TemporaryDirectory() as folder:
            runner=FakeHypr(); manager=Manager(folder,'/unused',runner)
            manager.graphics_limits={'maxWidth':8192,'maxHeight':8192}
            try:
                manager.use_setup('builtin:one-fhd')
                self.assertFalse(manager.owned)
                self.assertFalse(manager.setups()['items'])
                manager.apply(manager.load())
                process=Mock();process.poll.return_value=None
                manager.viewer=process;manager.direct=True
                manager.use_setup('builtin:portrait-sides')
                self.assertEqual(len(manager.owned),3)
                process.terminate.assert_not_called()
                self.assertIs(manager.viewer,process)
                manager.save_setup('My portrait layout',manager.load())
                saved=manager.setups()['items'][0]
                self.assertFalse(saved['id'].startswith('builtin:'))
                with self.assertRaisesRegex(ValueError,'saved setup'):
                    manager.save_setup('Overwrite built-in',manager.load(),'builtin:one-fhd')
                before=manager.profile.read_text()
                manager.graphics_limits={'maxWidth':1920,'maxHeight':1920}
                with self.assertRaisesRegex(ValueError,'per-monitor limit'):
                    manager.use_setup('builtin:one-4k')
                self.assertEqual(manager.profile.read_text(),before)
                manager.applied=None
                with self.assertRaises(ValueError):manager.use_setup('builtin:one-4k')
            finally:
                manager.viewer=None;manager.direct=False;manager.cleanup();manager.lock.close()
