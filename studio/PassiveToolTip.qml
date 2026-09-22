import QtQuick
import QtQuick.Controls

// Paint in the window without a Popup's input layer. Even when help overlaps a
// neighbouring control, all pointer events must reach that control directly.
Control {
    id: tip
    required property Item boundaryItem
    required property Item target
    property string text: ""
    property bool active: false
    property int delay: 650
    property int timeout: 5000
    property bool revealed: false
    readonly property bool requested: active && target && target.visible && text.length > 0
    readonly property real edge: 8
    readonly property real gap: 8
    parent: boundaryItem
    z: 10000
    enabled: false
    focus: false
    visible: requested && revealed
    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding

    onRequestedChanged: {
        revealed = false;
        delayTimer.stop();
        expiryTimer.stop();
        if (requested) delayTimer.start();
    }
    Timer {
        id: delayTimer
        interval: tip.delay
        onTriggered: {
            tip.revealed = true;
            if (tip.timeout > 0) expiryTimer.start();
        }
    }
    Timer { id: expiryTimer; interval: tip.timeout; onTriggered: tip.revealed = false }
    x: {
        if (!target || !visible || !boundaryItem) return 0;
        var origin = target.mapToItem(boundaryItem, 0, 0);
        var preferred = origin.x + (target.width - width) / 2;
        var limit = Math.max(edge, boundaryItem.width - width - edge);
        return Math.min(Math.max(preferred, edge), limit);
    }
    y: {
        if (!target || !visible || !boundaryItem) return 0;
        var origin = target.mapToItem(boundaryItem, 0, 0);
        var above = origin.y - height - gap;
        var below = origin.y + target.height + gap;
        // Never clamp back onto the source control in a cramped window.
        return above >= edge ? above : below;
    }
    contentItem: Text { text: tip.text; wrapMode: Text.WordWrap; textFormat: Text.PlainText }
}
