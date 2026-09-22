import sys
import unittest
from pathlib import Path
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "studio"))
import setup_actions
from backend import runtime_installed


class SetupActionTests(unittest.TestCase):
    def test_runtime_distinguishes_marketplace_launcher_from_compiled_source(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            launcher = root / "launcher"
            launcher.write_text("#!/bin/sh\n")
            compiled = root / "renderer"
            compiled.write_bytes(b"\x7fELFbinary")
            missing_system = root / "missing-system-runtime"
            self.assertFalse(runtime_installed(launcher, missing_system))
            self.assertTrue(runtime_installed(compiled, missing_system))

    def test_packaged_integrations_use_package_setup(self):
        runner = Mock()
        setup_actions.run("controls", which=lambda _: "/usr/bin/omarchy-xr-setup", runner=runner)
        runner.assert_called_once_with(["/usr/bin/omarchy-xr-setup", "--controls"], check=True)

    def test_source_controls_reload_hyprland(self):
        runner = Mock()
        setup_actions.run("controls", which=lambda _: None, runner=runner)
        self.assertEqual(runner.call_args_list[-2].args[0], ["hyprctl", "reload"])
        self.assertEqual(runner.call_args_list[-1].args[0], ["hyprctl", "configerrors"])

    def test_helper_uses_the_narrow_source_installer(self):
        runner = Mock()
        setup_actions.run("helper", which=lambda _: None, runner=runner)
        command = runner.call_args.args[0]
        self.assertEqual(command[:3], ["sudo", "/usr/bin/python3", "-I"])
        self.assertTrue(command[3].endswith("scripts/install-helper.py"))


if __name__ == "__main__":
    unittest.main()
