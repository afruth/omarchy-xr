import QtQuick
import qs.Ui as Ui
import "CurvatureAngles.js" as Angles

Ui.NumberField {
    id: root
    property real amount: 0
    property real maximumDegrees: 0
    property real editingMaximum: maximumDegrees
    readonly property real currentMaximum: field.activeFocus ? editingMaximum : maximumDegrees
    signal amountEdited(real amount)
    onMaximumDegreesChanged: if (!field.activeFocus) editingMaximum=maximumDegrees
    from: 0
    to: Math.max(0,Math.ceil(currentMaximum))
    stepSize: 1
    field.live: false
    // A telemetry update must not replace partially typed text or change its
    // conversion scale. Adopt new geometry after the user finishes editing.
    Binding {
        target: root.field
        property: "value"
        value: Math.min(root.field.to, Math.round(root.amount*root.maximumDegrees/100))
        when: !root.field.activeFocus
        restoreMode: Binding.RestoreNone
    }
    Connections {
        target: root.field
        function onActiveFocusChanged() {
            if (root.field.activeFocus) root.editingMaximum=root.maximumDegrees;
        }
    }
    onModified: function(value) { amountEdited(Angles.amount(value,editingMaximum)); }
}
