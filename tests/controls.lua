-- Exercise actual Lua callbacks without a compositor or any input devices.
local files,bindings,gestures,events,unbound,bindLists={}, {}, {}, {}, {}, {}
package.preload["hypr.xr-touchpads"]=function()return {"test-touchpad"} end
local now=100
files["/proc/uptime"]="100"
os.getenv=function(key)
    if key=="XDG_STATE_HOME" then return "/state" end
    if key=="XDG_RUNTIME_DIR" then return "/run" end
    if key=="HOME" then return "/home/user" end
    return nil
end
os.time=function() return now end
os.rename=function(a,b) files[b]=files[a];files[a]=nil;return true end
io.open=function(path,mode)
    if mode=="r" then
        if not files[path] then return nil end
        return {read=function(_,format) if format=="*l" or format=="*a" then return files[path] end local fields={} for v in files[path]:gmatch("%S+") do fields[#fields+1]=tonumber(v) end return table.unpack(fields) end,close=function() end}
    end
    return {write=function(_,value) files[path]=value end,close=function() end}
end
hl={
    on=function(name,callback)
        local subscription={callback=callback}
        subscription.remove=function(self) if events[name]==self then events[name]=nil end end
        events[name]=subscription;return subscription
    end,
    get_config=function(_)return "lrm" end,
    bind=function(key,callback,options)
        local binding={options=options,callback=callback,set_enabled=function(self,value)self.enabled=value end}
        binding.remove=function(self)
            if bindings[key]==self then bindings[key]=nil end
            for i,other in ipairs(bindLists[key] or {}) do if other==self then table.remove(bindLists[key],i);break end end
        end
        bindings[key]=binding;bindLists[key]=bindLists[key] or {};table.insert(bindLists[key],binding);return binding
    end,
    unbind=function(chord) unbound[#unbound+1]=chord;bindings[chord]=nil;bindLists[chord]=nil end,
    gesture=function(spec) gestures[#gestures+1]=spec end,
    timer=function(callback,options) return {callback=callback, timeout=options and options.timeout, enabled=true, set_enabled=function(self,value) self.enabled=value end} end,
}
local path="/run/omarchy-xr/pose.sock.controls"
dofile("config/xr-controls.lua")
local function gstart(t)omarchy_xr_controls.gesture.start({time_ms=t,delta={y=0}})end
local function gmove(t,y)omarchy_xr_controls.gesture.update({time_ms=t,delta={y=y}})end
local function gend(t,cancelled)omarchy_xr_controls.gesture.finish({time_ms=t,cancelled=cancelled})end
local firstTimer, gazeTimer
local function testActivation()
assert(not bindings["CTRL + Up"].enabled and #gestures==0)
files[path..".active"]="42 100"
omarchy_xr_controls.refresh()
assert(bindings["CTRL + Up"].enabled and gestures[#gestures].direction=="vertical")
assert(omarchy_xr_controls.version==6 and omarchy_xr_controls.hover_timer.timeout==33 and omarchy_xr_controls.hover_timer.enabled)
firstTimer=omarchy_xr_controls.hover_timer
end
local function testDoubleTap()
local tap=bindings["mouse:274"]
assert(tap.enabled and tap.options.device.inclusive and tap.options.device.list[1]=="test-touchpad")
local function tapAt(ms)files["/proc/uptime"]=tostring(ms/1000);tap.callback()end
local before=files[path]
tapAt(101000);assert(files[path]==before) -- single tap does nothing
tapAt(101200);assert(files[path]~=before and files[path]:match("3\n$"))
before=files[path]
tapAt(101300);assert(files[path]==before) -- third tap doesn't reset again
tapAt(102000);assert(files[path]==before) -- previous tap expired
tapAt(102200);assert(files[path]~=before)
before=files[path]
tapAt(103000);tapAt(103001);assert(files[path]==before) -- duplicate/bounce
files["/proc/uptime"]="103.1";gstart(103100);gend(103150,true)
tapAt(103200);tapAt(103300);assert(files[path]==before) -- swipe cancels and suppresses trailing taps
tapAt(104000);assert(files[path]==before)
tapAt(104200);assert(files[path]~=before)
print("Double tap: single/late/triple/bounce taps and swipe cancellation passed")
end
local function testGestures()
-- Slow swipe commits early and remains continuous, in either direction.
gstart(1000);gmove(1100,-25)
assert(files[path]:find("0.100000000",1,true))
gmove(1150,10);gend(1200)
assert(files[path]:find("0.060000000",1,true))
bindings["CTRL + Down"].callback()
assert(files[path]:match("3 2\n$"))
-- Reload preserves cumulative zoom and disables the timer from the previous chunk.
dofile("config/xr-controls.lua")
assert(firstTimer.enabled==false)
assert(omarchy_xr_controls.hover_timer and omarchy_xr_controls.hover_timer~=firstTimer and omarchy_xr_controls.hover_timer.enabled)
gstart(2000);gmove(2100,-10);gend(2200)
assert(files[path]:find("0.100000000",1,true))
local before=files[path]
gstart(3000);gmove(3060,-60);assert(files[path]==before);gend(3100)
assert(files[path]:find("0.100000000",1,true) and files[path]:match("2\n$"))
-- Fast down fits the workspace; a held fast start becomes continuous zoom instead.
gstart(4000);gmove(4050,60);gend(4100);assert(files[path]:match("1\n$"))
gstart(5000);gmove(5050,-60);gmove(5250,-10);gend(5300)
assert(files[path]:find("0.380000000",1,true))
before=files[path];gstart(6000);gmove(6050,-60);gend(6100,true);assert(files[path]==before)
testDoubleTap()
gazeTimer=omarchy_xr_controls.hover_timer
assert(gazeTimer and gazeTimer.enabled)
now=104;omarchy_xr_controls.refresh()
assert(not bindings["CTRL + Up"].enabled and not bindings["mouse:274"].enabled and gestures[#gestures].action=="unset")
assert(omarchy_xr_controls.hover_timer==nil and gazeTimer.enabled==false)
local previous=files[path]
bindings["CTRL + Up"].callback()
assert(files[path]==previous)
print("Live swipe direction, fit bindings, reload continuity and crash expiry passed")
end

local function holdStill(readHover, cursor, movements, focuses)
    for i=1,300 do readHover(1,nil,.8,.2) end
    assert(cursor.x==20)
    for i=1,20 do cursor={x=cursor.x+1,y=cursor.y};readHover(1) end
    assert(cursor.x==40 and #movements==0 and #focuses==1)
    return cursor
end
local function repeatSample(sample, count, name)
    for i=1,count do sample(1, name) end
end
local function testHalo()
-- Halo transitions select workspaces, never continuously steer the mouse.
local cursor={x=20,y=30}
local movements,focuses={},{}
hl.get_cursor_pos=function()return cursor end
hl.get_monitors=function()return {
 {name="OMXR-test-1",x=2020,y=100,width=1920,height=1080,scale=1,transform=0,active_workspace={id=5,config_name="5"}},
 {name="OMXR-test-2",x=3940,y=100,width=1920,height=1080,scale=1,transform=0,active_workspace={id=-1337,config_name="name:work"}}
} end
hl.dsp={focus=function(spec)return spec end,cursor={move=function(point)return point end}}
hl.dispatch=function(spec)
 if spec.workspace then focuses[#focuses+1]=spec.workspace
 else cursor=spec;movements[#movements+1]=spec end
end
now=105;files["/proc/uptime"]="105";files[path..".active"]="42 105";omarchy_xr_controls.refresh()
local liveTimer=omarchy_xr_controls.hover_timer
assert(liveTimer and liveTimer.enabled and liveTimer~=gazeTimer)
local serial=0
local function sample(mode,name,u,v)
 serial=serial+1
 files[path..".hover"]=string.format("42 %d %d %s %g %g",serial,mode,name or "OMXR-test-1",u or .5,v or .5)
 omarchy_xr_controls.hover()
end
sample(1);assert(#focuses==0) -- don't replay a pre-reload sample
sample(1);assert(#focuses==1 and focuses[1]=="5") -- same timing as halo, no dwell
cursor=holdStill(sample, cursor, movements, focuses)
assert(#focuses==1 and #movements==0)
-- A v3 line carries a dwell serial: the pointer warps once per serial, to the monitor's global
-- pixel, and the window under it is focused. Repeated samples with the same serial do nothing.
local windowFocuses={}
hl.get_windows=function(filter) return {
 {address="0xa",at={x=2020,y=100},size={x=960,y=1080},mapped=true,active=false,floating=false},
 {address="0xb",at={x=2980,y=100},size={x=960,y=1080},mapped=true,active=true,floating=false}} end
local dispatch=hl.dispatch
hl.dispatch=function(spec)
 if spec.window then windowFocuses[#windowFocuses+1]=spec.window.address else dispatch(spec) end
end
local function dwell(pointerSerial,px,py,name)
 serial=serial+1
 files[path..".hover"]=string.format("v3 42 %d 1 %s 0 0 %d %g %g",serial,name or "OMXR-test-1",pointerSerial,px,py)
 omarchy_xr_controls.hover()
end
-- The hovered XR monitor's most recently focused mapped window (lowest focus history index) goes
-- to the pane mailbox, once per change; the globally active window (on the laptop) plays no part.
local paneWindows={
 {address="0xa",at={x=2120,y=150},size={x=800,y=600},mapped=true,active=false,floating=false,focus_history_id=4},
 {address="0xb",at={x=2980,y=100},size={x=960,y=1080},mapped=true,active=false,floating=false,focus_history_id=2}}
local windowsBefore=hl.get_windows
hl.get_windows=function(filter) if filter and filter.monitor=="OMXR-test-1" then return paneWindows end return {} end
hl.get_active_window=function() return {monitor={name="eDP-1",x=0,y=0},at={x=10,y=10},size={x=100,y=100}} end
sample(1,"OMXR-test-1")
local paneSerial=files[path..".pane"]:match("^v1 42 (%d+) OMXR%-test%-1 960 0 960 1080 %d+");assert(paneSerial)
omarchy_xr_controls.hover();assert(files[path..".pane"]:match("^v1 42 "..paneSerial.." "))   -- unchanged: not rewritten
paneWindows[1].focus_history_id=1
omarchy_xr_controls.hover();assert(files[path..".pane"]:match("^v1 42 "..(paneSerial+1).." OMXR%-test%-1 100 50 800 600 "))
sample(1,"OMXR-test-2");assert(files[path..".pane"]:match("^v1 42 "..(paneSerial+2).." %- %d+"))  -- no windows there
sample(1,"OMXR-test-1")
hl.get_windows=windowsBefore
dwell(0,0,0);assert(#movements==0)                        -- no dwell yet
dwell(1,100,200);assert(#movements==1 and movements[1].x==2120 and movements[1].y==300 and windowFocuses[1]=="0xa")
dwell(1,100,200);dwell(1,150,220);assert(#movements==1)  -- same serial: once
dwell(2,1500,200);assert(#movements==2 and movements[2].x==3520 and #windowFocuses==1) -- window 0xb is already active
hl.dispatch=dispatch
sample(1,"OMXR-test-2");assert(#focuses==4 and focuses[4]=="name:work")
omarchy_xr_controls.hover();assert(#focuses==4) -- duplicate sample
sample(0);sample(1);assert(#focuses==5 and focuses[5]=="5") -- leave/re-enter
repeatSample(sample, 200, "eDP-1")
assert(#focuses==5)
now=110;files["/proc/uptime"]="110";omarchy_xr_controls.refresh()
assert(omarchy_xr_controls.hover_timer==nil and liveTimer.enabled==false)
repeatSample(sample, 200)
dwell(9,100,100)
assert(#focuses==5 and #movements==2) -- a stale session neither focuses nor warps
print("Halo transitions select existing workspaces once; a dwell warps the pointer once and focuses the window under it; stale sessions cannot focus")
end

local function testSettings()
-- Apply settings live: unregister old gesture and replace only XR bindings.
now=111;files["/proc/uptime"]="111";files[path..".active"]="42 111";omarchy_xr_controls.refresh()
files["/state/omarchy-xr/controls-settings.tsv"]="5\nALT + Up\nALT + Down\nCTRL + R\nCTRL + I\nCTRL + O\n"
omarchy_xr_controls.refresh()
assert(not bindings["CTRL + Up"] and bindings["ALT + Up"].enabled)
assert(omarchy_xr_controls.fingers==5 and gestures[#gestures].fingers==5)
assert(gestures[#gestures-1].fingers==3 and type(gestures[#gestures-1].action)=="table")
bindings["CTRL + I"].callback();assert(files[path]:match("4\n$"))
bindings["CTRL + O"].callback();assert(files[path]:match("5\n$"))
print("Live hotkey replacement and swipe finger count passed")
end

local function testPan()
local pan=omarchy_xr_controls.pan
pan.start({delta={x=2,y=3}});pan.update({delta={x=8,y=-5}})
assert(files[path..".pan"]:find("10.000000000 -2.000000000 1 111",1,true))
pan.finish({cancelled=false})
assert(files[path..".pan"]:find("10.000000000 -2.000000000 0 111",1,true))
local last=files[path..".pan"]
now=115;files["/proc/uptime"]="115";omarchy_xr_controls.refresh();pan.start({delta={x=20,y=20}})
assert(files[path..".pan"]==last)
local unset=false
for _,g in ipairs(gestures) do if g.fingers==4 and g.direction=="swipe" and g.action=="unset" then unset=true end end
assert(unset)
print("Four-finger pan: two axes, cumulative motion, release and expiry passed")
end
testActivation()
testGestures()
testHalo()
testSettings()
testPan()

local function testWorkspaceFocus()
    now=120;files["/proc/uptime"]="120";files[path..".active"]="42 120";omarchy_xr_controls.refresh()
    local ws={id=10,monitor={name="OMXR-test-2"},special=false}
    hl.get_active_workspace=function() return ws end
    local function event(name) events[name or "workspace.active"].callback(ws) end
    local function hover(seq,name,pointer)
        files[path..".hover"]=string.format("v3 42 %d 1 %s 0 0 %d 100 100",seq,name,pointer or 0)
        omarchy_xr_controls.hover()
    end
    local function packet(seq,name) return string.format("v1 42 %d %s 120\n",seq,name or ws.monitor.name) end
    event("monitor.focused");event() -- a single shortcut can emit both
    omarchy_xr_controls.hover();assert(files[path..".focus"]==packet(1))
    local dispatched=0
    hl.dispatch=function()
        dispatched=dispatched+1;event("monitor.focused");event()
    end
    hover(1000,"OMXR-test-1",1);hover(1001,"OMXR-test-1",2)
    assert(dispatched==0) -- old hover/pointer samples cannot undo the keyboard choice
    hover(1002,"OMXR-test-2",3)
    assert(dispatched==0) -- acknowledgment must not replay a pointer dwell
    hover(1003,"OMXR-test-1",4);omarchy_xr_controls.hover()
    assert(dispatched>0 and files[path..".focus"]==packet(1)) -- XR's own dispatch never fits
    event("monitor.focused");omarchy_xr_controls.hover()
    assert(files[path..".focus"]==packet(2)) -- already-visible workspace on another monitor
    ws={id=11,monitor={name="OMXR-test-2"}};event();omarchy_xr_controls.hover()
    assert(files[path..".focus"]==packet(3)) -- different workspace on the same monitor
    ws={id=12,monitor={name="OMXR-test-1"}};event()
    ws={id=13,monitor={name="OMXR-test-2"}};event();omarchy_xr_controls.hover()
    assert(files[path..".focus"]==packet(4)) -- rapid switching targets the final workspace
    local previous=files[path..".focus"]
    ws.special=true;event();omarchy_xr_controls.hover();assert(files[path..".focus"]==previous)
    ws.special=false;ws.monitor.active_special_workspace={id=-99}
    event("monitor.focused");omarchy_xr_controls.hover();assert(files[path..".focus"]==previous)
    ws={monitor={name="eDP-1"}};event();omarchy_xr_controls.hover();assert(files[path..".focus"]==previous)
    ws=nil;event();omarchy_xr_controls.hover();assert(files[path..".focus"]==previous)
    ws={id=10,monitor={name="OMXR-test-2"}}
    dofile("config/xr-controls.lua");event();omarchy_xr_controls.hover()
    assert(files[path..".focus"]==packet(5)) -- serial survives a live config reload
    previous=files[path..".focus"]
    now=125;files["/proc/uptime"]="125";event();omarchy_xr_controls.hover()
    assert(files[path..".focus"]==previous) -- expired renderer, even before the heartbeat timer fires
    print("Workspace focus: coalescing, gaze suppression, hover acknowledgment, reload and expiry passed")
end
testWorkspaceFocus()

local function testNotificationFlicks()
    now=130;files["/proc/uptime"]="130";files[path..".active"]="42 130";omarchy_xr_controls.refresh()
    files[path..".notification"]="v1 42 ab1234 130\n"
    local before=files[path]
    gstart(1000);gmove(1050,-60);assert(files[path]==before);gend(1100)
    assert(files[path]:match("^v3 42 .* 6 ab1234 130\n$"))
    gstart(2000);gmove(2050,60);gend(2100)
    assert(files[path]:match("^v3 42 .* 7 ab1234 130\n$"))
    before=files[path]
    gstart(3000);gmove(3050,-60);gend(3100,true);assert(files[path]==before)
    gstart(4000);gmove(4100,-10);gmove(4400,-80);gend(4500);assert(files[path]==before)
    gstart(5000);gmove(5050,-60);files[path..".notification"]="v1 42 cdef 130\n";gend(5100)
    assert(files[path]==before) -- cannot retarget halfway through a gesture
    files[path..".notification"]="v1 42 ab1234 127\n"
    gstart(6000);gmove(6050,-60);gend(6100);assert(files[path]==before) -- stale; three fingers are not zoom when set to five
    files[path..".notification"]="v1 42 ab1234 130\n"
    local five=gestures[#gestures].action
    five.start({time_ms=7000});five.update({time_ms=7050,delta={y=-60}});five.finish({time_ms=7100})
    assert(files[path]:match("^v2 .* 2\n$")) -- five fingers retain monitor fit even over an alert
    dofile("config/xr-controls.lua")
    gstart(8000);gmove(8050,-60);gend(8100);assert(files[path]:match("^v3 .* 6 ab1234 130\n$"))
    files["/state/omarchy-xr/controls-settings.tsv"]="3\nALT + Up\nALT + Down\nCTRL + R\nCTRL + I\nCTRL + O\n"
    omarchy_xr_controls.refresh();files[path..".notification"]="v1 42 - 130\n"
    gstart(9000);gmove(9050,60);gend(9100);assert(files[path]:match("^v2 .* 1\n$"))
    gstart(10000);gmove(10100,-10);gend(10250)
    assert(files[path]:match("^v2 ")) -- ordinary zoom still works after a notification action
    print("Notification flicks: gaze identity, up/down, cancellation, slow swipes, stale gaze, three/five fingers and reload passed")
end
testNotificationFlicks()

-- Window canvas (controls v6): a fake compositor with a canvas output, workspaces and windows.
local canvasMonitorName="OMXR-abcd1234-canvas"
local laptop={name="eDP-1",x=0,y=0,width=1920,height=1080,scale=1,active_workspace={id=1,name="1"}}
local canvasOutput={name=canvasMonitorName,x=20000,y=0,width=2560,height=1440,scale=1,active_workspace={id=20,name="omxr-canvas"}}
local WORKSPACES={["omxr-canvas"]=20,["omxr-park"]=21,["1"]=1,["3"]=3}
local windows,dispatched,cursorPos={}, {}, {x=100,y=100}
local function workspace(name) return {id=WORKSPACES[name],name=name,special=false} end
local function window(address,ws,fields)
    local w={address=address,class="foot",title="t "..address,size={x=800,y=600},at={x=20000,y=0},floating=true,
        focus_history_id=#windows,pid=100,xwayland=false,mapped=true,fullscreen=0,workspace=workspace(ws)}
    for key,value in pairs(fields or {}) do w[key]=value end
    windows[#windows+1]=w;return w
end
local function find(address)
    for _,w in ipairs(windows) do if w.address==address then return w end end
end
local function fire(name,...) if events[name] then events[name].callback(...) end end
local function tagged(kind) return function(spec) return {kind=kind,spec=spec} end end
local function applyWindow(kind,spec)
    local w=find((spec.window or ""):gsub("^address:",""))
    if not w then return end
    if kind=="window.move" and spec.workspace then w.workspace=workspace((spec.workspace:gsub("^name:","")));fire("window.move_to_workspace",w,w.workspace)
    elseif kind=="window.move" then w.at={x=spec.x,y=spec.y}
    elseif kind=="window.resize" then w.size={x=spec.x,y=spec.y}
    elseif kind=="window.float" then w.floating=spec.action=="float"
    elseif kind=="window.fullscreen_state" then w.fullscreen=spec.internal;fire("window.fullscreen",w) end
end
local function apply(d)
    local spec=d.spec
    if d.kind=="cursor.move" then cursorPos={x=spec.x,y=spec.y}
    elseif d.kind=="focus" and spec.window then fire("window.active",find(spec.window:gsub("^address:","")))
    elseif d.kind=="focus" and spec.workspace=="name:omxr-canvas" then canvasOutput.active_workspace={id=20,name="omxr-canvas"}
    elseif d.kind:match("^window%.") then applyWindow(d.kind,spec) end
end
local function installCanvasHl()
    hl.dsp={focus=tagged("focus"),cursor={move=tagged("cursor.move")},
        window={move=tagged("window.move"),resize=tagged("window.resize"),float=tagged("window.float"),set_prop=tagged("window.set_prop"),
            bring_to_top=tagged("window.bring_to_top"),fullscreen=tagged("window.fullscreen"),fullscreen_state=tagged("window.fullscreen_state"),
            cycle_next=tagged("window.cycle_next"),swap=tagged("window.swap"),drag=tagged("window.drag")},
        group={prev=tagged("group.prev"),next=tagged("group.next")},
        workspace={move=tagged("workspace.move"),toggle_special=tagged("workspace.toggle_special")}}
    hl.dispatch=function(d) dispatched[#dispatched+1]=d;apply(d) end
    hl.get_monitors=function() return {laptop,canvasOutput} end
    hl.get_windows=function() return windows end
    hl.get_window=function(selector) return find((selector:gsub("^address:",""))) end
    hl.get_cursor_pos=function() return cursorPos end
    hl.get_active_workspace=function() return nil end
end
local function clock(t,mode,owner,takeover)
    now=math.floor(t);files["/proc/uptime"]=tostring(t)
    files[path..".active"]=string.format("42 %d",math.floor(t))
    files[path..".mode"]=string.format("v1 %s %s %s %d\n",owner or "42",mode or "canvas",takeover or "1",math.floor(t))
end
local function tick(t) files["/proc/uptime"]=tostring(t);omarchy_xr_controls.hover() end
local function count(kind,address)
    local n=0
    for _,d in ipairs(dispatched) do
        if d.kind==kind and (not address or d.spec.window=="address:"..address) then n=n+1 end
    end
    return n
end
local function last(kind)
    for i=#dispatched,1,-1 do if dispatched[i].kind==kind then return dispatched[i].spec end end
end
local function unsetProps(address)
    local n=0
    for _,d in ipairs(dispatched) do
        if d.kind=="window.set_prop" and d.spec.window=="address:"..address and d.spec.value=="unset" then n=n+1 end
    end
    return n
end
local function lines(text)
    local out={}
    for line in text:gmatch("[^\n]+") do out[#out+1]=line end
    return out
end
local function row(address)
    for _,line in ipairs(lines(files[path..".windows"])) do
        if line:match("^"..address.." ") then return lines(line:gsub(" ","\n")) end
    end
end
local function seq(file) return tonumber(files[path..file]:match("^v1 42 (%d+) ")) end

-- Canvas key set (§5.5): search and pin always; the optional takeovers replace Omarchy's defaults and
-- give them back on exit (tiling.lua), including both ALT+TAB binds.
local DIRECTIONS={LEFT={"left","l","Focus on left window","Swap window to the left"},RIGHT={"right","r","Focus on right window","Swap window to the right"},
    UP={"up","u","Focus on above window","Swap window up"},DOWN={"down","d","Focus on below window","Swap window down"}}
local TAKEN={"SUPER + TAB","ALT + TAB","ALT + SHIFT + TAB","SUPER + mouse_down","SUPER + mouse_up"}
for key in pairs(DIRECTIONS) do TAKEN[#TAKEN+1]="SUPER + "..key;TAKEN[#TAKEN+1]="SUPER + SHIFT + "..key end
local ALWAYS,RELEASES={"SUPER + CTRL + G","SUPER + ALT + P","SUPER + CTRL + Page_Up","SUPER + CTRL + Page_Down"},{"ALT + ALT_L","ALT + ALT_R"}
local function hex(text) return (text:gsub(".",function(c) return ("%02x"):format(c:byte()) end)) end
local function isXR(chord) return bindings[chord]~=nil and bindings[chord].options.description:match("^XR: ")~=nil end
local function allXR(list,wanted) for _,chord in ipairs(list) do assert(isXR(chord)==wanted,chord) end end
local function press(chord)
    bindings[chord].callback()
    return files[path]:match("^v3 42 %d+ %S+ %d+ (%d+) (%S+) %d+\n$")
end
-- A fresh Omarchy config (the canvas activation test above already restored them once).
local function omarchyDefaults()
    for _,chord in ipairs(TAKEN) do hl.unbind(chord) end
    hl.bind("SUPER + TAB",hl.dsp.focus({workspace="e+1"}),{description="Next workspace"})
    hl.bind("SUPER + mouse_down",hl.dsp.focus({workspace="e+1"}),{description="Scroll active workspace forward"})
    hl.bind("SUPER + mouse_up",hl.dsp.focus({workspace="e-1"}),{description="Scroll active workspace backward"})
    hl.bind("ALT + TAB",hl.dsp.window.cycle_next(),{description="Focus on next window"})
    hl.bind("ALT + TAB",hl.dsp.window.bring_to_top(),{description="Reveal active window on top"})
    hl.bind("ALT + SHIFT + TAB",hl.dsp.window.cycle_next({next=false}),{description="Focus on previous window"})
    hl.bind("ALT + SHIFT + TAB",hl.dsp.window.bring_to_top(),{description="Reveal active window on top"})
    for key,d in pairs(DIRECTIONS) do
        hl.bind("SUPER + "..key,hl.dsp.focus({direction=d[2]}),{description=d[3]})
        hl.bind("SUPER + SHIFT + "..key,hl.dsp.window.swap({direction=d[2]}),{description=d[4]})
    end
end
local function assertOmarchy()
    local tab=bindings["SUPER + TAB"]
    assert(tab.options.description=="Next workspace");assert(tab.callback.kind=="focus");assert(tab.callback.spec.workspace=="e+1")
    for chord,wheel in pairs({["SUPER + mouse_down"]={"forward","e+1"},["SUPER + mouse_up"]={"backward","e-1"}}) do
        local b=bindings[chord];assert(#bindLists[chord]==1)
        assert(b.options.description=="Scroll active workspace "..wheel[1]);assert(b.callback.kind=="focus");assert(b.callback.spec.workspace==wheel[2])
    end
    for _,chord in ipairs({"ALT + TAB","ALT + SHIFT + TAB"}) do
        local list=bindLists[chord]
        assert(#list==2);assert(list[1].callback.kind=="window.cycle_next");assert(list[2].callback.kind=="window.bring_to_top")
        assert(list[2].options.description=="Reveal active window on top")
    end
    assert(bindLists["ALT + TAB"][1].options.description=="Focus on next window");assert(bindLists["ALT + TAB"][1].callback.spec==nil)
    assert(bindLists["ALT + SHIFT + TAB"][1].options.description=="Focus on previous window")
    assert(bindLists["ALT + SHIFT + TAB"][1].callback.spec.next==false)
    for key,d in pairs(DIRECTIONS) do
        local focus,swap=bindings["SUPER + "..key],bindings["SUPER + SHIFT + "..key]
        assert(#bindLists["SUPER + "..key]==1);assert(focus.options.description==d[3])
        assert(focus.callback.kind=="focus");assert(focus.callback.spec.direction==d[2])
        assert(swap.options.description==d[4]);assert(swap.callback.kind=="window.swap");assert(swap.callback.spec.direction==d[2])
    end
end
local function unboundSince(start,chord)
    for i=start+1,#unbound do if unbound[i]==chord then return true end end
    return false
end
local function testTakeoverPresses()
    for key,d in pairs(DIRECTIONS) do
        local mode,token=press("SUPER + "..key);assert(mode=="14");assert(token==hex(d[1]))
        mode,token=press("SUPER + SHIFT + "..key);assert(mode=="15");assert(token==hex(d[1]))
    end
    local mode,token=press("ALT + TAB");assert(mode=="11");assert(token=="-")
    mode,token=press("ALT + SHIFT + TAB");assert(mode=="12");assert(token=="-")
    for _,chord in ipairs(RELEASES) do
        assert(bindings[chord].options.release==true)
        mode,token=press(chord);assert(mode=="11");assert(token==hex("release"))
    end
    mode,token=press("SUPER + TAB");assert(mode=="8");assert(token=="-")
    mode=press("SUPER + CTRL + G");assert(mode=="9")
    mode=press("SUPER + ALT + P");assert(mode=="16")
end
local function testCanvasKeys()
    omarchyDefaults();assertOmarchy()
    local start=#unbound
    clock(146);omarchy_xr_controls.refresh()
    for _,chord in ipairs(TAKEN) do assert(unboundSince(start,chord),chord) end
    allXR(TAKEN,true);allXR(ALWAYS,true);allXR(RELEASES,true);assert(#bindLists["ALT + TAB"]==1)
    assert(bindings["SUPER + CTRL + G"].options.release==nil)
    testTakeoverPresses()
    clock(146,"monitors");omarchy_xr_controls.refresh()
    assertOmarchy();allXR(ALWAYS,false);allXR(RELEASES,false)
    assert(bindings["SUPER + F"].options.description=="Full screen")
    -- Flag 0: the optional chords stay Omarchy's; search, pin and fill are still bound.
    start=#unbound
    clock(147,"canvas",nil,"0");omarchy_xr_controls.refresh()
    for _,chord in ipairs(TAKEN) do assert(not unboundSince(start,chord),chord) end
    assertOmarchy();allXR(ALWAYS,true);allXR(RELEASES,false);assert(isXR("SUPER + F"))
    -- The flag flips live: takeovers install and retire without leaving the canvas.
    clock(147.5);omarchy_xr_controls.refresh();allXR(TAKEN,true);allXR(RELEASES,true)
    clock(148,"canvas",nil,"0");omarchy_xr_controls.refresh();assertOmarchy();allXR(ALWAYS,true);allXR(RELEASES,false)
    -- A config reload while live retires everything and re-enters with the takeover intact.
    clock(148.5);omarchy_xr_controls.refresh()
    local before=bindings["SUPER + LEFT"]
    dofile("config/xr-controls.lua")
    assert(bindings["SUPER + LEFT"]~=before);allXR(TAKEN,true);allXR(ALWAYS,true);allXR(RELEASES,true)
    assert(#bindLists["ALT + TAB"]==1);assert(#bindLists["SUPER + CTRL + G"]==1);assert(#omarchy_xr_canvas.takeovers==15)
    testTakeoverPresses()
    clock(149,"monitors");omarchy_xr_controls.refresh();assertOmarchy();allXR(ALWAYS,false)
    print("Canvas keys: search/pin always, takeovers with ALT release binds, restore of Omarchy's defaults, live flag and reload passed")
end

local function testCanvasActivation()
    installCanvasHl()
    hl.bind("SUPER + F",hl.dsp.window.fullscreen({mode="fullscreen"}),{description="Full screen"})
    clock(140,"canvas","43");omarchy_xr_controls.refresh()
    assert(bindings["SUPER + F"].options.description=="Full screen");assert(#unbound==0);assert(not events["window.open"]) -- foreign owner
    clock(140);omarchy_xr_controls.refresh()
    assert(omarchy_xr_controls.version==6);assert(files["/run/omarchy-xr/controls.version"]=="6\n")
    assert(unbound[1]=="SUPER + F");assert(bindings["SUPER + F"].options.description=="XR: fill window (canvas)")
    assert(events["window.open"]);assert(events["window.fullscreen"]);assert(events["window.move_to_workspace"])
    bindings["SUPER + F"].callback();assert(files[path]:match("^v3 42 %d+ %S+ %d+ 10 %- 140\n$"))
    -- A config reload while the canvas is live retires the old takeover and events and re-enters.
    local takeover,openEvent=bindings["SUPER + F"],events["window.open"]
    dofile("config/xr-controls.lua")
    assert(bindings["SUPER + F"]~=takeover);assert(bindings["SUPER + F"].options.description=="XR: fill window (canvas)")
    assert(events["window.open"]);assert(events["window.open"]~=openEvent);assert(#omarchy_xr_canvas.events==9)
    clock(140,"monitors");omarchy_xr_controls.refresh()
    local restored=bindings["SUPER + F"]
    assert(restored.options.description=="Full screen");assert(restored.callback.kind=="window.fullscreen");assert(restored.callback.spec.mode=="fullscreen")
    assert(not events["window.open"]);assert(#omarchy_xr_canvas.events==0)
    clock(140);omarchy_xr_controls.refresh();assert(bindings["SUPER + F"].options.description=="XR: fill window (canvas)")
    clock(145,"canvas");files[path..".active"]="42 140";omarchy_xr_controls.refresh()
    assert(bindings["SUPER + F"].options.description=="Full screen");assert(not events["window.open"]) -- expired renderer
    print("Canvas activation: .mode owner gate, SUPER+F takeover and restore, reload and expiry passed")
end

local function testPublishWindows()
    window("0xa","omxr-canvas");window("0xb","omxr-park");window("0xc","1",{floating=false,at={x=10,y=20}})
    window("0xd","omxr-park",{floating=false,size={x=1000,y=700}})
    window("0xe","omxr-park",{class="omarchy-xr-spectator"});window("0xf","omxr-park",{title="Omarchy XR"})
    window("0x10","omxr-park",{xdg_tag="helper"});window("0x11","special:scratch",{workspace={id=-98,name="special:scratch",special=true}})
    omarchy_xr_canvas.staged="0xa"
    clock(150);omarchy_xr_controls.refresh()
    assert(#lines(files[path..".windows"])==5)
    local header=lines(files[path..".windows"])[1]
    assert(header:match("^v1 42 %d+ 150 20000 0 OMXR%-abcd1234%-canvas$"))
    local a,b,c=row("0xa"),row("0xb"),row("0xc")
    assert(#a==13);assert(a[2]=="666f6f74");assert(a[3]=="74203078" .. "61");assert(a[9]=="stage");assert(a[13]=="1")
    assert(b[9]=="park");assert(b[13]=="1");assert(c[9]=="off");assert(c[13]=="0");assert(c[6]=="10");assert(c[7]=="20");assert(c[10]=="0")
    -- Non-floating members are floated and undecorated once; the origin records the tiled state first.
    assert(count("window.float","0xd")==1);assert(last("window.float").action=="float");assert(count("window.set_prop","0xd")==6)
    assert(count("window.float","0xc")==0);assert(omarchy_xr_canvas.origin["0xd"].floating==false)
    -- Two events coalesce into one write per 100 ms; an unchanged list is repeated every second.
    local first=seq(".windows")
    tick(150.02);assert(seq(".windows")==first)
    fire("window.title",find("0xa"));fire("window.urgent",find("0xb"))
    tick(150.05);assert(seq(".windows")==first)
    tick(150.11);assert(seq(".windows")==first+1)
    tick(150.5);assert(seq(".windows")==first+1)
    files["/proc/uptime"]="151.2";omarchy_xr_controls.refresh();assert(seq(".windows")==first+2)
    assert(count("window.set_prop","0xd")==6);assert(count("window.float","0xd")==1)
    print("Publish windows: header, rows, places, exclusions, enforcement once, 100 ms coalescing and heartbeat passed")
end
local function testWindowLimits()
    local before=#windows
    window("0x12","1",{title="a"..("\xc3\xa9"):rep(300)})
    for i=1,600 do window(string.format("0x%x",0x1000+i),"1") end
    clock(152);fire("window.open",nil);tick(152.2)
    local list=lines(files[path..".windows"])
    assert(#list==513);assert(row("0xd"));assert(row("0xa"))
    windows={table.unpack(windows,1,before+1)}
    fire("window.title",nil);tick(152.4)
    local title=row("0x12")[3]
    assert(#title==1098);assert(title:match("^61c3a9"));assert(title:match("c3a9$")) -- cut before a split character
    windows={table.unpack(windows,1,before)}
    fire("window.close",nil);tick(152.6)
    print("Publish windows: 512-row cap keeps canvas windows, 550-byte titles cut on a character boundary passed")
end

local hoverSerial=0
local function hoverV4(address,pointer,px,py)
    hoverSerial=hoverSerial+1
    files[path..".hover"]=string.format("v4 42 %d 1 %s 0.5 0.5 %d %g %g",hoverSerial,address,pointer,px,py)
    omarchy_xr_controls.hover()
end
local function testHoverV4Stage()
    clock(153)
    local focus=files[path..".focus"]
    hoverV4("0xa",5,10,10) -- the first sample of a session only records the pointer serial
    dispatched={}
    hoverV4("0xb",6,100,50)
    assert(count("window.move","0xa")==1);assert(find("0xa").workspace.name=="omxr-park")
    assert(find("0xb").workspace.name=="omxr-canvas");assert(find("0xb").at.x==20000);assert(find("0xb").at.y==0)
    assert(count("window.resize","0xb")==1);assert(count("window.bring_to_top","0xb")==1);assert(count("focus","0xb")==1)
    assert(cursorPos.x==20100);assert(cursorPos.y==50);assert(omarchy_xr_canvas.staged=="0xb")
    assert(files[path..".focus"]==focus) -- XR's own staging does not publish focus
    local n=#dispatched
    files[path..".hover"]=files[path..".hover"]:gsub("^v4 42 %d+","v4 42 "..(hoverSerial+1));hoverSerial=hoverSerial+1
    omarchy_xr_controls.hover();assert(#dispatched==n) -- same pointer serial: once
    canvasOutput.scale=2
    hoverV4("0xa",7,300,100)
    assert(find("0xb").workspace.name=="omxr-park");assert(find("0xa").workspace.name=="omxr-canvas")
    assert(cursorPos.x==20150);assert(cursorPos.y==50);assert(omarchy_xr_canvas.staged=="0xa")
    canvasOutput.scale=1
    tick(153.3);assert(row("0xa")[9]=="stage");assert(row("0xb")[9]=="park")
    print("Hover v4: stage, park, raise, focus and warp once per pointer serial, scale and restage passed")
end

local function cursorLine() return files[path..".cursor"]:match("^v1 42 %d+ (%S+ %S+ %S+ %S+) %d+\n$") end
local function testCursorConfinement()
    clock(154)
    cursorPos={x=20900,y=100};dispatched={};tick(154)
    assert(last("cursor.move").x==20798);assert(cursorPos.x==20798);assert(cursorLine()=="20798.0 100.0 102.0 0.0")
    local s=seq(".cursor")
    tick(154.1);assert(seq(".cursor")==s) -- unchanged
    cursorPos={x=20850,y=700};tick(154.2)
    assert(cursorPos.x==20798);assert(cursorPos.y==598);assert(cursorLine()=="20798.0 598.0 154.0 102.0")
    tick(154.7);assert(seq(".cursor")==s+2) -- refreshed while the pointer rests
    hoverV4("0xb",8,10,10);cursorPos={x=20400,y=300};tick(154.8)
    assert(cursorLine()=="20400.0 300.0 0.0 0.0") -- restage resets the overflow
    cursorPos={x=20900,y=300};tick(154.9);assert(cursorLine()=="20798.0 300.0 102.0 0.0")
    cursorPos={x=500,y=400};tick(155);assert(cursorLine()=="500.0 400.0 0.0 0.0");assert(cursorPos.x==500)
    -- The three-finger double tap recenters and hands the pointer back to the laptop.
    cursorPos={x=20300,y=300};clock(155.5)
    local tap=bindings["mouse:274"]
    files["/proc/uptime"]="160";tap.callback();files["/proc/uptime"]="160.2";tap.callback()
    assert(files[path]:match(" 3\n$"));assert(cursorPos.x==960);assert(cursorPos.y==540)
    print("Cursor confinement: clamp, cumulative overflow, rest refresh, restage/leave reset and tap release passed")
end

-- `.fill` resizes the staged window only, once per sequence number, and confinement follows the size.
local function fillLine(n,address,w,h,stamp,owner)
    files[path..".fill"]=string.format("v1 %s %d %s %d %d %d\n",owner or "42",n,address,w,h,stamp)
end
local function testFill()
    clock(160.5);omarchy_xr_controls.refresh()
    local staged=omarchy_xr_canvas.staged;assert(staged=="0xb");dispatched={}
    fillLine(1,staged,1800,1000,160);tick(160.5)
    assert(count("window.resize",staged)==1);assert(last("window.resize").x==1800);assert(last("window.resize").y==1000)
    assert(find(staged).size.x==1800)
    cursorPos={x=21900,y=1200};tick(160.6)
    assert(cursorPos.x==21798);assert(cursorPos.y==998);assert(count("window.resize")==1) -- a seen sequence is not replayed
    fillLine(1,staged,900,500,160);tick(160.7)
    fillLine(2,staged,900,500,160,"43");tick(160.7)
    fillLine(3,staged,900,500,150);tick(160.7)
    fillLine(4,"0xa",900,500,160);tick(160.7)
    assert(count("window.resize")==1) -- old sequence, foreign owner, stale stamp, not staged
    fillLine(5,staged,900,500,160);tick(160.8);assert(count("window.resize",staged)==2);assert(find(staged).size.y==500)
    dofile("config/xr-controls.lua");tick(160.9);assert(count("window.resize",staged)==2) -- the sequence survives a reload
    fillLine(6,staged,5000,3000,160);tick(160.9);assert(last("window.resize").x==2552);assert(last("window.resize").y==1440)
    fillLine(7,staged,800,600,160);tick(161)
    print("Fill: staged window resized once per sequence, confinement follows, stale/foreign/old/other ignored, reload passed")
end
local function testGuards()
    clock(161);dispatched={}
    canvasOutput.active_workspace={id=3,name="3"}
    fire("workspace.active");tick(161.1)
    assert(dispatched[1].kind=="focus");assert(dispatched[1].spec.workspace=="name:omxr-canvas")
    assert(last("workspace.move").workspace==3);assert(last("workspace.move").monitor=="eDP-1")
    -- A scratchpad opened on the canvas output closes there and opens on the laptop.
    canvasOutput.active_special_workspace={id=-98,name="special:scratchpad"};dispatched={}
    fire("monitor.focused");tick(161.2)
    assert(#dispatched==4);assert(dispatched[1].spec.workspace=="name:omxr-canvas");assert(dispatched[2].spec=="scratchpad")
    assert(dispatched[3].kind=="focus");assert(dispatched[3].spec.workspace=="1");assert(dispatched[4].spec=="scratchpad")
    canvasOutput.active_special_workspace=nil
    -- Fullscreen on a canvas window is reverted and becomes Fill.
    local a=find("0xa");a.fullscreen=2;fire("window.fullscreen",a)
    local state=last("window.fullscreen_state")
    assert(state.window=="address:0xa");assert(state.internal==0);assert(state.client==0);assert(a.fullscreen==0)
    assert(files[path]:match(" 10 %- 161\n$"))
    -- SUPER+SHIFT+n removes a window from the canvas and restores its origin: decorations, then the
    -- tiled state, or the floating size and position. The backend's journal wins over Lua's record.
    local d=find("0xd");assert(d.floating);dispatched={}
    hl.dispatch(hl.dsp.window.move({window="address:0xd",workspace="1"}))
    assert(last("window.float").window=="address:0xd");assert(last("window.float").action=="tile");assert(not d.floating)
    assert(not omarchy_xr_canvas.origin["0xd"]);assert(unsetProps("0xd")==6)
    -- The backend floated 0x30 before Lua saw it: only the journal knows it was tiled.
    local parked=window("0x30","omxr-park",{size={x=900,y=500}});fire("window.open",parked)
    assert(omarchy_xr_canvas.origin["0x30"].floating==true)
    local function entry(address,floating,at)
        return '    "'..address..'": {\n      "workspace": 2,\n      "floating": '..floating..',\n      "size": [\n        640,\n'..
            '        480\n      ],\n      "at": [\n        '..at..',\n        -40\n      ]\n    }'
    end
    files["/state/omarchy-xr/canvas-session.json"]='{\n  "session": "s",\n  "windows": {\n'..entry("0xb","true",300)..",\n"..
        entry("0x30","false",0).."\n  }\n}\n"
    hl.dispatch(hl.dsp.window.move({window="address:0x30",workspace="1"}))
    assert(not parked.floating);assert(unsetProps("0x30")==6)
    hl.dispatch(hl.dsp.window.move({window="address:0xb",workspace="1"}));assert(omarchy_xr_canvas.staged==nil)
    local b=find("0xb");assert(b.size.x==640);assert(b.size.y==480);assert(b.at.x==300);assert(b.at.y==-40);assert(b.floating)
    files["/state/omarchy-xr/canvas-session.json"]=nil
    hl.dispatch(hl.dsp.window.move({window="address:0xc",workspace="name:omxr-canvas"}))
    assert(find("0xc").workspace.name=="omxr-park");assert(omarchy_xr_canvas.origin["0xc"].floating==false) -- adopted
    -- New windows are adopted: a small window of a canvas process is staged, others are parked.
    local dialog=window("0x20","1",{size={x=300,y=200},pid=100});fire("window.open",dialog)
    assert(omarchy_xr_canvas.staged=="0x20");assert(dialog.workspace.name=="omxr-canvas");assert(dialog.at.x==20000)
    local other=window("0x21","1",{pid=555});fire("window.open",other);assert(other.workspace.name=="omxr-park")
    print("Guards: workspace and special intruders, fullscreen to Fill, remove/adopt on move, dialog staging passed")
end
local function testAdoptPolicyEmpty()
    files["/state/omarchy-xr/canvas.tsv"]="# canvas v1 60 2.4 60 0.35 0.8 1 300 empty 0\n"
    clock(162,"monitors");omarchy_xr_controls.refresh();clock(162);omarchy_xr_controls.refresh()
    local laptopWindow=window("0x22","1",{pid=777});fire("window.open",laptopWindow)
    assert(laptopWindow.workspace.name=="1");assert(not omarchy_xr_canvas.origin["0x22"])
    local onCanvas=window("0x23","omxr-canvas",{pid=778});fire("window.open",onCanvas)
    assert(onCanvas.workspace.name=="omxr-park")
    files["/state/omarchy-xr/canvas.tsv"]=nil
    print("Adopt policy empty: laptop windows stay, windows opened on the canvas are parked passed")
end
-- canvas.tsv exclusions (class or pid): never adopted nor undecorated; one that opens on the canvas
-- output goes to the laptop's workspace.
local function testExclusions()
    files["/state/omarchy-xr/canvas.tsv"]="# canvas v1 60 2.4 60 0.35 0.8 1 300 all\nexclude firefox\nexclude 999\n"
    clock(162.5,"monitors");omarchy_xr_controls.refresh();clock(162.5);omarchy_xr_controls.refresh();dispatched={}
    local browser=window("0x24","1",{class="firefox",pid=901});fire("window.open",browser)
    assert(browser.workspace.name=="1");assert(not omarchy_xr_canvas.origin["0x24"])
    local studio=window("0x25","omxr-canvas",{pid=999});fire("window.open",studio)
    assert(studio.workspace.name=="1");assert(not omarchy_xr_canvas.origin["0x25"])
    hl.dispatch(hl.dsp.window.move({window="address:0x24",workspace="name:omxr-park"}))
    assert(not omarchy_xr_canvas.origin["0x24"]);assert(browser.workspace.name=="1")
    tick(162.7);assert(count("window.set_prop","0x24")==0);assert(count("window.set_prop","0x25")==0)
    local kept=window("0x26","1",{pid=902});fire("window.open",kept);assert(kept.workspace.name=="omxr-park")
    files["/state/omarchy-xr/canvas.tsv"]=nil
    print("Exclusions: excluded classes and pids stay on the laptop, undecorated passed")
end
local function testFocusAddress()
    clock(163)
    local focus=files[path..".focus"]
    fire("window.active",find("0x21"));tick(163.1)
    local serial=tonumber(files[path..".focus"]:match("^v1 42 (%d+) 0x21 163\n$"))
    assert(serial);assert(files[path..".focus"]~=focus)
    fire("window.active",find("0x21"));tick(163.2);assert(seq(".focus")==serial) -- deduplicated
    fire("window.active",find("0x22"));tick(163.3);assert(seq(".focus")==serial) -- not a canvas window
    fire("window.active",find("0x21"));tick(163.4);assert(seq(".focus")==serial+1)
    print("Address focus: window.active publishes canvas windows once per change passed")
end
local function testSettingsUnchangedInCanvas()
    clock(164)
    local pane=files[path..".pane"]
    files["/state/omarchy-xr/controls-settings.tsv"]="4\nSUPER + Up\nSUPER + Down\nCTRL + R\nCTRL + I\nCTRL + O\n"
    omarchy_xr_controls.refresh()
    assert(bindings["SUPER + Up"].enabled);assert(not bindings["ALT + Up"]);assert(omarchy_xr_controls.fingers==3)
    assert(bindings["SUPER + F"].options.description=="XR: fill window (canvas)");assert(isXR("SUPER + CTRL + G"));assert(isXR("SUPER + LEFT"))
    bindings["CTRL + I"].callback();assert(files[path]:match("^v2 42 .* 4\n$"))
    tick(164.1);assert(files[path..".pane"]==pane) -- no pane publishing in canvas mode
    local members={}
    for address in pairs(omarchy_xr_canvas.origin) do members[#members+1]=address end
    assert(#members>0);dispatched={}
    clock(170,"monitors");files[path..".active"]="42 160";omarchy_xr_controls.refresh()
    assert(bindings["SUPER + F"].options.description=="Full screen");assert(not events["window.open"])
    assert(not bindings["SUPER + CTRL + G"]);assertOmarchy()
    for _,address in ipairs(members) do assert(unsetProps(address)==6) end -- leaving restores decorations
    assert(next(omarchy_xr_canvas.origin)==nil)
    print("Canvas mode keeps the 5-key settings file and remaps existing bindings passed")
end
-- `.tiers` (M5): the renderer's sliver set; slivers sit 8 px inside the right edge with no_follow_mouse.
local function tiersFile(n,stamp,owner,list)
    local line=string.format("v1 %s %d %d",owner or "42",n,stamp)
    for _,address in ipairs(list or {}) do line=line.." "..address.." sliver" end
    files[path..".tiers"]=line.."\n"
end
local function followProp(address,value)
    local n=0
    for _,d in ipairs(dispatched) do
        local spec=d.spec
        if d.kind=="window.set_prop" and spec.prop=="no_follow_mouse" and spec.window=="address:"..address and spec.value==value then n=n+1 end
    end
    return n
end
local function moves(address)
    local n=0
    for _,d in ipairs(dispatched) do if d.kind=="window.move" and d.spec.workspace and d.spec.window=="address:"..address then n=n+1 end end
    return n
end
local function windowDispatches()
    local n=0
    for _,d in ipairs(dispatched) do if d.kind:match("^window%.") then n=n+1 end end
    return n
end
local function lastIndex(kind,address)
    for i=#dispatched,1,-1 do if dispatched[i].kind==kind and dispatched[i].spec.window=="address:"..address then return i end end
    return 0
end
local function testTiersSliver()
    files[path..".tiers"]=nil;cursorPos={x=500,y=400}
    windows={};window("0xa","omxr-canvas");window("0xb","omxr-park");window("0xc","omxr-park")
    clock(180);omarchy_xr_controls.refresh();omarchy_xr_canvas.staged="0xa";dispatched={}
    tiersFile(1,180,"43",{"0xb"});tick(180)                  -- foreign owner
    tiersFile(1,170,nil,{"0xb"});tick(180.6)                 -- stale stamp
    files[path..".tiers"]="v1 42 x 180 0xb sliver\n";tick(181.2) -- malformed
    assert(windowDispatches()==0)
    tiersFile(1,181,nil,{"0xb"});tick(181.3)
    local b=find("0xb")
    assert(b.workspace.name=="omxr-canvas");assert(followProp("0xb","1")==1);assert(b.at.x==20000+2560-8);assert(b.at.y==0)
    assert(lastIndex("window.bring_to_top","0xa")>lastIndex("window.move","0xb"));assert(count("window.bring_to_top","0xb")==0)
    assert(row("0xb")[9]=="sliver");assert(row("0xb")[13]=="1");assert(row("0xa")[9]=="stage")
    local n=windowDispatches()
    tick(181.9)                                              -- the same line again
    tiersFile(1,182,nil,{"0xb"});tick(182.5)                 -- heartbeat: same seq, fresh stamp
    tiersFile(0,182,nil,{"0xb","0xc"});tick(183)             -- older seq
    tiersFile(2,183,"43",{"0xb","0xc"});tick(183.6)          -- foreign owner
    assert(windowDispatches()==n);assert(omarchy_xr_canvas.staged=="0xa")
    tiersFile(2,184,nil,{"0xc","0xb"});tick(184)
    local c=find("0xc");assert(c.workspace.name=="omxr-canvas");assert(c.at.x==22552);assert(c.at.y==24)
    assert(followProp("0xc","1")==1);assert(followProp("0xb","1")==1);assert(moves("0xb")==1)
    print("Tiers: sliver move, no_follow_mouse once, stage on top, .windows place, rejected lines, 24 px stacking passed")
end
local function testTiersPark()
    dispatched={}
    tiersFile(3,185,nil,{"0xc"});tick(185)
    local b=find("0xb");assert(b.workspace.name=="omxr-park");assert(followProp("0xb","unset")==1);assert(moves("0xb")==1)
    assert(moves("0xc")==0);assert(row("0xb")[9]=="park");assert(not omarchy_xr_canvas.slivers["0xb"])
    files[path..".tiers"]=nil;tick(185.6)                    -- no file: no slivers
    assert(find("0xc").workspace.name=="omxr-park");assert(followProp("0xc","unset")==1)
    tick(186.2);tick(186.8);assert(moves("0xc")==1);assert(moves("0xb")==1) -- once per transition
    tiersFile(4,187,nil,{"0xb"});tick(187);assert(moves("0xb")==2);assert(find("0xb").at.y==0)
    tiersFile(5,187,nil,{"0xb","0xc"});tick(187.1)           -- two lines 100 ms apart ...
    tiersFile(6,187,nil,{"0xc"});tick(187.2)
    assert(moves("0xb")==2);assert(moves("0xc")==1)
    tick(187.6)                                              -- ... apply once, with the newest set
    assert(moves("0xb")==3);assert(moves("0xc")==2);assert(find("0xb").workspace.name=="omxr-park");assert(find("0xc").at.y==0)
    tick(188.2);assert(moves("0xb")==3);assert(moves("0xc")==2)
    tiersFile(6,186,nil,{"0xc"});tick(188.5)                 -- a renderer hitch: the same line goes stale ...
    assert(moves("0xc")==3);assert(find("0xc").workspace.name=="omxr-park")
    tiersFile(6,188,nil,{"0xc"});tick(189)                   -- ... and its heartbeat brings the sliver back
    assert(moves("0xc")==4);assert(find("0xc").workspace.name=="omxr-canvas");assert(omarchy_xr_canvas.slivers["0xc"])
    print("Tiers: sliver back to park, no_follow_mouse unset, missing file, one move per transition, 0.5 s rate limit, stale then heartbeat passed")
end
local function testStageBand()
    clock(189);tiersFile(7,189,nil,{"0xc"});omarchy_xr_controls.refresh();dispatched={}
    window("0xd","omxr-park",{size={x=2600,y=1400}})
    hoverV4("0xa",20,1,1);hoverV4("0xd",21,10,10)
    local d=find("0xd");assert(omarchy_xr_canvas.staged=="0xd");assert(d.size.x==2552);assert(d.size.y==1400)
    assert(find("0xa").workspace.name=="omxr-park")         -- not in the set: parked
    fillLine(8,"0xd",2600,900,189);tick(189.1);assert(d.size.x==2552);assert(d.size.y==900)
    tiersFile(8,189,nil,{"0xc","0xd"});tick(189.2)
    local n=windowDispatches();tick(189.8);assert(windowDispatches()==n) -- the staged window is never touched
    hoverV4("0xc",22,10,10)
    assert(omarchy_xr_canvas.staged=="0xc");assert(followProp("0xc","unset")==1);assert(not omarchy_xr_canvas.slivers["0xc"])
    assert(d.workspace.name=="omxr-canvas");assert(followProp("0xd","1")==1);assert(omarchy_xr_canvas.slivers["0xd"])
    assert(d.at.x==22552);assert(d.at.y==0)
    tick(190.4);assert(row("0xd")[9]=="sliver");assert(row("0xc")[9]=="stage");assert(moves("0xd")==1) -- staged there, not moved again
    print("Stage band: output minus the strip for staging and Fill, a staged sliver loses no_follow_mouse, a demoted window in the set becomes a sliver passed")
end
local function testTiersLeave()
    clock(191);omarchy_xr_controls.refresh();dispatched={}
    hl.dispatch(hl.dsp.window.move({window="address:0xd",workspace="1"})) -- SUPER+SHIFT+n on a sliver
    assert(unsetProps("0xd")==7);assert(followProp("0xd","unset")==1);assert(not omarchy_xr_canvas.slivers["0xd"])
    tick(191.1);assert(moves("0xd")==1)                     -- off the canvas: left alone
    tiersFile(9,191,nil,{"0xb","0xd"});tick(191.6);assert(omarchy_xr_canvas.slivers["0xb"]);assert(moves("0xd")==1)
    dofile("config/xr-controls.lua")
    assert(omarchy_xr_canvas.slivers["0xb"]);assert(omarchy_xr_canvas.tiersSeq==9)
    local n=moves("0xb");tick(192.2);assert(moves("0xb")==n)
    dispatched={}
    clock(192.5,"monitors");omarchy_xr_controls.refresh()
    assert(followProp("0xb","unset")==1);assert(unsetProps("0xb")==7);assert(next(omarchy_xr_canvas.slivers)==nil)
    files[path..".tiers"]=nil
    print("Tiers leave: release, reload keeps the set, leaving the canvas unsets no_follow_mouse and the props passed")
end
-- On a short output the stack stops SLIVER_FLOOR (64) px above the bottom edge.
local function testSliverFloor()
    local height=canvasOutput.height;canvasOutput.height=100
    windows={};window("0xa","omxr-canvas");window("0xb","omxr-park");window("0xc","omxr-park");window("0xe","omxr-park")
    clock(194);omarchy_xr_controls.refresh();omarchy_xr_canvas.staged="0xa";dispatched={}
    tiersFile(10,194,nil,{"0xb","0xc","0xe"});tick(194.6)
    assert(find("0xb").at.y==0);assert(find("0xc").at.y==24);assert(find("0xe").at.y==36)
    canvasOutput.height=height;files[path..".tiers"]=nil
    print("Sliver stack: clamped 64 px above a short output's bottom edge passed")
end
testCanvasActivation()
testCanvasKeys()
testPublishWindows()
testWindowLimits()
testHoverV4Stage()
testCursorConfinement()
testFill()
testGuards()
testAdoptPolicyEmpty()
testExclusions()
testFocusAddress()
testSettingsUnchangedInCanvas()
testTiersSliver()
testTiersPark()
testStageBand()
testTiersLeave()
testSliverFloor()

-- M7: confirm, auto-staging, the SUPER+left-drag takeover, resize keys and the stage re-clamp.
files["/state/omarchy-xr/controls-settings.tsv"]="3\nCTRL + Up\nCTRL + Down\n\n\n\n"
local DRAG="SUPER + mouse:272"
local function testConfirmHotkey()
    clock(200);omarchy_xr_controls.refresh()
    bindings["CTRL + Down"].callback();assert(files[path]:match("^v3 42 %d+ %S+ %d+ 18 %- 200\n$"))
    omarchy_xr_controls.fit_target();assert(files[path]:match(" 18 %- 200\n$"))
    bindings["CTRL + Up"].callback();assert(files[path]:match("^v2 .* 1\n$"))
    gstart(20000);gmove(20050,-60);gend(20100);assert(files[path]:match("^v2 .* 2\n$")) -- the flick-in keeps mode 2
    clock(200.5,"monitors");omarchy_xr_controls.refresh()
    bindings["CTRL + Down"].callback();assert(files[path]:match("^v2 .* 2\n$"))
    omarchy_xr_controls.fit_target();assert(files[path]:match("^v2 .* 2\n$"))
    print("Confirm hotkey: fit_target publishes 18 in canvas mode, 2 in monitor mode; the flick keeps 2 passed")
end
local function testTapConfirm()
    clock(201);omarchy_xr_controls.refresh()
    local tap=bindings["mouse:274"]
    local before=files[path]
    files["/proc/uptime"]="201.5";tap.callback();assert(files[path]==before)
    tick(201.7);assert(files[path]==before)                  -- a second tap may still follow
    tick(201.95);assert(files[path]:match("^v3 42 .* 18 %- 201\n$"))
    before=files[path];tick(202.5);assert(files[path]==before) -- once
    files["/proc/uptime"]="203";tap.callback();files["/proc/uptime"]="203.2";tap.callback()
    assert(files[path]:match("^v2 .* 3\n$"));before=files[path]
    tick(203.8);assert(files[path]==before)                  -- a double tap is no confirm
    files["/proc/uptime"]="204";tap.callback();gstart(204100);gend(204150,true)
    tick(204.6);assert(files[path]==before)                  -- a swipe cancels the tap
    clock(205,"monitors");omarchy_xr_controls.refresh();before=files[path]
    files["/proc/uptime"]="205.5";tap.callback();tick(206);assert(files[path]==before)
    print("Tap confirm: single tap confirms after 400 ms once, double tap and swipe do not, monitor mode ignores it passed")
end
local function kindsFor(address)
    local out={}
    for _,d in ipairs(dispatched) do if d.spec and d.spec.window=="address:"..address then out[#out+1]=d.kind..(d.spec.workspace and "@ws" or "") end end
    return table.concat(out," ")
end
local function testEnsureStaged()
    windows={};omarchy_xr_canvas.staged=nil;cursorPos={x=500,y=400}
    window("0x3f","omxr-park",{focus_history_id=0,class="omarchy-xr-spectator"}) -- never staged, even as the MRU
    window("0x40","omxr-park",{focus_history_id=3});window("0x41","omxr-park",{focus_history_id=1,at={x=20500,y=300}})
    window("0x42","omxr-park",{focus_history_id=7});window("0x43","1",{focus_history_id=0})
    clock(210);omarchy_xr_controls.refresh();dispatched={}
    tick(210)
    assert(omarchy_xr_canvas.staged=="0x41");local b=find("0x41")
    assert(b.workspace.name=="omxr-canvas");assert(b.at.x==20000);assert(b.at.y==0)
    assert(kindsFor("0x41")=="window.move@ws window.resize window.move window.bring_to_top")
    assert(count("focus")==0);assert(count("cursor.move")==0) -- quiet: keyboard and pointer stay
    tick(210.2);assert(row("0x41")[9]=="stage");assert(row("0x40")[9]=="park")
    for i,w in ipairs(windows) do if w==b then table.remove(windows,i);break end end
    fire("window.close",b);assert(omarchy_xr_canvas.staged==nil)
    tick(210.4);assert(omarchy_xr_canvas.staged==nil)       -- at most every 0.5 s
    tick(210.6);assert(omarchy_xr_canvas.staged=="0x40");assert(count("focus","0x40")==0)
    local n=windowDispatches();tick(211.2);assert(windowDispatches()==n) -- a live staged window stays
    windows={};window("0x44","1");omarchy_xr_canvas.staged=nil;dispatched={}
    tick(211.8);assert(windowDispatches()==0);assert(omarchy_xr_canvas.staged==nil)
    print("Ensure staged: the MRU member is staged quietly, the next one after a close, nothing without members passed")
end
local function dragFields() return files[path..".drag"]:match("^v2 42 %d+ (%d+) (%S+) (%S+) ([01]) %d+\n$") end
local function testStageDrag()
    windows={};window("0x50","omxr-canvas");window("0x51","omxr-park");omarchy_xr_canvas.staged="0x50"
    clock(219.5,"monitors");omarchy_xr_controls.refresh();assert(bindings[DRAG].callback.kind=="window.drag")
    clock(220);omarchy_xr_controls.refresh();tick(220);assert(omarchy_xr_canvas.staged=="0x50") -- staged again on entry
    local list=bindLists[DRAG];assert(#list==2);assert(list[1].options.description:match("^XR: "));assert(list[2].options.release==true)
    dispatched={};cursorPos={x=20100,y=100};files["/proc/uptime"]="220";list[1].callback()
    assert(files[path..".drag"]:match("^v2 42 %d+ 1 0%.0+ 0%.0+ 1 220\n$"))
    cursorPos={x=20140,y=130};tick(220.1)
    local id,dx,dy,held=dragFields();assert(id=="1");assert(tonumber(dx)==40);assert(tonumber(dy)==30);assert(held=="1")
    cursorPos={x=21000,y=130};tick(220.2)                     -- confined: the overflow carries the travel
    assert(cursorPos.x==20798);assert(select(2,dragFields())=="900.000000000");assert(select(3,dragFields())=="30.000000000")
    local plain=bindings["mouse:272"];assert(plain.options.release==true) -- SUPER let go first: the plain release ends it
    assert(plain.options.description:match("^XR: "));plain.callback()
    assert(select(4,dragFields())=="0");assert(select(2,dragFields())=="900.000000000");assert(bindings["mouse:272"]==nil)
    list[1].callback();assert(bindings["mouse:272"]);list[2].callback();assert(bindings["mouse:272"]==nil)
    assert(select(4,dragFields())=="0")
    assert(count("window.move","0x50")==0);assert(find("0x50").at.x==20000) -- the real window never moves
    dofile("config/xr-controls.lua");assert(#bindLists[DRAG]==2)  -- a reload retires the old binds
    cursorPos={x=20300,y=300};bindLists[DRAG][1].callback();assert(dragFields()=="3");assert(select(4,dragFields())=="1")
    clock(221,"monitors");omarchy_xr_controls.refresh();assert(select(4,dragFields())=="0") -- leaving ends the drag
    assert(bindings["mouse:272"]==nil)
    local restored=bindings[DRAG];assert(#bindLists[DRAG]==1);assert(restored.callback.kind=="window.drag")
    assert(restored.options.mouse==true);assert(restored.options.description=="Move window")
    print("Stage drag: SUPER+left-drag taken over, cumulative travel with overflow, release, the plain-release fallback, reload, restore of window.drag passed")
end
local function testResizeKeys()
    clock(222);omarchy_xr_controls.refresh();tick(222);assert(omarchy_xr_canvas.staged=="0x50");dispatched={}
    local function key(name,times) for _=1,times or 1 do bindings["SUPER + CTRL + "..name].callback() end return last("window.resize") end
    assert(key("RIGHT").x==900);assert(last("window.resize").y==600);assert(key("DOWN").y==700);assert(key("UP").y==600)
    assert(key("LEFT",12).x==100);tick(222.2);assert(row("0x50")[4]=="100");assert(row("0x50")[5]=="600")
    assert(key("RIGHT",40).x==2552);assert(key("DOWN",20).y==1440);assert(key("UP",20).y==100)
    clock(223,"monitors");omarchy_xr_controls.refresh()
    assert(bindings["SUPER + CTRL + UP"]==nil);assert(bindings["SUPER + CTRL + DOWN"]==nil)
    local left,right=bindings["SUPER + CTRL + LEFT"],bindings["SUPER + CTRL + RIGHT"]
    assert(left.callback.kind=="group.prev");assert(left.options.description=="Move grouped window focus left")
    assert(right.callback.kind=="group.next");assert(right.options.description=="Move grouped window focus right")
    print("Resize keys: 100 px steps clamped to 100 px and the band, Omarchy's group keys back on exit passed")
end
-- M8 scroll (mode 19): the wheel is a takeover, SUPER+CTRL+Page_Up/Down are always bound in canvas mode.
local testScrollKeys
do
    local function wheelIsOmarchy()
        assert(bindings["SUPER + mouse_up"].options.description=="Scroll active workspace backward")
        assert(bindings["SUPER + mouse_down"].options.description=="Scroll active workspace forward")
        assert(bindings["SUPER + mouse_up"].callback.spec.workspace=="e-1");assert(bindings["SUPER + mouse_down"].callback.spec.workspace=="e+1")
    end
    testScrollKeys=function()
        clock(222.5);omarchy_xr_controls.refresh()
        for chord,want in pairs({["SUPER + CTRL + Page_Up"]="pageup",["SUPER + CTRL + Page_Down"]="pagedown"}) do
            local mode,token=press(chord);assert(mode=="19",chord);assert(token==hex(want),chord)
        end
        -- A wheel notch counts within its run: the same direction continues it, a turn or another mode press starts one.
        local function notch(chord)
            local mode,token=press(chord);assert(mode=="19",chord)
            return token:gsub("%x%x",function(h) return string.char(tonumber(h,16)) end):match("^(%a+):(%d+):(%d+)$")
        end
        local dir,run,one=notch("SUPER + mouse_down");assert(dir=="down" and one=="1")
        local _,again,second=notch("SUPER + mouse_down");assert(again==run and second=="2")
        local up,turned,first=notch("SUPER + mouse_up");assert(up=="up" and turned~=run and first=="1")
        press("SUPER + CTRL + Page_Up")
        local _,fresh,restart=notch("SUPER + mouse_up");assert(fresh~=turned and restart=="1")
        clock(222.6,"canvas",nil,"0");omarchy_xr_controls.refresh();wheelIsOmarchy()
        assert(isXR("SUPER + CTRL + Page_Up"));assert(select(2,press("SUPER + CTRL + Page_Down"))==hex("pagedown"))
        clock(222.7,"monitors");omarchy_xr_controls.refresh();wheelIsOmarchy()
        assert(bindings["SUPER + CTRL + Page_Up"]==nil);assert(bindings["SUPER + CTRL + Page_Down"]==nil)
        print("Scroll keys: SUPER+wheel takeover and SUPER+CTRL+Page_Up/Down publish mode 19, wheel notches counted per run, Omarchy's workspace scroll back on exit passed")
    end
end
local function testStageReclamp()
    clock(224);omarchy_xr_controls.refresh();tick(224)
    local w=find("0x50");w.at={x=19990,y=-10};w.size={x=3000,y=2000};dispatched={}
    tick(224.1);tick(224.2)
    w.size={x=3100,y=2000};tick(224.3);tick(224.6)            -- still changing: no fight
    assert(count("window.move","0x50")==0);assert(count("window.resize","0x50")==0)
    tick(224.9)
    assert(last("window.resize").x==2552);assert(last("window.resize").y==1440);assert(w.at.x==20000);assert(w.at.y==0)
    tick(225.5);assert(count("window.move","0x50")==1)       -- once
    local fitted=w.size;w.size={x=3000,y=2000};tick(225.52);tick(226.1);assert(count("window.move","0x50")==2)
    w.size={x=3000,y=2000};tick(226.7);tick(227.3);assert(count("window.move","0x50")==2) -- refused: tried once per geometry
    w.size=fitted
    w.at={x=20100,y=50};cursorPos={x=20300,y=300};files["/proc/uptime"]="227.6";bindLists[DRAG][1].callback()
    tick(227.6);tick(228.3);assert(count("window.move","0x50")==2) -- never during a drag
    bindLists[DRAG][2].callback();tick(228.4);tick(229)
    assert(count("window.move","0x50")==3);assert(w.at.x==20000)
    files["/proc/uptime"]="229";bindLists[DRAG][1].callback();tick(229.5);tick(231.9);assert(select(4,dragFields())=="1")
    tick(232.1);assert(select(4,dragFields())=="0");assert(bindings["mouse:272"]==nil) -- 3 s without travel end a lost drag
    clock(233,"monitors");omarchy_xr_controls.refresh()
    print("Stage re-clamp: back to the origin and the band 0.5 s after the geometry settles, never while changing or dragging passed")
    print("Drag idle: a drag whose release was missed ends after 3 s without pointer travel passed")
end
testConfirmHotkey()
testTapConfirm()
testEnsureStaged()
testStageDrag()
testResizeKeys()
testScrollKeys()
testStageReclamp()
