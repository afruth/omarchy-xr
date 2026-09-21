import QtQuick
import qs.Ui

BarWidget {
    id: root
    moduleName: "afruth.omarchy-xr"

    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight

    BarIconButton {
        id: button
        anchors.fill: parent
        bar: root.bar
        text: "󰚺"
        tooltipText: "XR Monitor Studio"
        onPressed: function (buttonCode) {
            if (buttonCode === Qt.LeftButton && root.bar)
                root.bar.run("omarchy-shell shell summon afruth.omarchy-xr '{}'");
        }
    }
}
