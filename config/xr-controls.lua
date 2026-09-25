-- Omarchy XR: live controls, active only while the renderer heartbeat is fresh.
-- No processes are spawned during a gesture. Atomic cumulative samples are read
-- by the renderer each frame. Horizontal gestures remain available to Hyprland.
local state = os.getenv("XDG_STATE_HOME") or (os.getenv("HOME") .. "/.local/state")
local runtime = os.getenv("XDG_RUNTIME_DIR")
local runtime_root = (runtime and runtime ~= "" and (runtime .. "/omarchy-xr")) or (state .. "/omarchy-xr")
local path = runtime_root .. "/pose.sock.controls"
local CONTROLS_VERSION = 6
-- Window canvas (v6): fixed names shared with the backend and the renderer (§3.3, §5.5).
local CANVAS_WS, PARK_WS = "omxr-canvas", "omxr-park"
local CANVAS_MONITOR_PATTERN = "^OMXR%-%x%x%x%x%x%x%x%x%-canvas$"
local EXCLUDED_CLASSES = {["omarchy-xr-spectator"]=true, ["omarchy-xr-search"]=true}
local CANVAS_MODES = {overview=8, search=9, fill=10, mru_next=11, mru_prev=12, arrange=13, neighbour=14, nudge=15, pin=16, help=17}
local setHoverTimer, updateCanvas, releasePointer
local fingers=3
omarchy_xr_controls = omarchy_xr_controls or {version=CONTROLS_VERSION}
local function retireHoverTimer()
    local timer=omarchy_xr_controls.hover_timer
    if not timer then return end
    timer:set_enabled(false)
    omarchy_xr_controls.hover_timer=nil
end
local function retireWorkspaceEvents()
    for _,subscription in ipairs(omarchy_xr_controls.workspace_events or {}) do subscription:remove() end
end
-- Canvas state that must survive a config reload: journaled origins, the staged address,
-- event subscriptions, the SUPER+F takeover and the mailbox sequence numbers.
local function canvasState()
    omarchy_xr_canvas = omarchy_xr_canvas or {origin={}, staged=nil, events={}, fill=nil, windowsSeq=0, cursorSeq=0}
    return omarchy_xr_canvas
end
local canvas = canvasState()
local function retireCanvasEvents()
    for _,subscription in ipairs(canvas.events or {}) do subscription:remove() end
    canvas.events={}
end
local function retireFill()
    local binding=canvas.fill
    if not binding then return false end
    pcall(function() binding:remove() end)
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
retireHoverTimer()
retireWorkspaceEvents()
local function retireCanvas()
    retireCanvasEvents()
    if canvas.fill then restoreFullscreen() end
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
    taps[1]:set_enabled(false)
