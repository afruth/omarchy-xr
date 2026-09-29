-- Omarchy XR: live controls, active only while the renderer heartbeat is fresh.
-- No processes are spawned during a gesture. Atomic cumulative samples are read
-- by the renderer each frame. Horizontal gestures remain available to Hyprland.
local state = os.getenv("XDG_STATE_HOME") or (os.getenv("HOME") .. "/.local/state")
local runtime = os.getenv("XDG_RUNTIME_DIR")
local runtime_root = (runtime and runtime ~= "" and (runtime .. "/omarchy-xr")) or (state .. "/omarchy-xr")
local path = runtime_root .. "/pose.sock.controls"
local CONTROLS_VERSION = 8
-- Window canvas (v6): fixed names shared with the backend and the renderer (§3.3, §5.5).
local CANVAS_WS, PARK_WS = "omxr-canvas", "omxr-park"
-- Sliver strip (§3.3, M5): 8 px inside the output's right edge, stacked 24 px apart; the stack stops
-- SLIVER_FLOOR px above the bottom edge, so surplus slivers overlap there but stay on the output.
local SLIVER_PX, SLIVER_STEP, SLIVER_FLOOR = 8, 24, 64
local CANVAS_MONITOR_PATTERN = "^OMXR%-%x%x%x%x%x%x%x%x%-canvas$"
local EXCLUDED_CLASSES = {["omarchy-xr-spectator"]=true, ["omarchy-xr-search"]=true}
-- Renderer action codes (.controls "fit" field). 8+ carry a token; the renderer gives each its meaning in the
-- current scene (docs/xr-controls-plan.md §5.1), so the XR key layer publishes the same code in both modes.
local CANVAS_MODES = {overview=8, search=9, fill=10, mru_next=11, mru_prev=12, arrange=13, neighbour=14, nudge=15, pin=16, help=17, confirm=18, scroll=19,
    dismiss=20, notify_next=21, cycle=22, grab=26, undo=27, redo=28, stats=29, level_in=30, level_out=31, move=32}
local setHoverTimer, updateCanvas, releasePointer, installLayer, parseLayerSettings
local layerModifier,layerKeys
local fingers=3
omarchy_xr_controls = omarchy_xr_controls or {version=CONTROLS_VERSION}
-- A keybind handle is a weak reference: unbind, a reload, or remove() of any bind on the same chord
-- (Hyprland erases them all) expires it, and remove()/set_enabled() on an expired handle segfault
-- Hyprland 0.56 (pcall cannot catch that). Its __tostring is the one safe probe.
local function bindingLive(binding) return binding~=nil and tostring(binding)~="HL.Keybind(expired)" end
local function removeBinding(binding) if bindingLive(binding) then binding:remove() end end
local function enableBinding(binding,enabled) if bindingLive(binding) then binding:set_enabled(enabled) end end
local function retireHoverTimer()
    local timer=omarchy_xr_controls.hover_timer
    if not timer then return end
    timer:set_enabled(false)
    omarchy_xr_controls.hover_timer=nil
end
local function retireWorkspaceEvents()
    for _,subscription in ipairs(omarchy_xr_controls.workspace_events or {}) do subscription:remove() end
end
-- The SUPER+left-drag binds (M7), the drag's plain release bind (dragRelease) and the `.drag` id and
-- sequence, which stay monotonic across a reload.
local function dragState(saved)
    saved.dragBinds,saved.dragId,saved.dragSeq=saved.dragBinds or {},saved.dragId or 0,saved.dragSeq or 0
