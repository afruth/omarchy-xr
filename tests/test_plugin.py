import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("install_studio", ROOT / "scripts/install-studio.py")
install_studio = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(install_studio)


class PluginContractTests(unittest.TestCase):
    def test_manifest_declares_kept_panel_and_bar_widget(self):
        manifest = json.loads((ROOT / "manifest.json").read_text())
        self.assertEqual(manifest["schemaVersion"], 1)
        self.assertEqual(manifest["id"], "afruth.omarchy-xr")
        self.assertEqual(set(manifest["kinds"]), {"panel", "bar-widget"})
        self.assertTrue(manifest["keepLoaded"])
        self.assertEqual(manifest["entryPoints"]["panel"], "studio/MonitorStudio.qml")
        self.assertEqual(manifest["entryPoints"]["barWidget"], "studio/BarWidget.qml")
        self.assertTrue((ROOT / manifest["entryPoints"]["panel"]).is_file())
        self.assertTrue((ROOT / manifest["entryPoints"]["barWidget"]).is_file())
        self.assertEqual(manifest["barWidget"]["defaultSection"], "right")
        self.assertFalse(manifest["barWidget"].get("allowMultiple", False))

    def test_enable_places_bar_widget_after_tray_and_leaves_an_existing_icon(self):
        config = {
            "bar": {"layout": {
                "left": [{"id": "omarchy.menu"}],
                "center": [],
                "right": [{"id": "omarchy.tray"}, {"id": "omarchy.audio"}],
            }},
            "plugins": [{"id": "afruth.omarchy-xr"}],
        }
        placed = install_studio.ensure_bar_widget(config)
        self.assertEqual([install_studio.bar_entry_id(entry) for entry in placed["bar"]["layout"]["right"]],
                         ["omarchy.tray", "afruth.omarchy-xr", "omarchy.audio"])
        self.assertEqual(placed["plugins"], [{"id": "afruth.omarchy-xr"}])
        self.assertIsNot(placed, config)
        again = install_studio.ensure_bar_widget(placed)
        self.assertEqual(again["bar"]["layout"]["right"], placed["bar"]["layout"]["right"])

    def test_install_copies_bar_widget_and_registers_layout(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "src"
            config_home = Path(temp) / "config"
            data_home = Path(temp) / "data"
            (root / "build").mkdir(parents=True)
            (root / "build/omarchy-xr").write_text("renderer")
            for file in install_studio.PLUGIN_FILES:
                destination = root / file
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_text((ROOT / file).read_text() if (ROOT / file).is_file() else "x")
            shell = config_home / "omarchy/shell.json"
            shell.parent.mkdir(parents=True)
            shell.write_text(json.dumps({
                "bar": {"layout": {"left": [], "center": [], "right": [{"id": "omarchy.tray"}]}},
                "plugins": [{"id": "afruth.omarchy-xr"}],
            }))
            runner = Mock(return_value=Mock(returncode=0, stdout="ok", stderr=""))
            target, placed = install_studio.install(root=root, config_home=config_home, data_home=data_home,
                                                    runner=runner, import_skies=False)
            self.assertTrue(placed)
            self.assertTrue((target / "studio/BarWidget.qml").is_file())
            self.assertEqual(json.loads((target / "manifest.json").read_text())["id"], "afruth.omarchy-xr")
            self.assertTrue((target / "bin/omarchy-xr").is_file())
            self.assertTrue((data_home / "applications/omarchy-xr-studio.desktop").is_file())
            layout = json.loads(shell.read_text())
            self.assertTrue(install_studio.widget_in_bar(layout))
            self.assertEqual(layout["plugins"], [{"id": "afruth.omarchy-xr"}])
            commands = [call.args[0][:3] for call in runner.call_args_list]
            self.assertIn(["omarchy", "bar", "put"], commands)
            self.assertIn(["omarchy-shell", "shell", "reloadConfig"], commands)

    def test_place_bar_widget_returns_false_for_invalid_shell_json(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "shell.json"
            runner = Mock(return_value=Mock(returncode=0, stdout="ok", stderr=""))
            path.write_text("{")
            self.assertFalse(install_studio.place_bar_widget(path, runner=runner))
            path.write_text("[]")
            self.assertFalse(install_studio.place_bar_widget(path, runner=runner))
            path.write_text("null")
            self.assertFalse(install_studio.place_bar_widget(path, runner=runner))
            self.assertEqual(path.read_text(), "null")


if __name__ == "__main__":
    unittest.main()
