.pragma library

// Work in desktop pixels. Nearby sibling edges win over the 20px fallback
// grid; adjacency includes the configured gutter, including odd-sized gutters.
function place(monitors, index, x, y, spacing, scale) {
    var moving = monitors[index];
    var gap = Math.max(1, spacing);
    var reach = Math.min(160, 10 / Math.max(.001, scale));
    function options(axis, raw, size) {
        var candidates = [];
        var seen = {};
        monitors.forEach(function(peer, i) {
            if (i === index) return;
            var start = peer[axis], extent = peer[axis === "x" ? "width" : "height"];
            [start, start + extent - size, start + extent + gap, start - size - gap].forEach(function(pos) {
                var distance = Math.abs(pos - raw);
                if (distance <= reach && !seen[String(pos)]) {
                    seen[String(pos)] = true;
                    candidates.push({pos:pos, distance:distance, peer:i});
                }
            });
        });
        candidates.sort(function(a,b) {return a.distance-b.distance;});
        candidates = candidates.slice(0,8);
        candidates.push({pos:Math.round(raw/20)*20, distance:reach*3, peer:-1});
        return candidates;
    }
    var xs = options("x", x, moving.width), ys = options("y", y, moving.height);
    var best = null, bestScore = Infinity;
    xs.forEach(function(a) { ys.forEach(function(b) {
        var score = a.distance + b.distance;
        if (score >= bestScore) return;
        var collision = monitors.some(function(peer, i) {
            return i !== index && a.pos < peer.x+peer.width+gap && peer.x < a.pos+moving.width+gap
                && b.pos < peer.y+peer.height+gap && peer.y < b.pos+moving.height+gap;
        });
        if (!collision) { bestScore=score; best={x:a.pos,y:b.pos,snapX:a.peer>=0,snapY:b.peer>=0,blocked:false}; }
    }); });
    // Do not allow dragging to create an overlap or erase the gutter.
    return best || {x:moving.x,y:moving.y,snapX:false,snapY:false,blocked:true};
}

// Preserve each pair's original left/right or above/below relationship.
// Solving these acyclic constraints in coordinate order propagates growth
// through neighbours without depending on monitor IDs or array order.
function resize(monitors, index, width, height, spacing) {
    var result = JSON.parse(JSON.stringify(monitors));
    var gap = Math.max(1, spacing);
    result[index].width = Math.round(width);
    result[index].height = Math.round(height);
    var edges = {x:[], y:[]};
    for (var i=0;i<monitors.length;i++) {
        for (var j=i+1;j<monitors.length;j++) {
            var a=monitors[i], b=monitors[j];
            var left = a.x <= b.x ? i : j, right = left === i ? j : i;
            var top = a.y <= b.y ? i : j, bottom = top === i ? j : i;
            var dx = monitors[right].x - monitors[left].x - monitors[left].width;
            var dy = monitors[bottom].y - monitors[top].y - monitors[top].height;
            // Keep both relationships for diagonal neighbours so rows and
            // columns move together and shrinking reverses an earlier growth.
            if (dx >= gap) edges.x.push([left,right]);
            if (dy >= gap) edges.y.push([top,bottom]);
            if (dx < gap && dy < gap) {
                if (dx >= dy) edges.x.push([left,right]);
                else edges.y.push([top,bottom]);
            }
        }
    }
    ["x","y"].forEach(function(axis) {
        var size = axis === "x" ? "width" : "height";
        edges[axis].sort(function(a,b) {
            return monitors[a[0]][axis]-monitors[b[0]][axis] || a[0]-b[0];
        });
        // Remove indirect links: A → B → C must pull C through B, not
        // leave C pinned by its old distance from A when A shrinks.
        var incoming = monitors.map(function() { return []; });
        edges[axis].forEach(function(edge) { incoming[edge[1]].push(edge[0]); });
        var order = monitors.map(function(m,i) { return i; });
        order.sort(function(a,b) { return monitors[a][axis]-monitors[b][axis] || a-b; });
        order.forEach(function(target) {
            var parents = incoming[target].slice().sort(function(a,b) {
                return monitors[b][axis]-monitors[a][axis] || b-a;
            });
            var covered = {}, direct = [];
            parents.forEach(function(parent) {
                if (covered[parent]) return;
                direct.push(parent);
                var pending = [parent];
                while (pending.length) {
                    var ancestor = pending.pop();
                    if (covered[ancestor]) continue;
                    covered[ancestor] = true;
                    incoming[ancestor].forEach(function(value) { pending.push(value); });
                }
            });
            if (!direct.length) return;
            var delta = 0, minimum = -Infinity;
            direct.forEach(function(parent) {
                var shift = result[parent][axis]+result[parent][size]-monitors[parent][axis]-monitors[parent][size];
                if (shift > 0) delta = Math.max(delta,shift);
                else if (delta <= 0) delta = Math.min(delta,shift);
                minimum = Math.max(minimum,result[parent][axis]+result[parent][size]+gap);
            });
            result[target][axis] = Math.max(minimum,monitors[target][axis]+delta);
        });
    });
    if (result.some(function(m) { return Math.abs(m.x)>100000 || Math.abs(m.y)>100000; }))
        throw new Error("Resize exceeds the layout position limit.");
    return result;
}
