import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("notification_install", ROOT / "scripts/install-notifications.py")
installer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(installer)


class NotificationInstallTests(unittest.TestCase):
    def test_installs_extension_with_original_service_reference(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            native = root / "native"
            native.mkdir()
            original = "import QtQuick\nItem {}\n"
            (native / "Service.qml").write_text(original)
            target = installer.install_files(ROOT / "notifications", root / "config", native)
            manifest = json.loads((target / "manifest.json").read_text())
            self.assertEqual(manifest["omarchy"]["clonedFrom"], "omarchy.notifications")
            self.assertTrue((target / manifest["entryPoints"]["service"]).is_file())
            self.assertIn(json.dumps(native.as_uri()), (target / "Service.qml").read_text())
            self.assertTrue((target / "Bridge.qml").is_file())
            self.assertEqual((native / "Service.qml").read_text(), original)
            installer.install_files(ROOT / "notifications", root / "config", native)

    def test_preserves_other_enabled_notification_clone(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory)
            plugin = config / "omarchy/plugins/custom.notifications"
            plugin.mkdir(parents=True)
            (plugin / "manifest.json").write_text(json.dumps({
                "id": "custom.notifications", "omarchy": {"clonedFrom": "omarchy.notifications"}}))
            settings = config / "omarchy/shell.json"
            settings.write_text(json.dumps({"plugins": [{"id": "custom.notifications"}]}))
            before = settings.read_text()
            with self.assertRaisesRegex(RuntimeError, "custom.notifications"):
                installer.check_existing(config)
            self.assertEqual(settings.read_text(), before)
            settings.write_text(json.dumps({"plugins": []}))
            installer.check_existing(config)

    def test_rejects_missing_native_service_before_installing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(RuntimeError):
                installer.install_files(ROOT / "notifications", root / "config", root / "missing")
            self.assertFalse((root / "config").exists())
