"""Exercise the installed QML transport with real Quickshell FileViews."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

from test_notification_install import ROOT, installer


@unittest.skipUnless(shutil.which("quickshell"), "requires Quickshell")
class NotificationTransportTests(unittest.TestCase):
    def test_dismissal_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runtime = root / "runtime"
            runtime.mkdir(mode=0o700)
            commons = root / "Commons"
            commons.mkdir()
            (commons / "qmldir").write_text("singleton Color 1.0 Color.qml\n")
            (commons / "Color.qml").write_text('''pragma Singleton
import QtQuick
QtObject {
    property var notifications: ({background:"#111111",text:"#eeeeee",countdown:"#888888"})
    property string urgent: "#ff0000"
}
''')
            native = root / "native"
            native.mkdir()
            (native / "Service.qml").write_text('''import QtQuick
import Quickshell.Io
Item {
    id: service
    property alias popupModel: model
    ListModel { id: model }
    function dismissPopup(index) { model.remove(index); }
    Component.onCompleted: {
        for (var i=1;i<=3;i++) model.append({originalId:i,timestamp:123,summary:"Test " + i});
    }
    IpcHandler {
        target: "fixture"
        function dismiss(): void { service.dismissPopup(0); }
    }
}
''')
            target = installer.install_files(ROOT / "notifications", root / "config", native)
            shell = root / "shell.qml"
            shell.write_text('import Quickshell\nimport ' + json.dumps(target.as_uri())
                             + ' as Plugin\nShellRoot { Plugin.Service {} }\n')
            env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QPA_PLATFORMTHEME="basic",
                       XDG_RUNTIME_DIR=str(runtime), OMARCHY_XR_RUNTIME=str(runtime / "xr"))
            with (root / "log").open("w+") as log:
                process = subprocess.Popen(["quickshell", "-n", "-p", str(shell)], env=env,
                                           stdout=log, stderr=subprocess.STDOUT)
                try:
                    feed = runtime / "xr/notifications.json"
                    packet = self.wait_count(feed, 3, process, log)
                    # Two atomic replacements exercise rearming the watcher as well as first use.
                    for count in (2, 1):
                        temp = runtime / "xr/request.tmp"
                        temp.write_text(json.dumps(dict(generation=packet["generation"],
                                                       key=packet["entries"][0]["key"],
                                                       time=int(time.time() * 1000) - 10)))
                        temp.replace(runtime / "xr/notification-dismiss.json")
                        packet = self.wait_count(feed, count, process, log)
                    subprocess.run(["quickshell", "ipc", "-p", str(shell), "call", "fixture", "dismiss"],
                                   env=env, check=True, capture_output=True, timeout=5)
                    self.wait_count(feed, 0, process, log)
                finally:
                    process.terminate()
                    process.wait(timeout=5)

    def wait_count(self, feed, count, process, log):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline and process.poll() is None:
            try:
                packet = json.loads(feed.read_text())
                if packet["count"] == count:
                    return packet
            except (FileNotFoundError, json.JSONDecodeError):
                pass
            time.sleep(.02)
        log.seek(0)
        self.fail(f"Expected {count} notifications; feed={feed.read_text() if feed.exists() else 'missing'}\n"
                  + log.read())
