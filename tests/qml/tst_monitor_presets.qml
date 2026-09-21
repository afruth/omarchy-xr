import QtQuick
import QtTest
import "../../studio/MonitorPresets.js" as Presets
TestCase {
    name: "MonitorPresets"
    function test_limits() {
        var limits = {maxWidth:2560,maxHeight:1920};
        var options = Presets.options(limits);
        verify(options.some(function(p){return p.value === "fhd";}));
        verify(!options.some(function(p){return p.value === "4k";}));
        verify(!Presets.supported(1080,3840,limits));
        verify(!Presets.supported(200,320,limits));
    }
    function test_matching() {
        compare(Presets.match({width:1920,height:1080}),"fhd");
        compare(Presets.match({width:1080,height:1920}),"custom");
        compare(Presets.find("5k").width,5120);
        compare(Presets.find("custom"),null);
    }
}
