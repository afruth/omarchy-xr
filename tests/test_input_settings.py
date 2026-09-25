import sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'studio'))
from input_settings import DEFAULTS,chord,validate_controls,save_controls,load_controls
class InputSettingsTests(unittest.TestCase):
    def test_defaults_and_normalization(self):
        self.assertEqual(validate_controls(DEFAULTS),DEFAULTS)
        value=validate_controls(dict(DEFAULTS,recenter='alt+control+r',fingers=5))
        self.assertEqual(value['recenter'],'CTRL + ALT + R')
    def test_slash_and_period_keys(self):
        self.assertEqual(chord('super+Slash'),('SUPER + slash',64,'slash'))
        self.assertEqual(chord('SUPER + period'),('SUPER + period',64,'period'))
        self.assertEqual(validate_controls(dict(DEFAULTS,recenter='SUPER + slash'))['recenter'],'SUPER + slash')
        # The canvas takeovers are bound with 'XR:' descriptions, so they never count as a clash.
        validate_controls(dict(DEFAULTS,recenter='SUPER + slash'),[{'modmask':64,'key':'slash','description':'XR: search window (canvas)'}])
    def test_canvas_chords_reserved(self):
        # Bound only in canvas mode with 'XR:' descriptions, so the binding check alone would let them through.
        for text in ('SUPER + F','super + ctrl + g','ALT + SUPER + P','SUPER + TAB','ALT + Tab','SHIFT + ALT + Tab','SUPER + Left','SUPER + SHIFT + Down'):
            with self.subTest(chord=text),self.assertRaisesRegex(ValueError,'reserved for the window canvas'):
                validate_controls(dict(DEFAULTS,recenter=text))
        self.assertEqual(validate_controls(dict(DEFAULTS,recenter='SUPER + CTRL + P'))['recenter'],'CTRL + SUPER + P')
        self.assertEqual(validate_controls(dict(DEFAULTS,recenter='SUPER + ALT + P'),reserve=False)['recenter'],'ALT + SUPER + P')
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
