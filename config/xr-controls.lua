-- Omarchy XR: live controls, active only while the renderer heartbeat is fresh.
-- No processes are spawned during a gesture. Atomic cumulative samples are read
-- by the renderer each frame. Horizontal gestures remain available to Hyprland.
local state = os.getenv("XDG_STATE_HOME") or (os.getenv("HOME") .. "/.local/state")
local runtime = os.getenv("XDG_RUNTIME_DIR")
local runtime_root = (runtime and runtime ~= "" and (runtime .. "/omarchy-xr")) or (state .. "/omarchy-xr")
local path = runtime_root .. "/pose.sock.controls"
local CONTROLS_VERSION = 2
local setHoverTimer
omarchy_xr_controls = omarchy_xr_controls or {version=CONTROLS_VERSION}
local function retireHoverTimer()
    local timer=omarchy_xr_controls.hover_timer
    if not timer then return end
    timer:set_enabled(false)
    omarchy_xr_controls.hover_timer=nil
end
retireHoverTimer()
local session, serial, total, fit_serial, fit_mode = nil, 0, 0, 0, 0
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
local function hoverTarget(line)
    local owner,serialText,mode,name=line:match("^v2 (%d+) (%d+) ([01]) ([%w_-]+) ")
    if owner then return owner,serialText,mode,name end
    return line:match("^(%d+) (%d+) ([01]) ([%w_-]+) ")
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
    if not active then gazeOwner=nil;gazeSerial=nil;gazeTarget=nil;return end
    local file=io.open(path..".hover","r")
    if not file then return end
    local line=file:read("*l");file:close()
    if not line then return end
    local owner,serialText,mode,name=hoverTarget(line)
    local serialNumber=tonumber(serialText)
    if not noteHover(owner, serialNumber) then return end
    if mode~="1" or not name:match("^OMXR%-") then gazeTarget=nil;return end
    if gazeTarget==name then return end
    focusMonitor(name)
end
local function updatePointer() selectGazeWorkspace() end
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
