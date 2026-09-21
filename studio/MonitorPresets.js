.pragma library

var presets = [
    {value:"hd", label:"HD · 1280 × 720", width:1280, height:720},
    {value:"fhd", label:"Full HD · 1920 × 1080", width:1920, height:1080},
    {value:"qhd", label:"QHD · 2560 × 1440", width:2560, height:1440},
    {value:"uwqhd", label:"Ultrawide · 3440 × 1440", width:3440, height:1440},
    {value:"4k", label:"4K UHD · 3840 × 2160", width:3840, height:2160},
    {value:"square", label:"Square HD · 1920 × 1920", width:1920, height:1920},
    {value:"5k", label:"Apple 5K · 5120 × 2880", width:5120, height:2880}
];
function supported(width, height, limits) {
    return width >= 320 && height >= 200 && width <= limits.maxWidth && height <= limits.maxHeight;
}
function options(limits) {
    return [{value:"custom", label:"Custom"}].concat(presets.filter(function(p) {
        return supported(p.width,p.height,limits);
    }));
}
function match(monitor) {
    for (var i=0;i<presets.length;i++)
        if (presets[i].width === monitor.width && presets[i].height === monitor.height) return presets[i].value;
    return "custom";
}
function find(value) {
    for (var i=0;i<presets.length;i++) if (presets[i].value === value) return presets[i];
    return null;
}
