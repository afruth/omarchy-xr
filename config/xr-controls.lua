-- Omarchy XR: live controls, active only while the renderer heartbeat is fresh.
-- No processes are spawned during a gesture. Atomic cumulative samples are read
-- by the renderer each frame. Horizontal gestures remain available to Hyprland.
local state = os.getenv("XDG_STATE_HOME") or (os.getenv("HOME") .. "/.local/state")
local runtime = os.getenv("XDG_RUNTIME_DIR")
local runtime_root = (runtime and runtime ~= "" and (runtime .. "/omarchy-xr")) or (state .. "/omarchy-xr")
local path = runtime_root .. "/pose.sock.controls"
local CONTROLS_VERSION = 4
local setHoverTimer
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
retireHoverTimer()
retireWorkspaceEvents()
local session, serial, total, fit_serial, fit_mode = nil, 0, 0, 0, 0
local workspacePending, gazeDispatch = false, false
local focusSerial, focusWaiting = 0, nil
local active = false
local lastTap, tapBlockedUntil=nil,0
local function bootSeconds()
    local file=io.open("/proc/uptime","r")
    if not file then return os.time() end
    local seconds=file:read("*n");file:close()
    return seconds or os.time()
end
local function fresh(stamp)
    if stamp==nil then return false end
    local now=stamp>1000000000 and os.time() or bootSeconds()
    return now-stamp>=0 and now-stamp<=2
end
local function numbers(line)
    if not line then return {} end
    line=line:gsub("^v2%s+","")
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
local function publish(delta, mode)
    if not active then return end
    total = total + (delta or 0)
    serial = serial + 1
    if mode then fit_serial = serial; fit_mode = mode end
    local file = io.open(path .. ".tmp", "w")
    if not file then return end
    file:write(string.format("v2 %s %d %.9f %d %d\n", session, serial, total, fit_serial, fit_mode))
    file:close()
    os.rename(path .. ".tmp", path)
end
-- Buffer a short fast candidate so a flick fits without first jumping in zoom.
-- Slow motion commits early; sustained motion becomes continuous at 220 ms.
local swipe
local function commitZoom()
    if swipe and swipe.pending~=0 then publish(-swipe.pending*.004);swipe.pending=0 end
end
local function finishSwipe(e)
    local elapsed=math.max(1,(e.time_ms or swipe.started)-swipe.started)
    local flick=not swipe.live and elapsed<=220 and math.abs(swipe.net)>=35
        and math.abs(swipe.net)/elapsed>=.35 and math.abs(swipe.net)>=swipe.travel*.8
    if flick then publish(0,swipe.net<0 and 2 or 1) else commitZoom() end
end
local gesture = {
    start = function(e)
        cancelTap()
        swipe={started=e.time_ms or 0,net=0,travel=0,pending=0,live=false}
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
-- Separate cumulative mailbox: pan and zoom never consume each other's deltas.
local panSerial,panId,panX,panY,panActive=0,0,0,0,false
local function publishPan()
    if not active then return end
    panSerial=panSerial+1
    local file=io.open(path..".pan.tmp","w")
    if not file then return end
    file:write(string.format("v2 %s %d %d %.9f %.9f %d %d\n",session,panSerial,panId,panX,panY,panActive and 1 or 0,math.floor(bootSeconds())))
    file:close();os.rename(path..".pan.tmp",path..".pan")
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
        lastTap=nil;publish(0,3);return
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
local fingers=3
local settingsText
local function configure(nextFingers,nextKeys)
    if active then hl.gesture({fingers=fingers,direction="vertical",action="unset"}) end
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
    if active then hl.gesture({fingers=fingers,direction="vertical",action=gesture}) end
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
    hl.gesture({fingers=fingers, direction="vertical", action=active and gesture or "unset"})
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
    local file=io.open(path..".focus.tmp","w")
    if not file then return end
    focusSerial=focusSerial+1
    file:write(string.format("v1 %s %d %s %d\n",session,focusSerial,name,math.floor(bootSeconds())))
    file:close();os.rename(path..".focus.tmp",path..".focus")
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
local function hoverTarget(line)
    local owner,serialText,mode,name,pointerSerial,px,py=line:match("^v3 (%d+) (%d+) ([01]) ([%w_-]+) %S+ %S+ (%d+) (%S+) (%S+)")
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
    if not active then paneLast=nil;return end
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
    local file=io.open(path..".pane.tmp","w")
    if not file then return end
    file:write(string.format("v1 %s %d %s %d\n",session,paneSerial,line,math.floor(bootSeconds())))
    file:close();os.rename(path..".pane.tmp",path..".pane")
end
local function updatePointer()
    followWorkspace()
    gazeDispatch=true
    local success,message=pcall(selectGazeWorkspace)
    gazeDispatch=false
    if not success then error(message) end
    publishPane(hoverName)
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
