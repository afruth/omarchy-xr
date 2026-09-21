import io
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import Mock,patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'studio'))
from laptop_display import LaptopDisplay, internal, snapshot, watch, validate_snapshot
from backend import Manager,default_layout
from test_studio import FakeHypr

PANEL={'name':'eDP-1','width':1920,'height':1080,'refreshRate':60.003,'scale':1.25,'transform':0,'x':-1536,'y':0}

class LaptopDisplayTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.enabled=True;self.commands=[]
    def tearDown(self): self.temp.cleanup()
    def runner(self,*args):
        self.commands.append(args)
        if args==('-j','monitors'):return json.dumps([PANEL] if self.enabled else [])
        if args[0]=='eval':self.enabled='disabled=false' in args[1];return 'ok'
        raise AssertionError(args)
    def journal(self):
        (self.root/'laptop-display.json').write_text(json.dumps({'session':os.environ.get('HYPRLAND_INSTANCE_SIGNATURE',''),'monitors':[PANEL]}))
    def test_only_active_internal_panels(self):
        choices=[PANEL,{**PANEL,'name':'HDMI-A-1'},{**PANEL,'name':'DP-1'},{**PANEL,'name':'OMXR-1'}, {**PANEL,'disabled':True},{**PANEL,'dpmsStatus':False}]
        self.assertEqual(internal(choices),[PANEL])
        self.assertEqual(snapshot([PANEL]),[PANEL])
        with self.assertRaises(ValueError):validate_snapshot([{**PANEL,'name':'DP-1'}])
    def test_restore_preserves_mode_and_retains_failed_journal(self):
        self.journal();display=LaptopDisplay(self.root,self.runner)
        display.restore()
        cmd=self.commands[0][1]
        for value in ('disabled=false','1920x1080@60.003','-1536x0','scale=1.25','transform=0'):
            self.assertIn(value,cmd)
        self.assertFalse(display.journal.exists())
        self.journal();display.runner=Mock(side_effect=RuntimeError('retry'))
        with self.assertRaises(RuntimeError):display.restore()
        self.assertTrue(display.journal.exists())
    def test_new_compositor_session_does_not_replay_old_geometry(self):
        self.journal();display=LaptopDisplay(self.root,self.runner)
        with patch.dict(os.environ,{'HYPRLAND_INSTANCE_SIGNATURE':'different-session'}):display.restore()
        self.assertFalse(self.commands)
    def exercise_watch(self,connection,identity,selection):
        with patch('laptop_display.hypr',side_effect=self.runner),patch('laptop_display.connection_alive',side_effect=connection),patch('laptop_display.process_identity',side_effect=identity),patch('laptop_display.select.select',side_effect=selection),patch('laptop_display.signal.signal'),patch('sys.stdout',io.StringIO()) as output:
            watch(self.root,123,'DP-1')
        self.assertTrue(self.enabled)
        self.assertFalse((self.root/'laptop-display.json').exists())
        return output.getvalue()
    def test_glasses_disconnect_restores(self):
        self.assertIn('ready',self.exercise_watch([True,False],['pid','pid'],[([],[],[])]))
        self.assertTrue(any('disabled=true' in args[-1] for args in self.commands))
    def test_renderer_crash_restores(self):
        self.assertIn('ready',self.exercise_watch([True],['pid',None],[([],[],[])]))
    def test_parent_crash_pipe_eof_restores(self):
        self.assertIn('ready',self.exercise_watch([True,True],['pid','pid'],[([],[],[]),([sys.stdin],[],[])]))
    def test_cancel_during_launch_never_disables(self):
        self.exercise_watch([True],['pid'],[([sys.stdin],[],[])])
        self.assertFalse(any('disabled=true' in args[-1] for args in self.commands))
    def test_partial_disable_failure_restores(self):
        def fail(*args):
            result=self.runner(*args)
            if args[0]=='eval' and 'disabled=true' in args[1]:raise RuntimeError('disable failure')
            return result
        with patch('laptop_display.hypr',side_effect=fail),patch('laptop_display.connection_alive',return_value=True),patch('laptop_display.process_identity',return_value='pid'),patch('laptop_display.select.select',return_value=([],[],[])),patch('laptop_display.signal.signal'):
            with self.assertRaisesRegex(RuntimeError,'disable failure'):watch(self.root,123,'DP-1')
        self.assertTrue(self.enabled)
        self.assertFalse((self.root/'laptop-display.json').exists())
    def test_preference_requires_verified_stereo_and_does_not_erase_obs(self):
        manager=Manager(self.root,'/unused',FakeHypr())
        try:
            manager.laptop=Mock()
            manager.spectator_enabled=True
            manager.set_laptop_off(True)
            manager.laptop.start.assert_not_called()
            self.assertEqual(json.loads(manager.presentation_profile.read_text()),{'spectator':True,'laptopOff':True})
            process=Mock();process.poll.return_value=None;process.pid=123
            manager.viewer=process;manager.direct=True;manager.stereo_active=True;manager.dedicated.output='DP-1'
            (self.root/'pose.sock.stats').write_text(json.dumps({'pid':123,'time':time.monotonic(),'fps':60}))
            manager.disable_laptop_display()
            manager.laptop.start.assert_called_once_with(123,'DP-1')
            manager.set_laptop_off(False)
            manager.laptop.stop.assert_called_once()
            self.assertFalse(manager.laptop_off_enabled)
            self.assertTrue(manager.spectator_enabled)
            process.poll.return_value=1
            with self.assertRaisesRegex(RuntimeError,'Start stereo'):manager.disable_laptop_display()
        finally:
            manager.viewer=None;manager.direct=False;manager.stereo_active=False;manager.lock.close()
    def test_live_layout_apply_works_with_laptop_disabled(self):
        fake=FakeHypr();manager=Manager(self.root,'/unused',fake)
        manager.graphics_limits={'maxWidth':8192,'maxHeight':8192}
        try:
            layout=default_layout();manager.apply(layout)
            original=fake.outputs.pop('eDP-1')
            manager.laptop.stop=Mock(side_effect=lambda:fake.outputs.update({'eDP-1':original}))
            with patch.object(manager.laptop,'saved',return_value=[original]):
                layout['monitors'][0]['width']=2560
                manager.apply(layout)
            self.assertEqual(fake.outputs[manager.prefix+'1']['width'],2560)
        finally:manager.cleanup();manager.lock.close()
