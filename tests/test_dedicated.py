import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'studio'))
from dedicated_helper import headset_edid, own_override
class EdidTests(unittest.TestCase):
    def edid(self,name=b'VITURE'):
        e=bytearray(128);e[:8]=bytes.fromhex('00ffffffffffff00');e[54:59]=b'\x00\x00\x00\xfc\x00';e[59:72]=name.ljust(13,b' ');e[127]=-sum(e[:127])%256
        return bytes(e)
    def test_preserves_identity_and_timings(self):
        original=self.edid();result=headset_edid(original)
        self.assertEqual(result[:126],original[:126]);self.assertEqual(result[126],1)
        self.assertEqual(result[128:133],bytes([0x70,0x20,7,8,0]))
        self.assertEqual(sum(result[:128])%256,0);self.assertEqual(sum(result[128:])%256,0)
        self.assertEqual(sum(result[129:141])%256,0)
    def test_refuses_other_displays_and_bad_data(self):
        for e in (self.edid(b'Laptop'),b'',self.edid()[:-1],self.edid()[:-1]+b'\x01'):
            with self.assertRaises(ValueError):headset_edid(e)
    def test_own_displayid_block_is_recognized(self):
        self.assertTrue(own_override(headset_edid(self.edid())))
        self.assertFalse(own_override(self.edid()))
        self.assertFalse(own_override(b'x'*256))

class AuthorizationTests(unittest.TestCase):
    def test_policy_only_grants_installed_helper_to_active_session(self):
        import xml.etree.ElementTree as ET
        from dedicated import HELPER
        root=Path(__file__).resolve().parents[1]
        action=ET.parse(root/'packaging/io.github.afruth.omarchy-xr.display.policy').getroot().find('action')
        self.assertEqual(action.findtext('defaults/allow_any'),'no')
        self.assertEqual(action.findtext('defaults/allow_inactive'),'no')
        self.assertEqual(action.findtext('defaults/allow_active'),'yes')
        self.assertEqual(action.find('annotate').text,str(HELPER))
        self.assertTrue((root/'studio/dedicated_helper.py').read_text().startswith('#!/usr/bin/python3 -I\n'))

    def test_missing_helper_never_falls_back_to_privileged_python(self):
        from dedicated import Dedicated
        from unittest.mock import patch
        with patch('dedicated.HELPER') as helper, patch('dedicated.subprocess.Popen') as popen:
            helper.is_file.return_value=False
            with self.assertRaisesRegex(RuntimeError,'Setup & integrations'):
                Dedicated('/tmp','renderer').start('DP-1')
            popen.assert_not_called()

    def test_invalid_connector_rejected_before_accessing_devices(self):
        import dedicated_helper
        from unittest.mock import patch
        for connector in ('../DP-1','HDMI-A-1','DP-1/../../foo','--help','DP-1\n'):
            with patch('dedicated_helper.os.geteuid',return_value=0), patch('sys.argv',['helper',connector]), patch('dedicated_helper.Path') as path:
                with self.assertRaises(ValueError):dedicated_helper.main()
                path.assert_not_called()