end
local keys={"CTRL + Up","CTRL + Down","","",""}
local descriptions={"fit all monitors","fit selected monitor","recenter","zoom in","zoom out"}
local bindings={}
local settingsText
local function configure(nextFingers,nextKeys)
    if active then registerVertical(false) end
    swipe=nil;cancelTap()
    for _,binding in ipairs(bindings) do binding:remove() end
    bindings={};keys=nextKeys;fingers=nextFingers==4 and 3 or nextFingers
    for mode,key in ipairs(keys) do
        if key~="" then
            local action=mode
            local binding=hl.bind(key,function() publish(0,action) end,{description="XR: "..descriptions[mode]})
            binding:set_enabled(active);bindings[#bindings+1]=binding
        end
    end
    if active then registerVertical(true) end
    if omarchy_xr_controls then omarchy_xr_controls.bindings=bindings;omarchy_xr_controls.fingers=fingers end
end
configure(fingers,keys)
local function settingKeys(lines, count)
    if #lines~=6 or not count or count%1~=0 or count<3 or count>5 then return nil end
    local nextKeys={}
    for i=2,6 do
        if #lines[i]>100 or lines[i]:find("[^%w_ +]") then return nil end
        nextKeys[#nextKeys+1]=lines[i]
    end
    return nextKeys
end
local function readSettings()
    local file=io.open(state.."/omarchy-xr/controls-settings.tsv","r")
    if not file then return end
    local text=file:read("*a");file:close()
    if text==settingsText then return end
    local lines={};for line in text:gmatch("(.-)\n") do lines[#lines+1]=line end
    local count=tonumber(lines[1])
    local nextKeys=settingKeys(lines, count)
    if not nextKeys then return end
    configure(count,nextKeys);settingsText=text
end
local function adoptSession(owner)
    session = tostring(owner); serial = 0; total = 0; fit_serial = 0; fit_mode = 0
    workspacePending=false;focusWaiting=nil;focusSerial=0
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
    for _,binding in ipairs(taps) do binding:set_enabled(active) end
    for _,binding in ipairs(bindings) do binding:set_enabled(active) end
    registerVertical(active)
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
omarchy_xr_controls = {version=CONTROLS_VERSION, tap_bindings=taps, recenter=function() publish(0,3) end,bindings=bindings, fingers=fingers, timer=timer, refresh=refresh, gesture=gesture, pan=panGesture, fit_all=function() publish(0,1) end, fit_target=function() publish(0,2) end}

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
local function place(w)
    if w.address==canvas.staged then return "stage" end
    return w.workspace and w.workspace.name==PARK_WS and "park" or "off"
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
        if member and not isExcluded(w) then enforce(w) end
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
local function stageWindow(address,mon)
    local fine,w=pcall(hl.get_window,"address:"..address)
    if not fine or not w then return false end
    local previous=canvas.staged
    if previous and previous~=address then windowDispatch("move",previous,{workspace="name:"..PARK_WS,follow=false}) end
    enforce(w)
    -- The stage band is the whole output in M3: the window sits at its origin, clamped to it.
    local maxW,maxH=logicalSize(mon)
    local width,height=pair(w.size)
    width,height=math.floor(math.min(width,maxW)),math.floor(math.min(height,maxH))
    windowDispatch("move",address,{workspace="name:"..CANVAS_WS,follow=false})
    windowDispatch("resize",address,{x=width,y=height})
    windowDispatch("move",address,{x=mon.x,y=mon.y})
    windowDispatch("bring_to_top",address)
    hl.dispatch(hl.dsp.focus({window="address:"..address}))
    canvas.staged=address;overflowX,overflowY=0,0;windowsDirty=true
    snapshot[address]={x=mon.x,y=mon.y,w=width,h=height}
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
    local x,y=c.x,c.y
    if onMonitor(mon,x,y) then x,y=confine(x,y) else overflowX,overflowY=0,0 end
    local line=string.format("%.1f %.1f %.1f %.1f",x,y,overflowX,overflowY)
    if line==cursorLine and now-cursorWritten<.4 then return end
    canvas.cursorSeq=canvas.cursorSeq+1
    writeMailbox(".cursor",string.format("v1 %s %d %s %d\n",session,canvas.cursorSeq,line,math.floor(now)))
    cursorLine,cursorWritten=line,now
end
local function flushFocus()
    if not focusPending then return end
    writeFocus(focusPending)
    focusPending=nil
end
local function canvasTick()
    local mon=canvasMonitor()
    if not mon then return end
    local now=bootSeconds()
    if windowsDirty and now-lastWindowsWrite>=.1 then publishWindows(now,mon) end
    publishCursor(mon,now)
    flushFocus()
end
-- The 3-finger double tap hands the pointer back to the first non-XR monitor.
releasePointer=function()
    if not canvasMode then return end
    local mon,laptop=canvasMonitor(),laptopMonitor()
    local fine,c=pcall(hl.get_cursor_pos)
    if not (mon and laptop and fine and c and onMonitor(mon,c.x,c.y)) then return end
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
-- "unset" drops the set_prop overrides, so the window's own decorations come back.
local function undecorate(address)
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
    canvas.origin[w.address]=nil
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
local function readMode()
    local file=io.open(path..".mode","r")
    if not file then return false end
    local line=file:read("*l") or "";file:close()
    local owner,mode,_,stamp=line:match("^v1 (%d+) (%a+) ([01]) (%d+)")
    return owner==session and mode=="canvas" and fresh(tonumber(stamp))
end
-- adoptPolicy is the last field of the `# canvas v1 ...` header Studio writes; `exclude` rows follow.
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
    retireFill()
    hl.unbind("SUPER + F")
    canvas.fill=hl.bind("SUPER + F",function() publish(0,CANVAS_MODES.fill) end,{description="XR: fill window (canvas)"})
    retireCanvasEvents()
    for name,callback in pairs(CANVAS_EVENTS) do canvas.events[#canvas.events+1]=hl.on(name,callback) end
    windowsDirty=true;lastWindowsWrite=-math.huge;readCanvasSettings()
    focusAddress=nil;focusPending=nil;cursorLine=nil
end
local function leaveCanvas()
    canvasMode=false
    restoreFullscreen()
    retireCanvasEvents()
    for address in pairs(canvas.origin) do undecorate(address) end
    canvas.staged=nil;canvas.origin={};snapshot={}
    overflowX,overflowY,cursorLine=0,0,nil
    focusAddress=nil;focusPending=nil
end
updateCanvas=function()
    local wanted=active and readMode()
    if wanted and not canvasMode then enterCanvas() elseif canvasMode and not wanted then leaveCanvas() end
    if not canvasMode then return end
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
local paneSerial,paneLast=0,nil
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
    if line==paneLast then return end
    paneLast=line;paneSerial=paneSerial+1
    writeMailbox(".pane",string.format("v1 %s %d %s %d\n",session,paneSerial,line,math.floor(bootSeconds())))
end
local function updatePointer()
    if canvasMode then followCanvas() else followWorkspace() end
    gazeDispatch=true
    local success,message=pcall(selectGazeWorkspace)
    gazeDispatch=false
    if not success then error(message) end
    publishPane(hoverName)
    if canvasMode then canvasTick() end
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
