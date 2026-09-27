import json,re,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'studio'))
from input_settings import ACTION_IDS,DEFAULTS,conflicts,key,modifier,migrate,tsv,validate_controls,save_controls,load_controls


def settings(**keys):
    return dict(DEFAULTS,keys=dict(DEFAULTS['keys'],**keys))


class InputSettingsTests(unittest.TestCase):
    def test_defaults_are_valid_and_unique(self):
        self.assertEqual(validate_controls(DEFAULTS),DEFAULTS)
        self.assertEqual(DEFAULTS['modifier'],'CTRL + ALT')
        self.assertEqual(conflicts(DEFAULTS),[])

    def test_normalization(self):
        self.assertEqual(modifier('alt + control'),('CTRL + ALT',12))
        self.assertEqual(key('shift+left'),('SHIFT + Left',1,'left'))
        self.assertEqual(key('g'),('G',0,'g'))
        self.assertEqual(key('Page_down'),('Page_Down',0,'page_down'))
        self.assertEqual(key(''),('',0,''))
        value=validate_controls(dict(settings(recenter='space'),modifier='super+alt'))
        self.assertEqual(value['modifier'],'ALT + SUPER')

    def test_invalid_values(self):
        for bad in ('CTRL','SHIFT + SHIFT','SHIFT + CTRL + X','','CTRL + CTRL'):
            with self.subTest(modifier=bad),self.assertRaises(ValueError):modifier(bad)
        for bad in ('CTRL + R','R\nprint(1)','SHIFT + SHIFT + R','ALT + Left','emoji','F36'):
            with self.subTest(key=bad),self.assertRaises(ValueError):key(bad)
        for field,value in [('fingers',4),('fingers',True),('keys',[]),('modifier','SHIFT + ALT + X')]:
            with self.subTest(field=field),self.assertRaises(ValueError):
                validate_controls(dict(DEFAULTS,**{field:value}))
        with self.assertRaisesRegex(ValueError,'Unknown XR action'):
            validate_controls(settings(launch_missiles='X'))

    def test_duplicate_and_reserved_keys(self):
        with self.assertRaisesRegex(ValueError,'Same key as Recenter'):
            validate_controls(settings(help='space'))
        with self.assertRaisesRegex(ValueError,'virtual terminals'):
            validate_controls(settings(help='F2'))
        # F-keys are fine on another layer.
        validate_controls(dict(settings(help='F2'),modifier='SUPER + ALT'))
        with self.assertRaisesRegex(ValueError,'already includes SHIFT'):
            validate_controls(dict(DEFAULTS,modifier='CTRL + ALT + SHIFT'))

    def test_desktop_binding_conflicts_are_named(self):
        omarchy=[{'modmask':12,'key':'Delete','description':'Log out'},{'modmask':12,'key':'space','description':'Launcher'},
                 {'modmask':12,'key':'H','description':'XR: help'}]
        problems=conflicts(validate_controls(DEFAULTS),omarchy)
        self.assertEqual(problems,[('recenter','Already used by Launcher')])
        with self.assertRaisesRegex(ValueError,r'Recenter \(CTRL \+ ALT \+ space\): Already used by Launcher'):
            validate_controls(DEFAULTS,omarchy)
        # A disabled action never conflicts.
        validate_controls(settings(recenter=''),omarchy)

    def test_version_one_profiles_migrate_to_the_layer(self):
        old={'fingers':5,'fit_all':'CTRL + Up','fit_target':'CTRL + Down','recenter':'','zoom_in':'','zoom_out':''}
        self.assertEqual(migrate(old),dict(DEFAULTS,fingers=5))
        self.assertEqual(migrate(dict(old,fingers=4))['fingers'],3)

    def test_tsv_mailbox(self):
        text=tsv(validate_controls(settings(redo='')))
        lines=text.splitlines()
        self.assertEqual(lines[:3],['v2','modifier\tCTRL + ALT','fingers\t3'])
        self.assertIn('key\tnudge_left\tSHIFT + Left',lines)
        self.assertIn('key\tredo\t',text)
        self.assertEqual(len(lines),3+len(ACTION_IDS))

    def test_lua_knows_every_action_with_the_same_defaults(self):
        lua=(ROOT/'config/xr-controls.lua').read_text()
        table=re.search(r'local LAYER_ACTIONS=\{(.*?)\n\}',lua,re.S)
        self.assertIsNotNone(table,'LAYER_ACTIONS table missing from xr-controls.lua')
        rows=re.findall(r'^\s*\{"(\w+)",key="([^"]*)"',table.group(1),re.M)
        self.assertEqual(rows,[(a,DEFAULTS['keys'][a]) for a in ACTION_IDS])

    def test_persist_and_apply(self):
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp);calls=[]
            def runner(*args):calls.append(args);return '[]' if args==('-j','binds') else 'ok'
            self.assertEqual(load_controls(directory),DEFAULTS)
            value=dict(settings(recenter='R'),fingers=5)
            save_controls(directory,value,runner)
            self.assertEqual(load_controls(directory)['keys']['recenter'],'R')
            self.assertTrue((directory/'controls-settings.tsv').read_text().startswith('v2\n'))
            self.assertEqual(json.loads((directory/'controls-settings.json').read_text())['version'],2)
            self.assertEqual(calls[-1],('eval','omarchy_xr_controls.refresh()'))

    def test_old_profile_on_disk_loads_as_defaults(self):
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp)
            (directory/'controls-settings.json').write_text(json.dumps({'fingers':3,'fit_all':'CTRL + Up','fit_target':'CTRL + Down','recenter':'','zoom_in':'','zoom_out':''}))
            self.assertEqual(load_controls(directory),DEFAULTS)

    def test_apply_failure_restores_saved_values(self):
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp)
            save_controls(directory,DEFAULTS,lambda *a:'[]' if a==('-j','binds') else 'ok')
            def failed(*args):
                if args==('eval','omarchy_xr_controls.refresh()'):raise RuntimeError('apply failed')
                return '[]' if args==('-j','binds') else 'ok'
            with self.assertRaises(RuntimeError):save_controls(directory,settings(recenter='R'),failed)
            self.assertEqual(load_controls(directory),DEFAULTS)