end
-- Canvas state that must survive a config reload: journaled origins, the staged address,
-- event subscriptions, the SUPER+F takeover, the canvas key set, the mailbox sequence numbers and
-- the sliver set (slivers: moved by Lua; tiersWanted: the renderer's last `.tiers` set) and the drag state.
local function canvasState()
    omarchy_xr_canvas = omarchy_xr_canvas or {origin={}, staged=nil, events={}, fill=nil, windowsSeq=0, cursorSeq=0}
    local saved=omarchy_xr_canvas
    saved.keys,saved.takeovers,saved.takeover=saved.keys or {},saved.takeovers or {},saved.takeover or false
    saved.fillSeq=saved.fillSeq or 0
    saved.slivers,saved.tiersWanted,saved.tiersSeq=saved.slivers or {},saved.tiersWanted or {},saved.tiersSeq or 0
    dragState(saved)
    return saved
end
local canvas = canvasState()
local function retireCanvasEvents()
    for _,subscription in ipairs(canvas.events or {}) do subscription:remove() end
    canvas.events={}
end
local function retireFill()
    local binding=canvas.fill
    if not binding then return false end
    removeBinding(binding)
    canvas.fill=nil
    return true
end
-- Omarchy binds SUPER+F with o.bind, which discards the handle, and there is no hl.get_binds:
-- its default dispatch is the only thing that can be restored (a custom SUPER+F returns on reload).
local function restoreFullscreen()
    retireFill()
    hl.unbind("SUPER + F")
    hl.bind("SUPER + F", hl.dsp.window.fullscreen({mode="fullscreen"}), {description="Full screen"})
end
-- SUPER+left-drag is Omarchy's window.drag (tiling.lua), bound with o.bind as well: the canvas replaces it
-- with a press and a release bind and gives the default back on exit.
local DRAG_CHORD="SUPER + mouse:272"
local function retireDragRelease()
    local binding=canvas.dragRelease
    removeBinding(binding)
    canvas.dragRelease=nil
end
local function retireDragBinds()
    for _,binding in ipairs(canvas.dragBinds) do removeBinding(binding) end
    canvas.dragBinds={}
    retireDragRelease()
end
local function restoreDrag()
    retireDragBinds()
    hl.unbind(DRAG_CHORD)
    hl.bind(DRAG_CHORD,hl.dsp.window.drag(),{mouse=true,description="Move window"})
end
-- Controls v6 bound a canvas key set and optional takeovers of Omarchy chords (SUPER+F, SUPER+TAB, ALT+TAB,
-- SUPER+arrows, SUPER+wheel, SUPER+left-drag). v7 binds only the XR key layer; the retire helpers below
-- still give a v6 takeover back when this chunk loads over it.
-- Our binds go; a taken-over chord gets every Omarchy default back (ALT+TAB has two).
local function retireKeys(list)
    for _,key in ipairs(list or {}) do
        removeBinding(key.binding)
        if key.restore then
            hl.unbind(key.chord)
            for _,original in ipairs(key.restore) do hl.bind(key.chord,original.dsp(),{description=original.desc}) end
        end
    end
    return {}
end
local function retireTakeovers() canvas.takeovers=retireKeys(canvas.takeovers);canvas.takeover=false end
local function retireCanvasKeys() canvas.keys=retireKeys(canvas.keys);retireTakeovers() end
retireHoverTimer()
retireWorkspaceEvents()
local function retireCanvas()
    retireCanvasEvents()
    if canvas.fill then restoreFullscreen() end
    if #canvas.dragBinds>0 then restoreDrag() end
    retireDragRelease()
    retireCanvasKeys()
end
retireCanvas()
local session, serial, total, fit_serial, fit_mode = nil, 0, 0, 0, 0
local workspacePending, gazeDispatch = false, false
local focusWaiting = nil
local active, canvasMode = false, false
local lastTap, tapBlockedUntil=nil,0
local function bootSeconds()
    local file=io.open("/proc/uptime","r")
    if not file then return os.time() end
    local seconds=file:read("*n");file:close()
    return seconds or os.time()
end
-- Every mailbox beside the pose socket is replaced atomically.
local function writeMailbox(suffix,text)
    local file=io.open(path..suffix..".tmp","w")
    if not file then return end
    file:write(text);file:close();os.rename(path..suffix..".tmp",path..suffix)
end
local focusSerial=0
local function writeFocus(name)
    focusSerial=focusSerial+1
    writeMailbox(".focus",string.format("v1 %s %d %s %d\n",session,focusSerial,name,math.floor(bootSeconds())))
end
local function fresh(stamp)
    if stamp==nil then return false end
    local now=stamp>1000000000 and os.time() or bootSeconds()
    return now-stamp>=0 and now-stamp<=2
end
local function numbers(line)
    if not line then return {} end
    line=line:gsub("^v[23]%s+","")
    local fields={}
    for value in line:gmatch("%S+") do fields[#fields+1]=tonumber(value) end
    return fields
end
local function tapTime()
    -- Monotonic wall time; os.clock measures CPU time, os.time is too coarse.
    local file=io.open("/proc/uptime","r")
    if not file then return nil end
    local seconds=file:read("*n");file:close()
    return seconds and seconds*1000 or nil
end
local function cancelTap()
    lastTap=nil
    tapBlockedUntil=(tapTime() or 0)+250
end
local function publish(delta, mode, target)
    if not active then return end
    if mode and mode>=8 then target=target or "-" end
    total = total + (delta or 0)
    serial = serial + 1
    if mode then fit_serial = serial; fit_mode = mode
    elseif fit_mode>=6 then fit_mode=0 end
    local file = io.open(path .. ".tmp", "w")
    if not file then return end
    if target then
        file:write(string.format("v3 %s %d %.9f %d %d %s %d\n", session, serial, total, fit_serial, fit_mode, target, math.floor(bootSeconds())))
    else
        file:write(string.format("v2 %s %d %.9f %d %d\n", session, serial, total, fit_serial, fit_mode))
    end
    file:close()
    os.rename(path .. ".tmp", path)
end
local function notificationTarget()
    local file=io.open(path..".notification","r")
    if not file then return nil end
    local line=file:read("*l") or "";file:close()
    local owner,target,stamp=line:match("^v1 (%d+) ([%da-f]+) (%d+)%s*$")
    if owner==session and fresh(tonumber(stamp)) then return target end
end
-- Buffer a short fast candidate so a flick fits without first jumping in zoom.
-- Slow motion commits early; sustained motion becomes continuous at 220 ms.
local swipe
local function commitZoom()
    if swipe and not swipe.target and swipe.zoom and swipe.pending~=0 then publish(-swipe.pending*.004);swipe.pending=0 end
end
local function finishSwipe(e)
    local elapsed=math.max(1,(e.time_ms or swipe.started)-swipe.started)
    local flick=not swipe.live and elapsed<=220 and math.abs(swipe.net)>=35
        and math.abs(swipe.net)/elapsed>=.35 and math.abs(swipe.net)>=swipe.travel*.8
    if swipe.target then
        if flick and notificationTarget()==swipe.target then publish(0,swipe.net<0 and 6 or 7,swipe.target) end
    elseif swipe.zoom then
        if flick then publish(0,swipe.net<0 and 2 or 1) else commitZoom() end
    end
end
local function makeGesture(count)
return {
    start = function(e)
        cancelTap()
        swipe={started=e.time_ms or 0,net=0,travel=0,pending=0,live=false,
            target=count==3 and notificationTarget() or nil,zoom=count==fingers}
        local dy=e.delta and e.delta.y or 0
        swipe.net=dy;swipe.travel=math.abs(dy);swipe.pending=dy
    end,
    update = function(e)
        if not swipe or not active then return end
        local dy=e.delta.y;swipe.net=swipe.net+dy;swipe.travel=swipe.travel+math.abs(dy);swipe.pending=swipe.pending+dy
        local elapsed=math.max(0,(e.time_ms or swipe.started)-swipe.started)
        if elapsed>=220 or (elapsed>=70 and swipe.travel/math.max(elapsed,1)<.30) then swipe.live=true end
        if swipe.live then commitZoom() end
    end,
    finish = function(e)
        cancelTap()
        if not swipe then return end
        if active and not e.cancelled then finishSwipe(e) end
        swipe=nil
    end,
}
end
local gesture=makeGesture(3)
local fiveGesture=makeGesture(5)
local function registerVertical(enabled)
    hl.gesture({fingers=3,direction="vertical",action=enabled and gesture or "unset"})
    if fingers==5 then hl.gesture({fingers=5,direction="vertical",action=enabled and fiveGesture or "unset"}) end
end
-- Separate cumulative mailbox: pan and zoom never consume each other's deltas.
local panSerial,panId,panX,panY,panActive=0,0,0,0,false
local function publishPan()
    if not active then return end
    panSerial=panSerial+1
    writeMailbox(".pan",string.format("v2 %s %d %d %.9f %.9f %d %d\n",session,panSerial,panId,panX,panY,panActive and 1 or 0,math.floor(bootSeconds())))
end
local panGesture={
    start=function(e)
        cancelTap()
        panId=panId+1;panActive=true;panX=0;panY=0
        if e.delta then panX=e.delta.x or 0;panY=e.delta.y or 0 end
        publishPan()
    end,
    update=function(e)
        if not panActive then return end
        panX=panX+(e.delta.x or 0);panY=panY+(e.delta.y or 0);publishPan()
    end,
    finish=function(e) cancelTap();panActive=false;publishPan() end,
}
local function recordTap(now)
    if lastTap and now-lastTap>=40 and now-lastTap<=400 then
        lastTap=nil;publish(0,3);releasePointer();return
    end
    if not lastTap or now-lastTap>400 or now<lastTap then lastTap=now end
end
-- In canvas mode a single tap confirms (focuses the gazed window) once no second tap followed in 400 ms.
local function settleTap(now)
    if canvasMode and lastTap and now and now-lastTap>400 then lastTap=nil;publish(0,CANVAS_MODES.confirm) end
end
local taps={}
local ok,touchpads=pcall(require,"hypr.xr-touchpads")
if ok and #touchpads>0 then
    local map=hl.get_config("input.touchpad.tap_button_map")
    local key=map=="lmr" and "mouse:273" or "mouse:274"
    taps[1]=hl.bind(key,function()
        if not active or swipe or panActive then lastTap=nil;return end
        local now=tapTime()
        if not now or now<tapBlockedUntil then lastTap=nil;return end
        recordTap(now)
    end,
        {description="XR: three-finger double tap recenter",device={inclusive=true,list=touchpads}})
    enableBinding(taps[1],false)
end
local settingsText
-- Gesture fingers only; the key layer is (re)bound by installLayer.
local function setFingers(count)
    if count==fingers then return end
    if active then registerVertical(false) end
    swipe=nil;cancelTap();fingers=count
    if active then registerVertical(true) end
    if omarchy_xr_controls then omarchy_xr_controls.fingers=fingers end
end
local function readSettings()
    local file=io.open(state.."/omarchy-xr/controls-settings.tsv","r")
    if not file then return end
    local text=file:read("*a");file:close()
    if text==settingsText then return end
    settingsText=text
    local modifier,count,keys=parseLayerSettings(text)
    if not modifier then return end
    setFingers(count)
    layerModifier,layerKeys=modifier,keys
    installLayer()
end
local function adoptSession(owner)
    session = tostring(owner); serial = 0; total = 0; fit_serial = 0; fit_mode = 0
    workspacePending=false;focusWaiting=nil;focusSerial=0
    if active then installLayer() end -- a restarted renderer gets .keys under its own session
    local focus=io.open(path..".focus","r")
    if focus then
        local savedOwner,savedSerial=(focus:read("*l") or ""):match("^v1 (%d+) (%d+) ")
        focus:close()
        if savedOwner==session then focusSerial=tonumber(savedSerial) or 0 end
    end
    -- Preserve accumulated input across a Hyprland config reload.
    local previous = io.open(path, "r")
    if not previous then return end
    local saved = numbers(previous:read("*l")); previous:close()
    if saved[1] == owner and saved[5] then
        serial, total, fit_serial, fit_mode = saved[2], saved[3], saved[4], saved[5]
    end
end
local function restorePan()
    local previous=io.open(path..".pan","r")
    if not previous then return end
    local saved=numbers(previous:read("*l")); previous:close()
    if saved[1] and tostring(saved[1])==session and saved[5] then panSerial,panId,panX,panY=saved[2],saved[3],saved[4],saved[5] end
end
local function applyLive(live)
    active = live
    if not live then workspacePending=false;focusWaiting=nil end
    swipe=nil;panActive=false;cancelTap()
    if active then publishPan() end
    hl.gesture({fingers=4,direction="swipe",action=active and panGesture or "unset"})
    for _,binding in ipairs(taps) do enableBinding(binding,active) end
    registerVertical(active)
    installLayer()
end
local function writeControlsVersion()
    local versionFile=io.open(runtime_root.."/controls.version","w")
    if versionFile then versionFile:write(CONTROLS_VERSION.."\n"); versionFile:close() end
end
local function refresh()
    readSettings()
    local file = io.open(path .. ".active", "r")
    local owner, stamp
    if file then owner, stamp = file:read("*n", "*n"); file:close() end
    local live = fresh(stamp)
    if live and tostring(owner) ~= session then adoptSession(owner) end
    if live and not active then restorePan() end
    if live ~= active then applyLive(live) end
    if active and panActive then publishPan() end
    writeControlsVersion()
    updateCanvas()
    if setHoverTimer then setHoverTimer(active) end
end
local timer = hl.timer(refresh, {timeout=250, type="repeat"})
-- Keep the timer alive and expose the exact callbacks for integration checks.
omarchy_xr_controls = {version=CONTROLS_VERSION, tap_bindings=taps, recenter=function() publish(0,3) end, fingers=fingers, timer=timer, refresh=refresh, gesture=gesture, pan=panGesture, fit_all=function() publish(0,1) end, fit_target=function() publish(0,CANVAS_MODES.confirm) end}

-- Halo target changes select the target's existing workspace once. Coordinate
-- changes within that monitor never steer the pointer or repeat focus dispatches.
local gazeOwner, gazeSerial, gazeTarget
local hoverName   -- the monitor the halo is on, for the pane publisher
local pointerSerialSeen
-- Observe existing workspace shortcuts, including a workspace already visible on
-- another monitor. Resolve after dispatch finishes, coalescing both event types.
local function workspaceChanged()
    if active and not gazeDispatch then workspacePending=true end
end
omarchy_xr_controls.workspace_events={
    hl.on("workspace.active",workspaceChanged),
    hl.on("monitor.focused",workspaceChanged),
}
-- Window canvas (§3): the renderer's `.mode` heartbeat switches names from monitors to window
-- addresses. The staged window floats at the canvas output's origin on CANVAS_WS; every other
-- canvas window is hidden on PARK_WS. Events only mark the list dirty; the timers write it.
local MAX_ROWS=512
local PROPS={{"border_size","0"},{"rounding","0"},{"no_anim","1"},{"no_shadow","1"},{"no_blur","1"},{"no_dim","1"}}
local windowsDirty,lastWindowsWrite,canvasPolicy,canvasExcludes=false,-math.huge,"all",{}
local snapshot,focusPending,focusAddress={},nil,nil
local overflowX,overflowY,cursorLine,cursorWritten=0,0,nil,-math.huge
local function pair(value)
    if type(value)~="table" then return 0,0 end
    return value.x or value[1] or 0,value.y or value[2] or 0
end
local function logicalSize(m)
    local scale=m.scale or 1
    return (m.width or 0)/scale,(m.height or 0)/scale
end
-- The stage band: the output's usable area (inside what layer surfaces reserve, such as Omarchy's bar, which
-- is drawn on every output and would otherwise cover the staged window's top in its region capture) minus
-- the sliver strip. Returns its origin and size in logical px.
local function stageBand(m)
    local width,height=logicalSize(m)
    local r=type(m.reserved)=="table" and m.reserved or {}
    local top,left=math.floor(r.top or 0),math.floor(r.left or 0)
    return m.x+left,m.y+top,width-left-math.floor(r.right or 0)-SLIVER_PX,height-top-math.floor(r.bottom or 0)
end
local function onMonitor(m,x,y)
    local w,h=logicalSize(m)
    return x>=m.x and x<m.x+w and y>=m.y and y<m.y+h
end
local function canvasMonitor()
    for _,m in ipairs(hl.get_monitors()) do
        if m.name and m.name:match(CANVAS_MONITOR_PATTERN) then return m end
    end
end
local function laptopMonitor()
    for _,m in ipairs(hl.get_monitors()) do
        if m.name and not m.name:match("^OMXR%-") then return m end
    end
end
local function isMember(w)
    local workspace=w.workspace
    return workspace~=nil and (workspace.name==CANVAS_WS or workspace.name==PARK_WS)
end
local function isRegular(w)
    if not w.mapped or (w.workspace and w.workspace.special) or EXCLUDED_CLASSES[w.class or ""] then return false end
    return not tostring(w.title or ""):find("^Omarchy XR") and w.xdg_tag==nil
end
-- canvas.tsv `exclude <class|pid>` rows (the user's exclusions and Studio itself): never adopted.
local function isExcluded(w)
    return canvasExcludes[tostring(w.class or "")] or canvasExcludes[tostring(math.floor(w.pid or -1))] or false
end
-- The renderer rejects tokens over 550 bytes; a cut never splits a UTF-8 character.
local function hexToken(text)
    text=tostring(text or "")
    if #text>550 then
        local cut=551
        for _=1,3 do if cut>1 and (text:byte(cut)&0xC0)==0x80 then cut=cut-1 end end
        text=text:sub(1,cut-1)
    end
    if text=="" then return "-" end
    return (text:gsub(".",function(c) return ("%02x"):format(c:byte()) end))
end
local function windowDispatch(kind,address,spec)
    spec=spec or {};spec.window="address:"..address
    hl.dispatch(hl.dsp.window[kind](spec))
end
local function listWindows()
    local fine,list=pcall(hl.get_windows)
    local out={}
    if not fine or type(list)~="table" then return out end
    for _,w in ipairs(list) do if w.address and isRegular(w) then out[#out+1]=w end end
    return out
end
-- Canvas windows float without decorations; the origin is recorded before the first change.
local function enforce(w)
    local address=w.address
    if not canvas.origin[address] then
        local width,height=pair(w.size)
        local x,y=pair(w.at)
        canvas.origin[address]={floating=w.floating,w=width,h=height,x=x,y=y}
        for _,prop in ipairs(PROPS) do windowDispatch("set_prop",address,{prop=prop[1],value=prop[2]}) end
    end
    if not w.floating then windowDispatch("float",address,{action="float"}) end
end
-- Every other window on the canvas workspace is a sliver (§3.3).
local function place(w)
    if w.address==canvas.staged then return "stage" end
    local name=w.workspace and w.workspace.name
    if name==CANVAS_WS then return "sliver" end
    return name==PARK_WS and "park" or "off"
end
local function flag(value) return value and 1 or 0 end
local function windowRow(w)
    local width,height=pair(w.size)
    local x,y=pair(w.at)
    local function size(value) return math.max(1,math.min(16384,math.floor(value+.5))) end
    return string.format("%s %s %s %d %d %d %d %d %s %d %d %d %d",w.address,hexToken(w.class),hexToken(w.title),
        size(width),size(height),math.floor(x+.5),math.floor(y+.5),math.floor(w.focus_history_id or 0),place(w),
        flag(w.floating),math.max(0,math.floor(w.pid or 0)),flag(w.xwayland),flag(isMember(w)))
end
local function remember(w)
    local x,y=pair(w.at)
    local width,height=pair(w.size)
    snapshot[w.address]={x=x,y=y,w=width,h=height}
end
-- One row per regular window, canvas members first when the list is over the cap.
local function windowRows(list)
    local members=0
    for _,w in ipairs(list) do if isMember(w) then members=members+1 end end
    local others,rows=MAX_ROWS-members,{}
    snapshot={}
    for _,w in ipairs(list) do
        local member=isMember(w)
        if member and canvasMode and not isExcluded(w) then enforce(w) end
        remember(w)
        if #rows<MAX_ROWS and (member or others>0) then
            rows[#rows+1]=windowRow(w)
            if not member then others=others-1 end
        end
    end
    return rows
end
local function publishWindows(now,mon)
    local rows=windowRows(listWindows())
    canvas.windowsSeq=canvas.windowsSeq+1
    local header=string.format("v1 %s %d %d %d %d %s",session,canvas.windowsSeq,math.floor(now),math.floor(mon.x),math.floor(mon.y),mon.name)
    rows[#rows+1]=""
    writeMailbox(".windows",header.."\n"..table.concat(rows,"\n"))
    windowsDirty=false;lastWindowsWrite=now
end
-- Sliver position: the index in the address-sorted `.tiers` set without the staged window.
local function addressBefore(a,b) return #a<#b or (#a==#b and a<b) end
local function sliverIndex(address,staged)
    local list={}
    for other in pairs(canvas.tiersWanted) do if other~=staged then list[#list+1]=other end end
    table.sort(list,addressBefore)
    for i,other in ipairs(list) do if other==address then return i-1 end end
    return #list
end
-- A demoted staged window is already on the canvas workspace (onCanvas).
local function makeSliver(mon,address,staged,onCanvas)
    local width,height=logicalSize(mon)
    local y=math.min(height-SLIVER_FLOOR,sliverIndex(address,staged)*SLIVER_STEP)
    if not onCanvas then windowDispatch("move",address,{workspace="name:"..CANVAS_WS,follow=false}) end
    windowDispatch("set_prop",address,{prop="no_follow_mouse",value="1"})
    windowDispatch("move",address,{x=math.floor(mon.x+width-SLIVER_PX),y=math.floor(mon.y+y)})
    canvas.slivers[address]=true;windowsDirty=true
end
local function dropSliver(address)
    if not canvas.slivers[address] then return end
    pcall(windowDispatch,"set_prop",address,{prop="no_follow_mouse",value="unset"})
    canvas.slivers[address]=nil
end
-- Quiet staging (XR's own choice, M7) leaves the keyboard focus where it is.
local function stageWindow(address,mon,quiet)
    local fine,w=pcall(hl.get_window,"address:"..address)
    if not fine or not w then return false end
    local previous=canvas.staged
    if previous and previous~=address then
        if canvas.tiersWanted[previous] then makeSliver(mon,previous,address,true)
        else windowDispatch("move",previous,{workspace="name:"..PARK_WS,follow=false}) end
    end
    dropSliver(address)
    enforce(w)
    -- The stage band is the output minus the sliver strip: the window sits at its origin, clamped to it.
    local bandX,bandY,maxW,maxH=stageBand(mon)
    local width,height=pair(w.size)
    width,height=math.floor(math.min(width,maxW)),math.floor(math.min(height,maxH))
    windowDispatch("move",address,{workspace="name:"..CANVAS_WS,follow=false})
    windowDispatch("resize",address,{x=width,y=height})
    windowDispatch("move",address,{x=bandX,y=bandY})
    windowDispatch("bring_to_top",address)
    if not quiet then hl.dispatch(hl.dsp.focus({window="address:"..address})) end
    canvas.staged=address;overflowX,overflowY=0,0;windowsDirty=true
    snapshot[address]={x=bandX,y=bandY,w=width,h=height}
    return true
end
-- Stage the window if needed, then land the pointer on the buffer pixel the renderer chose.
local function selectCanvasWindow(address,px,py)
    local mon=canvasMonitor()
    if not mon then return end
    if canvas.staged~=address then
        if not stageWindow(address,mon) then return end
    else
        hl.dispatch(hl.dsp.focus({window="address:"..address}))
    end
    local rect,scale=snapshot[address],mon.scale or 1
    local x,y=rect and rect.x or mon.x,rect and rect.y or mon.y
    hl.dispatch(hl.dsp.cursor.move({x=math.floor(x+px/scale+.5),y=math.floor(y+py/scale+.5)}))
end
-- Same pointer-serial rule as monitor mode: warp once per new serial, never replay the first sample.
local function canvasHover(mode,name,pointerSerial,px,py)
    hoverName=nil
    if not pointerSerial or pointerSerial==pointerSerialSeen then return end
    local first=pointerSerialSeen==nil
    pointerSerialSeen=pointerSerial
    if first or pointerSerial==0 or mode~="1" or not name:match("^0x%x+$") or not px or not py then return end
    selectCanvasWindow(name:lower(),px,py)
end
local function clampAxis(value,low,size)
    if value>=low and value<low+size then return value end
    return math.max(low+1,math.min(low+size-2,value))
end
local function bound(value) return math.max(-1e6,math.min(1e6,value)) end
-- The pointer stays inside the staged window; motion beyond it accumulates for the renderer's cursor.
local function confine(x,y)
    local rect=canvas.staged and snapshot[canvas.staged]
    if not rect then return x,y end
    local cx,cy=clampAxis(x,rect.x,rect.w),clampAxis(y,rect.y,rect.h)
    if cx==x and cy==y then return x,y end
    overflowX,overflowY=bound(overflowX+x-cx),bound(overflowY+y-cy)
    cx,cy=math.floor(cx),math.floor(cy)
    hl.dispatch(hl.dsp.cursor.move({x=cx,y=cy}))
    return cx,cy
end
-- Rewritten on change, and at least every 0.4 s: the renderer hides its cursor after 0.5 s.
local function publishCursor(mon,now)
    local fine,c=pcall(hl.get_cursor_pos)
    if not fine or not c then return end
    local x,y,inside=c.x,c.y,onMonitor(mon,c.x,c.y)
    if inside then x,y=confine(x,y) else overflowX,overflowY=0,0 end
    local line=string.format("%.1f %.1f %.1f %.1f",x,y,overflowX,overflowY)
    if line~=cursorLine or now-cursorWritten>=.4 then
        canvas.cursorSeq=canvas.cursorSeq+1
        writeMailbox(".cursor",string.format("v1 %s %d %s %d\n",session,canvas.cursorSeq,line,math.floor(now)))
        cursorLine,cursorWritten=line,now
    end
    if inside then return x,y end
end
-- SUPER+left-drag on the canvas (M7): `.drag` carries the pointer travel since the press, confinement
-- overflow included, in the `.pan` v2 codec; the renderer moves the staged window along the ring and
-- the real window stays at the stage origin. A release bind matches the modifiers held at release, so
-- letting go of SUPER first would miss it: a plain button release bind lives for the drag, and 3 s
-- without pointer travel end it too.
local DRAG_IDLE=3
local drag={active=false,startX=0,startY=0,dx=0,dy=0,moved=0}
local function publishDrag()
    if not active then return end
    canvas.dragSeq=canvas.dragSeq+1
    writeMailbox(".drag",string.format("v2 %s %d %d %.9f %.9f %d %d\n",session,canvas.dragSeq,canvas.dragId,drag.dx,drag.dy,
        drag.active and 1 or 0,math.floor(bootSeconds())))
end
local function endDrag()
    retireDragRelease()
    if not drag.active then return end
    drag.active=false;publishDrag()
end
local function beginDrag()
    if not (canvasMode and canvas.staged) then return end
    local fine,c=pcall(hl.get_cursor_pos)
    if not fine or not c then return end
    canvas.dragId=canvas.dragId+1;drag.active=true;drag.moved=bootSeconds()
    drag.startX,drag.startY,drag.dx,drag.dy=c.x+overflowX,c.y+overflowY,0,0
    retireDragRelease()
    canvas.dragRelease=hl.bind("mouse:272",endDrag,{release=true,description="XR: end window move (canvas)"})
    publishDrag()
end
local function updateDrag(x,y,now)
    if not drag.active then return end
    local dx,dy=drag.dx,drag.dy
    if x then dx,dy=x+overflowX-drag.startX,y+overflowY-drag.startY end
    if dx~=drag.dx or dy~=drag.dy then drag.dx,drag.dy,drag.moved=dx,dy,now;publishDrag()
    elseif now-drag.moved>=DRAG_IDLE then endDrag() end
end
-- SUPER+CTRL+arrows: 100 px steps, at least 100 px and at most the stage band.
local function resizeStaged(step)
    local mon=canvasMonitor()
    local rect=canvas.staged and snapshot[canvas.staged]
    if not (mon and rect) then return end
    local _,_,maxW,maxH=stageBand(mon)
    local width=math.floor(math.max(100,math.min(maxW,rect.w+(step.dw or 0))))
    local height=math.floor(math.max(100,math.min(maxH,rect.h+(step.dh or 0))))
    windowDispatch("resize",canvas.staged,{x=width,y=height})
    rect.w,rect.h=width,height;windowsDirty=true
end
local function flushFocus()
    if not focusPending then return end
    writeFocus(focusPending)
    focusPending=nil
end
-- The renderer owns the Fill size and the restore rule; Lua only resizes the staged window, and the
-- cursor confinement follows the new size.
local function applyFill(mon,address,width,height)
    local bandX,bandY,maxW,maxH=stageBand(mon)
    width,height=math.max(1,math.min(width,math.floor(maxW))),math.max(1,math.min(height,math.floor(maxH)))
    windowDispatch("resize",address,{x=width,y=height})
    local rect=snapshot[address] or {x=bandX,y=bandY}
    rect.w,rect.h=width,height;snapshot[address]=rect
    windowsDirty=true
end
-- `.fill`: v1 <owner> <seq> <address> <w> <h> <stamp>; each sequence number once per renderer.
local function readFill(mon)
    local file=io.open(path..".fill","r")
    if not file then return end
    local line=file:read("*l") or "";file:close()
    local owner,seqText,address,width,height,stamp=line:match("^v1 (%d+) (%d+) (0x%x+) (%d+) (%d+) (%d+)%s*$")
    if owner~=session then return end
    if canvas.fillOwner~=owner then canvas.fillOwner,canvas.fillSeq=owner,0 end
    local fillSeq=tonumber(seqText)
    if fillSeq<=canvas.fillSeq then return end
    canvas.fillSeq=fillSeq;address=address:lower()
    if fresh(tonumber(stamp)) and address==canvas.staged then applyFill(mon,address,tonumber(width),tonumber(height)) end
end
-- `.tiers`: v1 <owner> <seq> <stamp> [<address> sliver|park]..., the renderer's complete sliver set.
-- nil keeps the last set (foreign owner, malformed or already seen); a missing or stale file means no slivers
-- and forgets the seq, so the renderer's heartbeat (same seq, fresh stamp) brings the set back.
local function readTiers()
    local file=io.open(path..".tiers","r")
    if not file then canvas.tiersSeq=0;return {} end
    local line=file:read("*l") or "";file:close()
    local owner,seqText,stamp,rest=line:match("^v1 (%d+) (%d+) (%d+)(.*)$")
    if owner~=session then return nil end
    if canvas.tiersOwner~=owner then canvas.tiersOwner,canvas.tiersSeq=owner,0 end
    if not fresh(tonumber(stamp)) then canvas.tiersSeq=0;return {} end
    local tiersSeq=tonumber(seqText)
    if tiersSeq<=canvas.tiersSeq then return nil end
    canvas.tiersSeq=tiersSeq
    local set={}
    for address,kind in rest:gmatch("(0x%x+) (%a+)") do if kind=="sliver" then set[address:lower()]=true end end
    return set
end
local lastTiersApply=-math.huge
local function tiersChanges()
    local enter,leave={},{}
    for _,w in ipairs(listWindows()) do
        local address=w.address
        if isMember(w) and address~=canvas.staged and not isExcluded(w) then
            local wanted,current=canvas.tiersWanted[address],canvas.slivers[address]
            if wanted and not current then enter[#enter+1]=address elseif current and not wanted then leave[#leave+1]=address end
        end
    end
    return enter,leave
end
-- Park <-> sliver moves at most every 0.5 s; the staged window is never touched and stays on top.
local function applyTiers(mon,now)
    if now-lastTiersApply<.5 then return end
    local enter,leave=tiersChanges()
    if #enter==0 and #leave==0 then return end
    for _,address in ipairs(leave) do
        windowDispatch("move",address,{workspace="name:"..PARK_WS,follow=false})
        dropSliver(address)
    end
    for _,address in ipairs(enter) do makeSliver(mon,address,canvas.staged) end
    if canvas.staged then windowDispatch("bring_to_top",canvas.staged) end
    lastTiersApply=now;windowsDirty=true
end
local function syncTiers(mon,now)
    local wanted=readTiers()
    if wanted then canvas.tiersWanted=wanted end
    applyTiers(mon,now)
end
-- Always a staged window (M7): the most recently focused member, staged quietly at most every 0.5 s.
local ensureStaged
do
    local lastEnsure=-math.huge
    local function stagedLive()
        local fine,w=pcall(hl.get_window,"address:"..canvas.staged)
        return fine and w and isMember(w)
    end
    local function mruMember()
        local best
        for _,w in ipairs(listWindows()) do
            if isMember(w) and not isExcluded(w) and (not best or (w.focus_history_id or 0)<(best.focus_history_id or 0)) then best=w end
        end
        return best
    end
    ensureStaged=function(mon,now)
        if now-lastEnsure<.5 then return end
        lastEnsure=now
        if canvas.staged and stagedLive() then return end
        local best=mruMember()
        if best then stageWindow(best.address,mon,true) end
    end
end
-- A SUPER+right-drag resizes the real window, maybe from a corner: 0.5 s after its geometry settles
-- the staged window goes back to the stage origin, clamped to the band.
local reclampStage
do
    local stageSeen
    local function sameGeometry(seen,address,x,y,width,height)
        return seen and seen.address==address and seen.x==x and seen.y==y and seen.w==width and seen.h==height
    end
    local function onStage(mon,x,y,width,height)
        local bandX,bandY,maxW,maxH=stageBand(mon)
        return x==bandX and y==bandY and width<=maxW and height<=maxH
    end
    reclampStage=function(mon,now)
        local address=canvas.staged
        local fine,w=pcall(hl.get_window,"address:"..(address or ""))
        if not (address and fine and w) or drag.active then stageSeen=nil;return end
        local x,y=pair(w.at)
        local width,height=pair(w.size)
        if not sameGeometry(stageSeen,address,x,y,width,height) then stageSeen={address=address,x=x,y=y,w=width,h=height,at=now};return end
        if now-stageSeen.at<.5 or onStage(mon,x,y,width,height) then return end
        -- One try per geometry: a window that refuses the clamp (a minimum size) is not retried.
        stageSeen.at=math.huge
        local bandX,bandY,maxW,maxH=stageBand(mon)
        width,height=math.floor(math.min(width,maxW)),math.floor(math.min(height,maxH))
        windowDispatch("resize",address,{x=width,y=height})
        windowDispatch("move",address,{x=bandX,y=bandY})
        snapshot[address]={x=bandX,y=bandY,w=width,h=height};windowsDirty=true
    end
end
local function canvasTick()
    local mon=canvasMonitor()
    if not mon then return end
    local now=bootSeconds()
    readFill(mon)
    pcall(syncTiers,mon,now)
    ensureStaged(mon,now)
    reclampStage(mon,now)
    if windowsDirty and now-lastWindowsWrite>=.1 then publishWindows(now,mon) end
    local x,y=publishCursor(mon,now)
    updateDrag(x,y,now)
    flushFocus()
end
-- pointer_home and the 3-finger double tap hand the pointer back to the first non-XR monitor, from the
-- canvas output or any XR monitor.
releasePointer=function()
    local listed,monitors=pcall(hl.get_monitors)
    local fine,c=pcall(hl.get_cursor_pos)
    if not (listed and fine and c and type(monitors)=="table") then return end
    local laptop,onXr
    for _,m in ipairs(monitors) do
        if m.name and m.name:match("^OMXR%-") then onXr=onXr or onMonitor(m,c.x,c.y) elseif m.name and not laptop then laptop=m end
    end
    if not (laptop and onXr) then return end
    local w,h=logicalSize(laptop)
    hl.dispatch(hl.dsp.cursor.move({x=math.floor(laptop.x+w/2),y=math.floor(laptop.y+h/2)}))
    overflowX,overflowY=0,0
end
-- A special workspace toggles on the focused monitor: close it on the canvas output, focus the
-- laptop's workspace and open it there.
local function showSpecialOnLaptop(name)
    local laptop=laptopMonitor()
    hl.dispatch(hl.dsp.focus({workspace="name:"..CANVAS_WS}))
    hl.dispatch(hl.dsp.workspace.toggle_special(name))
    local workspace=laptop and laptop.active_workspace
    if not (workspace and workspace.id) then return end
    hl.dispatch(hl.dsp.focus({workspace=tostring(workspace.id)}))
    hl.dispatch(hl.dsp.workspace.toggle_special(name))
end
-- SUPER+1..9 or a special workspace on the canvas output: bring the canvas workspace back and
-- send the intruder to the laptop (§3.3 guards).
local function guardWorkspace()
    local mon=canvasMonitor()
    if not mon then return end
    local special,current=mon.active_special_workspace,mon.active_workspace
    if special then
        pcall(showSpecialOnLaptop,(tostring(special.name or ""):gsub("^special:","")))
    elseif current and current.name~=CANVAS_WS then
        hl.dispatch(hl.dsp.focus({workspace="name:"..CANVAS_WS}))
        local laptop=laptopMonitor()
        if laptop and current.id then hl.dispatch(hl.dsp.workspace.move({workspace=current.id,monitor=laptop.name})) end
    end
end
local function markWindowsDirty() windowsDirty=true end
-- "unset" drops the set_prop overrides, so the window's own decorations come back (and a sliver's no_follow_mouse).
local function undecorate(address)
    dropSliver(address)
    for _,prop in ipairs(PROPS) do pcall(windowDispatch,"set_prop",address,{prop=prop[1],value="unset"}) end
end
-- The backend floats tiled windows before Lua sees them: its journal (canvas-session.json, written by
-- json.dumps) holds the true origin. A missing or unreadable entry falls back to Lua's own record.
local function journaledOrigin(address)
    local file=io.open(state.."/omarchy-xr/canvas-session.json","r")
    if not file then return nil end
    local text=file:read("*a") or "";file:close()
    local block=text:match('"'..address..'"%s*:%s*(%b{})')
    local floating=block and block:match('"floating"%s*:%s*(%a+)')
    if floating~="true" and floating~="false" then return nil end
    local function field(key)
        local list=block:match('"'..key..'"%s*:%s*(%b[])') or ""
        local a,b=list:match("^%[%s*(%-?%d+)%s*,%s*(%-?%d+)%s*%]$")
        return tonumber(a),tonumber(b)
    end
    local width,height=field("size")
    local x,y=field("at")
    return {floating=floating=="true",w=width,h=height,x=x,y=y}
end
-- Leaving the canvas (SUPER+SHIFT+n) restores the window's origin state: decorations, then tiled, or
-- its floating size and position.
local function release(w)
    local address=w.address
    local origin=journaledOrigin(address) or canvas.origin[address]
    undecorate(address)
    if origin.floating==false then
        if w.floating then windowDispatch("float",address,{action="tile"}) end
    elseif origin.w and origin.x then
        windowDispatch("resize",address,{x=math.floor(origin.w),y=math.floor(origin.h)})
        windowDispatch("move",address,{x=math.floor(origin.x),y=math.floor(origin.y)})
    end
    canvas.origin[address]=nil
    if canvas.staged==address then canvas.staged=nil;overflowX,overflowY=0,0 end
end
-- A small window of a canvas window's process is its dialog: it is staged, not parked.
local function dialogParent(w)
    local width,height=pair(w.size)
    for _,other in ipairs(listWindows()) do
        local otherW,otherH=pair(other.size)
        if other.address~=w.address and other.pid==w.pid and isMember(other) and width*height<.6*otherW*otherH then return other end
    end
end
-- An excluded window that lands on the canvas output goes to the laptop's workspace instead.
local function evict(w)
    local laptop=laptopMonitor()
    local workspace=laptop and laptop.active_workspace
    if isMember(w) and workspace and workspace.id then windowDispatch("move",w.address,{workspace=tostring(workspace.id),follow=false}) end
end
local function adopt(w)
    local mon=canvasMonitor()
    if not mon then return end
    if isExcluded(w) then evict(w);return end
    enforce(w)
    if w.pid and dialogParent(w) then stageWindow(w.address,mon)
    elseif w.address~=canvas.staged and w.workspace.name~=PARK_WS then
        windowDispatch("move",w.address,{workspace="name:"..PARK_WS,follow=false})
    end
end
local function onOpen(w)
    windowsDirty=true
    if not canvasMode or not w or not isRegular(w) then return end
    if canvasPolicy=="all" or isMember(w) then adopt(w) end
end
local function onClose(w)
    windowsDirty=true
    if not w or not w.address then return end
    if canvas.staged==w.address then canvas.staged=nil end
    canvas.origin[w.address]=nil;canvas.slivers[w.address]=nil
end
local function onMove(w)
    windowsDirty=true
    if not canvasMode or not w or not w.address then return end
    if isMember(w) then
        if not canvas.origin[w.address] and isRegular(w) then adopt(w) end
    elseif canvas.origin[w.address] then release(w) end
end
-- Compositor fullscreen slips past suppress_event: revert it and ask the renderer for Fill.
local function onFullscreen(w)
    windowsDirty=true
    if not canvasMode or not w or not isMember(w) or (w.fullscreen or 0)==0 then return end
    windowDispatch("fullscreen_state",w.address,{internal=0,client=0})
    publish(0,CANVAS_MODES.fill)
end
-- Keyboard focus on a canvas window is published by address; XR's own staging never refits.
local function onActive(w)
    windowsDirty=true
    if not canvasMode or not w then return end
    local address=isMember(w) and w.address or nil
    if address and address~=focusAddress and not gazeDispatch then focusPending=address end
    focusAddress=address
end
local CANVAS_EVENTS={["window.open"]=onOpen, ["window.close"]=onClose, ["window.destroy"]=onClose,
    ["window.title"]=markWindowsDirty, ["window.class"]=markWindowsDirty, ["window.active"]=onActive,
    ["window.fullscreen"]=onFullscreen, ["window.move_to_workspace"]=onMove, ["window.urgent"]=markWindowsDirty}
-- `.pane` lines (v1 <owner> <seq> <monitor> <x> <y> <w> <h> <stamp>, or "-"), written on change only.
local paneSerial,paneLast=0,nil
local function writePane(line)
    if line==paneLast then return end
    paneLast=line;paneSerial=paneSerial+1
    writeMailbox(".pane",string.format("v1 %s %d %s %d\n",session,paneSerial,line,math.floor(bootSeconds())))
end
-- Virtual monitors mode windows for the XR layer (docs/xr-controls-plan.md §5.3-5.4): the visible windows of
-- every XR monitor, monitors left to right, each one's windows left to right and top to bottom.
local monitorWindows
do
    local function rect(w)
        local x,y=pair(w.at);local width,height=pair(w.size)
        return x,y,width,height
    end
    local function xrMonitors()
        local fine,monitors=pcall(hl.get_monitors)
        local out={}
        if not fine or type(monitors)~="table" then return out end
        for _,m in ipairs(monitors) do
            if m.name and m.name:match("^OMXR%-") and not m.name:match(CANVAS_MONITOR_PATTERN) then out[#out+1]=m end
        end
        table.sort(out,function(a,b) if a.x~=b.x then return a.x<b.x end return a.y<b.y end)
        return out
    end
    monitorWindows=function()
        local out={}
        for _,m in ipairs(xrMonitors()) do
            local fine,list=pcall(hl.get_windows,{monitor=m.name,mapped=true})
            local here={}
            local active=m.active_workspace and m.active_workspace.id
            for _,w in ipairs(fine and list or {}) do
                if w.address and not w.hidden and (not active or not w.workspace or w.workspace.id==active) then here[#here+1]=w end
            end
            table.sort(here,function(a,b)
                local ax,ay=rect(a);local bx,by=rect(b)
                if ax~=bx then return ax<bx end
                if ay~=by then return ay<by end
                return a.address<b.address
            end)
            for _,w in ipairs(here) do out[#out+1]={window=w,monitor=m} end
        end
        return out
    end
end
local function paneFor(entry)
    local x,y=pair(entry.window.at);local width,height=pair(entry.window.size)
    local m=entry.monitor
    return string.format("%s %d %d %d %d",m.name,math.floor(x-m.x+.5),math.floor(y-m.y+.5),math.floor(width+.5),math.floor(height+.5))
end
-- XR previous/next outside the canvas: focus the next window (wrapping), point at its centre, publish its
-- pane, then code 22 so the renderer fits it.
local function monitorCycle(step)
    local list=monitorWindows()
    if #list==0 then return end
    local fine,current=pcall(hl.get_active_window)
    local index
    for i,entry in ipairs(list) do if fine and current and entry.window.address==current.address then index=i end end
    local nextIndex=index and ((index-1+step)%#list)+1 or (step>0 and 1 or #list)
    local entry=list[nextIndex]
    local x,y=pair(entry.window.at);local width,height=pair(entry.window.size)
    gazeDispatch=true
    hl.dispatch(hl.dsp.focus({window="address:"..entry.window.address}))
    hl.dispatch(hl.dsp.cursor.move({x=math.floor(x+width/2),y=math.floor(y+height/2)}))
    gazeDispatch=false
    writePane(paneFor(entry))
    publish(0,CANVAS_MODES.cycle,hexToken(step>0 and "next" or "prev"))
end
-- Search outside the canvas (§5.5): the renderer ranks every regular window from `.windows` (the 4-field header,
-- no canvas output), refreshed every second while XR runs and at once when search opens. Enter comes back as
-- `.land` (v1 <owner> <seq> <address> <stamp>): focus the window, which brings its workspace up on its monitor,
-- point at it and publish its pane for the camera.
-- Both scenes write `.windows` with the one sequence number in canvas.windowsSeq, which survives reloads, so
-- the renderer never keeps a stale list after a mode switch.
local monitorSearch={written=-math.huge,landSeq=0,landOwner=nil}
function monitorSearch.publish(now)
    local rows=windowRows(listWindows())
    canvas.windowsSeq=canvas.windowsSeq+1;monitorSearch.written=now
    rows[#rows+1]=""
    writeMailbox(".windows",string.format("v1 %s %d %d",session,canvas.windowsSeq,math.floor(now)).."\n"..table.concat(rows,"\n"))
end
function monitorSearch.tick(now)
    if now-monitorSearch.written>=1 then monitorSearch.publish(now) end
    local file=io.open(path..".land","r")
    if not file then return end
    local line=file:read("*l") or "";file:close()
    local owner,seqText,address,stamp=line:match("^v1 (%d+) (%d+) (0x%x+) (%d+)%s*$")
    if owner~=session then return end
    if monitorSearch.landOwner~=owner then monitorSearch.landOwner,monitorSearch.landSeq=owner,0 end
    local seq=tonumber(seqText)
    if seq<=monitorSearch.landSeq then return end
    monitorSearch.landSeq=seq
    if not fresh(tonumber(stamp)) then return end
    address=address:lower()
    local fine,w=pcall(hl.get_window,"address:"..address)
    if not fine or not w then return end
    local x,y=pair(w.at);local width,height=pair(w.size)
    gazeDispatch=true
    hl.dispatch(hl.dsp.focus({window="address:"..address}))
    hl.dispatch(hl.dsp.cursor.move({x=math.floor(x+width/2),y=math.floor(y+height/2)}))
    gazeDispatch=false
    for _,entry in ipairs(monitorWindows()) do if entry.window.address==address then writePane(paneFor(entry)) end end
end
-- XR fill outside the canvas: toggle maximize on the window focused on the gazed XR monitor, then code 10
-- so the renderer fits that monitor. Unlike the canvas, fullscreen is harmless here: every monitor is its
-- own output.
local function monitorFill()
    local target
    for _,entry in ipairs(monitorWindows()) do
        if entry.monitor.name==hoverName and (not target or (entry.window.focus_history_id or 0)<(target.window.focus_history_id or 0)) then target=entry end
    end
    if not target then return end
    windowDispatch("fullscreen",target.window.address,{mode="maximized"})
    publish(0,CANVAS_MODES.fill)
end
-- Canvas mode from the renderer's heartbeat (the v6 takeover flag after it is ignored).
local function readMode()
    local file=io.open(path..".mode","r")
    if not file then return false end
    local line=file:read("*l") or "";file:close()
    local owner,mode,stamp=line:match("^v1 (%d+) (%a+) [01] (%d+)")
    return owner==session and mode=="canvas" and fresh(tonumber(stamp))
end
do
-- .controls is last-writer, so a wheel notch is "up:<run>:<count>": a run starts with a turn or after any
-- other mode press, and the renderer scrolls every notch of the run since the line it last read.
local wheelRun={id=0,count=0,dir=0,at=-1}
local function publishWheel(entry)
    if wheelRun.at~=fit_serial or wheelRun.dir~=entry.wheel then wheelRun.id=serial+1;wheelRun.count=0;wheelRun.dir=entry.wheel end
    wheelRun.count=wheelRun.count+1
    publish(0,entry.mode,hexToken(entry.token..":"..wheelRun.id..":"..wheelRun.count))
    wheelRun.at=fit_serial
end
-- The XR key layer (docs/xr-controls-plan.md §3): one held modifier plus one key per action, bound only
-- while the viewer runs. The order, ids and default keys match studio/input_settings.py ACTIONS (checked by
-- tests/test_input_settings.py). `canvas`: bound only in canvas mode, otherwise the chord reaches
-- applications. An action publishes a renderer code (the renderer gives it its per-scene meaning), resizes
-- the staged window, or runs Lua. `hold`: a press and a release bind (begin/end).
local LAYER_ACTIONS={
    {"recenter",key="space",code=3},
    {"grab",key="G",code=CANVAS_MODES.grab,hold=true},
    {"zoom_in",key="equal",code=4,repeating=true},
    {"zoom_out",key="minus",code=5,repeating=true},
    {"zoom_level_in",key="Up",code=CANVAS_MODES.level_in},
    {"zoom_level_out",key="Down",code=CANVAS_MODES.level_out},
    {"overview",key="",code=CANVAS_MODES.overview},
    {"focus",key="",code=CANVAS_MODES.confirm},
    {"fill",key="Return",code=CANVAS_MODES.fill,monitors=monitorFill},
    {"previous",key="Left",code=CANVAS_MODES.cycle,token="prev",repeating=true,monitors=function() monitorCycle(-1) end},
    {"next",key="Right",code=CANVAS_MODES.cycle,token="next",repeating=true,monitors=function() monitorCycle(1) end},
    {"scroll_up",key="Page_Up",code=CANVAS_MODES.scroll,token="pageup",canvas=true,repeating=true},
    {"scroll_down",key="Page_Down",code=CANVAS_MODES.scroll,token="pagedown",canvas=true,repeating=true},
    {"search",key="slash",code=CANVAS_MODES.search,monitors=function() monitorSearch.publish(bootSeconds());publish(0,CANVAS_MODES.search) end},
    {"nudge_left",key="SHIFT + Left",code=CANVAS_MODES.nudge,token="left",canvas=true,repeating=true},
    {"nudge_right",key="SHIFT + Right",code=CANVAS_MODES.nudge,token="right",canvas=true,repeating=true},
    {"nudge_up",key="SHIFT + Up",code=CANVAS_MODES.nudge,token="up",canvas=true,repeating=true},
    {"nudge_down",key="SHIFT + Down",code=CANVAS_MODES.nudge,token="down",canvas=true,repeating=true},
    {"narrower",key="comma",resize={dw=-100},canvas=true,repeating=true},
    {"wider",key="period",resize={dw=100},canvas=true,repeating=true},
    {"shorter",key="SHIFT + comma",resize={dh=-100},canvas=true,repeating=true},
    {"taller",key="SHIFT + period",resize={dh=100},canvas=true,repeating=true},
    {"move_window",key="F",code=CANVAS_MODES.move,hold=true,canvas=true},
    {"pin",key="P",code=CANVAS_MODES.pin,canvas=true},
    {"arrange",key="A",code=CANVAS_MODES.arrange,canvas=true},
    {"undo",key="Z",code=CANVAS_MODES.undo,canvas=true},
    {"redo",key="SHIFT + Z",code=CANVAS_MODES.redo,canvas=true},
    {"notification_dismiss",key="N",notification=CANVAS_MODES.dismiss},
    {"notification_next",key="SHIFT + N",notification=CANVAS_MODES.notify_next},
    {"pointer_home",key="Home",run=function() releasePointer() end},
    {"help",key="H",code=CANVAS_MODES.help},
    {"stats",key="grave",code=CANVAS_MODES.stats},
}
local LAYER_BY_ID={}
for _,action in ipairs(LAYER_ACTIONS) do LAYER_BY_ID[action[1]]=action end
layerModifier,layerKeys="CTRL + ALT",{}
for _,action in ipairs(LAYER_ACTIONS) do layerKeys[action[1]]=action.key end
-- Handles survive a config reload in a global so the next chunk can remove them (they may be expired).
omarchy_xr_layer=omarchy_xr_layer or {bindings={}}
local function retireLayer()
    for _,binding in ipairs(omarchy_xr_layer.bindings) do removeBinding(binding) end
    removeBinding(omarchy_xr_layer.holdRelease);omarchy_xr_layer.holdRelease=nil
    omarchy_xr_layer.bindings={}
end
retireLayer()
local function bindLayer(chord,callback,options)
    local binding=hl.bind(chord,callback,options)
    omarchy_xr_layer.bindings[#omarchy_xr_layer.bindings+1]=binding
    return binding
end
local function layerCallback(action)
    if action.run then return action.run end
    if action.monitors then
        local canvasAction=layerCallback({code=action.code,token=action.token})
        return function() if canvasMode then canvasAction() else action.monitors() end end
    end
    if action.resize then return function() resizeStaged(action.resize) end end
    if action.notification then return function() publish(0,action.notification,notificationTarget() or "-") end end
    local token=action.token and hexToken(action.token) or nil
    return function() publish(0,action.code,token) end
end
-- A release bind matches the modifiers held at release, so letting go of the modifier first would miss it
-- (as with the drag): while held, a bare release bind on the key itself ends the hold too. Not for a mouse
-- button: the touchpad tap is bound on the bare button, and remove() would erase it with ours.
local function endHold(code)
    removeBinding(omarchy_xr_layer.holdRelease);omarchy_xr_layer.holdRelease=nil
    publish(0,code,hexToken("end"))
end
local function bindAction(action,chord)
    local description="XR: "..action[1]:gsub("_"," ")
    if action.hold then
        local bare=chord:match("([^+%s]+)%s*$")
        bindLayer(chord,function()
            publish(0,action.code,hexToken("begin"))
            removeBinding(omarchy_xr_layer.holdRelease);omarchy_xr_layer.holdRelease=nil
            if bare:match("^mouse") then return end
            omarchy_xr_layer.holdRelease=hl.bind(bare,function() endHold(action.code) end,{release=true,non_consuming=true,description=description.." (release)"})
        end,{description=description})
        bindLayer(chord,function() endHold(action.code) end,{release=true,description=description.." (release)"})
    else
        bindLayer(chord,layerCallback(action),{repeating=action.repeating,description=description})
    end
end
-- Mouse actions follow the modifier (§3.2): wheel zoom, SHIFT+wheel scroll (canvas), left-drag move and
-- right-drag resize (canvas), middle button held grabs.
local function bindMouse()
    local mod=layerModifier
    bindLayer(mod.." + mouse_up",function() publish(0,4) end,{description="XR: zoom in (wheel)"})
    bindLayer(mod.." + mouse_down",function() publish(0,5) end,{description="XR: zoom out (wheel)"})
    bindAction(LAYER_BY_ID.grab,mod.." + mouse:274")
    if not canvasMode then return end
    if not mod:find("SHIFT",1,true) then
        bindLayer(mod.." + SHIFT + mouse_up",function() publishWheel({mode=CANVAS_MODES.scroll,token="up",wheel=-1}) end,{description="XR: scroll canvas up"})
        bindLayer(mod.." + SHIFT + mouse_down",function() publishWheel({mode=CANVAS_MODES.scroll,token="down",wheel=1}) end,{description="XR: scroll canvas down"})
    end
    bindLayer(mod.." + mouse:272",beginDrag,{description="XR: move window along the ring"})
    bindLayer(mod.." + mouse:272",endDrag,{release=true,description="XR: end window move"})
    bindLayer(mod.." + mouse:273",hl.dsp.window.resize(),{mouse=true,description="XR: resize window"})
end
-- `.keys`: the chords actually bound, for the renderer's help and Studio. v1 <owner> <seq> <stamp>, then
-- "<action>\t<chord>\t<live>" rows; live=0 is a canvas-only action outside the canvas.
local keysSerial,keysText=0,nil
local function publishKeys(rows)
    local body=table.concat(rows,"\n")
    if tostring(session)..body==keysText then return end
    keysText=tostring(session)..body;keysSerial=keysSerial+1
    writeMailbox(".keys",string.format("v1 %s %d %d\n",tostring(session or 0),keysSerial,math.floor(bootSeconds()))..body..(body~="" and "\n" or ""))
end
installLayer=function()
    retireLayer()
    if not active then publishKeys({});return end
    local rows={"modifier\t"..layerModifier}
    for _,action in ipairs(LAYER_ACTIONS) do
        local key=layerKeys[action[1]]
        if key and key~="" then
            local chord=layerModifier.." + "..key
            local live=not action.canvas or canvasMode
            rows[#rows+1]=action[1].."\t"..chord.."\t"..(live and 1 or 0)
            if live then bindAction(action,chord) end
        end
    end
    bindMouse()
    publishKeys(rows)
end
-- controls-settings.tsv v2 (studio/input_settings.py tsv()): the modifier, the gesture fingers, then
-- `key<TAB>action<TAB>key` rows. Values are checked against the known actions and a strict character set;
-- anything else (a v1 file, a bad line) keeps the defaults for that value.
local function validChord(text) return #text<=100 and not text:find("[^%w_ +]") end
parseLayerSettings=function(text)
    local lines={};for line in text:gmatch("(.-)\n") do lines[#lines+1]=line end
    if lines[1]~="v2" then return nil end
    local modifier,count=layerModifier,fingers
    local keys,seen={},{};for _,action in ipairs(LAYER_ACTIONS) do keys[action[1]]=action.key end
    for i=2,#lines do
        local kind,a,b=lines[i]:match("^(%a+)\t([^\t]*)\t?([^\t]*)$")
        if kind=="modifier" and a:match("^%u+ %+ [%u +]+$") and validChord(a) then modifier=a
        elseif kind=="fingers" and (a=="3" or a=="5") then count=tonumber(a)
        elseif kind=="key" and LAYER_BY_ID[a] and validChord(b) then keys[a]=b;seen[a]=true end
    end
    -- A file from before the zoom levels: Up and Down (the old overview and focus defaults) move to them.
    if not seen.zoom_level_in then
        if keys.overview=="Up" then keys.overview="" end
        if keys.focus=="Down" then keys.focus="" end
    end
    return modifier,count,keys
end
end
-- adoptPolicy is field 8 of the `# canvas v1 ...` header Studio writes (field 9 is takeoverKeys); `exclude` rows follow.
local function readCanvasSettings()
    canvasPolicy,canvasExcludes="all",{}
    local file=io.open(state.."/omarchy-xr/canvas.tsv","r")
    if not file then return end
    local text=file:read("*a") or "";file:close()
    local policy=text:match("^# canvas v1"..("%s+%S+"):rep(7).."%s+([%a-]+)")
    canvasPolicy=policy=="empty" and "empty" or "all"
    for token in text:gmatch("\nexclude%s+(%S+)") do canvasExcludes[token]=true end
end
local function enterCanvas()
    canvasMode=true
    installLayer()
    retireCanvasEvents()
    for name,callback in pairs(CANVAS_EVENTS) do canvas.events[#canvas.events+1]=hl.on(name,callback) end
    windowsDirty=true;lastWindowsWrite=-math.huge;readCanvasSettings()
    focusAddress=nil;focusPending=nil;cursorLine=nil;lastTap=nil
end
local function leaveCanvas()
    canvasMode=false
    endDrag()
    installLayer()
    retireCanvasEvents()
    for address in pairs(canvas.origin) do undecorate(address) end
    for address in pairs(canvas.slivers) do dropSliver(address) end
    canvas.staged=nil;canvas.origin={};snapshot={};canvas.slivers={};canvas.tiersWanted={}
    overflowX,overflowY,cursorLine=0,0,nil
    focusAddress=nil;focusPending=nil
end
local function switchCanvas()
    local wanted=active and readMode()
    if wanted and not canvasMode then enterCanvas() elseif canvasMode and not wanted then leaveCanvas() end
end
updateCanvas=function()
    switchCanvas()
    if not canvasMode then
        if active then monitorSearch.tick(bootSeconds()) end
        return
    end
    local now=bootSeconds()
    local mon=now-lastWindowsWrite>=1 and canvasMonitor()
    if mon then readCanvasSettings();publishWindows(now,mon) end
end
-- Canvas mode keeps the canvas workspace on the canvas output instead of publishing monitor focus.
local function followCanvas()
    if not workspacePending then return end
    workspacePending=false
    refresh()
    if canvasMode then guardWorkspace() end
end
local function followWorkspace()
    if not workspacePending then return end
    workspacePending=false
    refresh()
    if not active then return end
    local workspace=hl.get_active_workspace()
    local monitor=workspace and workspace.monitor
    local name=monitor and monitor.name
    focusWaiting=nil
    if not name or workspace.special or monitor.active_special_workspace or not name:match("^OMXR%-[%w_-]+$") then return end
    writeFocus(name)
    focusWaiting={name=name,untilTime=bootSeconds()+2}
    gazeTarget=name
end
local function waitForFocus(mode,name,pointerSerial)
    if not focusWaiting then return false end
    -- A mailbox sample from before the shortcut must not switch focus back or
    -- warp the pointer. The renderer acknowledges by publishing its selection.
    pointerSerialSeen=pointerSerial
    if mode=="1" and name==focusWaiting.name then gazeTarget=name end
    if mode=="1" and name==focusWaiting.name or bootSeconds()>=focusWaiting.untilTime then
        focusWaiting=nil
        return false
    end
    return true
end
-- v3 appends a pointer serial and the dwelled monitor pixel; older lines carry no pointer.
-- v4 (canvas) names a window address and a pixel of that window's buffer.
local function hoverTarget(line)
    local owner,serialText,mode,name,pointerSerial,px,py=line:match("^v4 (%d+) (%d+) ([01]) (%S+) %S+ %S+ (%d+) (%S+) (%S+)")
    if owner then return owner,serialText,mode,name,tonumber(pointerSerial),tonumber(px),tonumber(py) end
    owner,serialText,mode,name,pointerSerial,px,py=line:match("^v3 (%d+) (%d+) ([01]) ([%w_-]+) %S+ %S+ (%d+) (%S+) (%S+)")
    if owner then return owner,serialText,mode,name,tonumber(pointerSerial),tonumber(px),tonumber(py) end
    owner,serialText,mode,name=line:match("^v2 (%d+) (%d+) ([01]) ([%w_-]+) ")
    if owner then return owner,serialText,mode,name end
    return line:match("^(%d+) (%d+) ([01]) ([%w_-]+) ")
end
-- A dwell warps the desktop pointer to the look point once and focuses the window under it.
-- Hyprland's follow-mouse may already focus it; the explicit dispatch covers the other policies.
local function warpPointer(name,px,py)
    for _,monitor in ipairs(hl.get_monitors()) do
        if monitor.name==name then
            local scale=monitor.scale or 1
            local x,y=monitor.x+px/scale,monitor.y+py/scale
            local ok,windows=pcall(hl.get_windows,{monitor=name,mapped=true})
            if ok and windows then
                local best
                for _,w in ipairs(windows) do
                    local at,size=w.at,w.size
                    if type(at)=="table" and type(size)=="table" then
                        local wx,wy=at.x or at[1],at.y or at[2]
                        local ww,wh=size.x or size[1],size.y or size[2]
                        if wx and x>=wx and x<wx+ww and y>=wy and y<wy+wh and not w.hidden then
                            if not best or w.floating then best=w end
                        end
                    end
                end
                if best and not best.active then pcall(function() hl.dispatch(hl.dsp.focus({window=best})) end) end
            end
            -- Focusing warps the cursor to the window's centre, so the move to the look point comes last.
            hl.dispatch(hl.dsp.cursor.move({x=math.floor(x+.5),y=math.floor(y+.5)}))
            return
        end
    end
end
local function focusMonitor(name)
    for _,monitor in ipairs(hl.get_monitors()) do
        if monitor.name==name then
            local workspace=monitor.active_workspace
            if not workspace or workspace.special then return end
            -- config_name also handles named workspaces correctly.
            hl.dispatch(hl.dsp.focus({workspace=workspace.config_name or tostring(workspace.id)}))
            gazeTarget=name
            return
        end
    end
end
local function noteHover(owner, serialNumber)
    if owner~=session or not serialNumber then return false end
    if gazeOwner~=owner then
        gazeOwner=owner;gazeSerial=serialNumber;gazeTarget=nil;return false
    end
    if serialNumber==gazeSerial then return false end
    gazeSerial=serialNumber
    return true
end
local function selectGazeWorkspace()
    if not active then gazeOwner=nil;gazeSerial=nil;gazeTarget=nil;pointerSerialSeen=nil;hoverName=nil;return end
    local file=io.open(path..".hover","r")
    if not file then return end
    local line=file:read("*l");file:close()
    if not line then return end
    local owner,serialText,mode,name,pointerSerial,px,py=hoverTarget(line)
    local serialNumber=tonumber(serialText)
    if not noteHover(owner, serialNumber) then pointerSerialSeen=pointerSerial;return end
    if canvasMode then canvasHover(mode,name,pointerSerial,px,py);return end
    if waitForFocus(mode,name,pointerSerial) then return end
    if pointerSerial and pointerSerial~=pointerSerialSeen then
        -- The first sample of a session only records the serial; a pre-existing dwell is not replayed.
        if pointerSerialSeen~=nil and pointerSerial>0 and mode=="1" and name:match("^OMXR%-") then warpPointer(name,px,py) end
        pointerSerialSeen=pointerSerial
    end
    hoverName=mode=="1" and name or nil
    if mode~="1" or not name:match("^OMXR%-") then gazeTarget=nil;return end
    if gazeTarget==name then return end
    focusMonitor(name)
end
-- The last-focused window on the selected XR monitor, for the pane zoom level; written on change
-- only. The globally active window is usually elsewhere (the laptop screen), so the monitor's
-- own focus history is what the pane fit needs.
local function publishPane(name)
    if not active or canvasMode then paneLast=nil;return end
    local line="-"
    if name and name:match("^OMXR%-") then
        for _,m in ipairs(hl.get_monitors()) do
            if m.name==name and m.x and m.y then
                -- The most recently focused mapped window on this monitor: the lowest focus history index.
                local ok,windows=pcall(hl.get_windows,{monitor=name,mapped=true})
                local best,bestRank
                if ok and windows then
                    for _,w in ipairs(windows) do
                        local rank=w.focus_history_id
                        if type(w.at)=="table" and type(w.size)=="table" and not w.hidden and rank and (not bestRank or rank<bestRank) then best,bestRank=w,rank end
                    end
                end
                if best then
                    local ax,ay=best.at.x or best.at[1],best.at.y or best.at[2]
                    local sw,sh=best.size.x or best.size[1],best.size.y or best.size[2]
                    if ax and ay and sw and sh then
                        line=string.format("%s %d %d %d %d",m.name,math.floor(ax-m.x+.5),math.floor(ay-m.y+.5),math.floor(sw+.5),math.floor(sh+.5))
                    end
                end
                break
            end
        end
    end
    writePane(line)
end
local function updatePointer()
    if canvasMode then followCanvas() else followWorkspace() end
    gazeDispatch=true
    local success,message=pcall(selectGazeWorkspace)
    gazeDispatch=false
    if not success then error(message) end
    publishPane(hoverName)
    if canvasMode then settleTap(tapTime());canvasTick() elseif active then monitorSearch.tick(bootSeconds()) end
end
local hoverTimer
setHoverTimer = function(enabled)
    if enabled then
        if not hoverTimer then hoverTimer=hl.timer(updatePointer,{timeout=33,type="repeat"}) end
    elseif hoverTimer then
        hoverTimer:set_enabled(false)
        hoverTimer=nil
    end
    omarchy_xr_controls.hover_timer=hoverTimer
end
omarchy_xr_controls.hover=updatePointer
refresh()
