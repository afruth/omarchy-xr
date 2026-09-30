function copy(value) { return JSON.parse(JSON.stringify(value)); }

function summary(before, after) {
    if (!before) return "Layout ready to apply";
    var old = before.monitors || [], next = after.monitors || [], parts = [];
    var added = next.filter(function(m) { return !old.some(function(p) { return p.id === m.id; }); }).length;
    var removed = old.filter(function(m) { return !next.some(function(p) { return p.id === m.id; }); }).length;
    if (added) parts.push("Add " + added + " monitor" + (added === 1 ? "" : "s"));
    if (removed) parts.push("Remove " + removed + " monitor" + (removed === 1 ? "" : "s"));
    [{verb:"Resize",keys:["width","height"]},{verb:"Move",keys:["x","y"]},
        {verb:"Change text scale on",keys:["scale"]},{verb:"Change bend on",keys:["curvature"]},
        {verb:"Change brightness on",keys:["brightness"]}].forEach(function(change) {
        var defaults={scale:1,curvature:0,brightness:100};
        var count=next.filter(function(m) {
            var p=old.find(function(previous) { return previous.id===m.id; });
            return p && change.keys.some(function(k) {
                return (p[k]===undefined ? defaults[k] : p[k]) !== (m[k]===undefined ? defaults[k] : m[k]);
            });
        }).length;
        if(count) parts.push(change.verb+" "+count+" monitor"+(count===1 ? "" : "s"));
    });
    var defaults={fps:60,curvature:0,workspaceDegrees:-1,workspaceFollow:false,spacing:24};
    if (Object.keys(defaults).some(function(k) {
        return (before[k]===undefined ? defaults[k] : before[k]) !== (after[k]===undefined ? defaults[k] : after[k]);
    })) parts.push("Change workspace settings");
    return parts.length ? parts.join(" · ") : "No pending changes";
}
