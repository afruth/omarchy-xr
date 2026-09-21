import sys,tempfile,unittest,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'studio'))
from input_settings import DEFAULTS,validate_controls,save_controls,load_controls
class InputSettingsTests(unittest.TestCase):
    def test_defaults_and_normalization(self):
        self.assertEqual(validate_controls(DEFAULTS),DEFAULTS)
        value=validate_controls(dict(DEFAULTS,recenter='alt+control+r',fingers=5))
        self.assertEqual(value['recenter'],'CTRL + ALT + R')
    def test_invalid_duplicate_and_injection(self):
        for field,value in [('fingers',1),('fingers',2),('fingers',4),('fingers',True),('fingers',6),('recenter','R'),('recenter','CTRL + Up'),('recenter','CTRL + R\nprint(1)'),('recenter','CTRL + CTRL + R')]:
            with self.subTest(field=field,value=value),self.assertRaises(ValueError):
                validate_controls(dict(DEFAULTS,**{field:value}))
    def test_conflict(self):
        with self.assertRaisesRegex(ValueError,'already used'):
            validate_controls(DEFAULTS,[{'modmask':4,'key':'Up','description':'Desktop action'}])
        validate_controls(DEFAULTS,[{'modmask':4,'key':'Up','description':'XR: fit workspace'}])
    def test_persist_and_apply_without_layout_change(self):
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp);calls=[]
            def runner(*args):calls.append(args);return '[]' if args==('-j','binds') else 'ok'
            self.assertEqual(load_controls(directory),DEFAULTS)
            value=dict(DEFAULTS,fingers=5,recenter='CTRL + ALT + R')
            save_controls(directory,value,runner)
            self.assertEqual(load_controls(directory),value)
            self.assertTrue((directory/'controls-settings.tsv').read_text().startswith('5\n'))
            self.assertEqual(calls[-1],('eval','omarchy_xr_controls.refresh()'))
            self.assertFalse((directory/'layout.json').exists())
    def test_apply_failure_restores_saved_values(self):
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp)
            save_controls(directory,DEFAULTS,lambda *a:'[]' if a==('-j','binds') else 'ok')
            def failed(*args):
                if args==('eval','omarchy_xr_controls.refresh()'):raise RuntimeError('apply failed')
                return '[]' if args==('-j','binds') else 'ok'
            with self.assertRaises(RuntimeError):save_controls(directory,dict(DEFAULTS,fingers=5),failed)
            self.assertEqual(load_controls(directory),DEFAULTS)
            self.assertTrue((directory/'controls-settings.tsv').read_text().startswith('3\n'))
