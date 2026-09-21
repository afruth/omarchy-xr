pragma Singleton
import QtQuick
QtObject {
    property FontTokens font: FontTokens {}
    property int cornerRadius: 8
    function space(value) { return value }
}
