-- Exercise actual Lua callbacks without a compositor or any input devices.
local files,bindings,gestures={}, {}, {}
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
    get_config=function(_)return "lrm" end,
    bind=function(key,callback,options)
        local binding={options=options,callback=callback,set_enabled=function(self,value)self.enabled=value end}
        binding.remove=function(self)if bindings[key]==self then bindings[key]=nil end end
        bindings[key]=binding;return binding
    end,
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
assert(omarchy_xr_controls.version==2 and omarchy_xr_controls.hover_timer.timeout==33 and omarchy_xr_controls.hover_timer.enabled)
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
sample(1,"OMXR-test-2");assert(#focuses==2 and focuses[2]=="name:work")
omarchy_xr_controls.hover();assert(#focuses==2) -- duplicate sample
sample(0);sample(1);assert(#focuses==3 and focuses[3]=="5") -- leave/re-enter
repeatSample(sample, 200, "eDP-1")
assert(#focuses==3)
now=110;files["/proc/uptime"]="110";omarchy_xr_controls.refresh()
assert(omarchy_xr_controls.hover_timer==nil and liveTimer.enabled==false)
repeatSample(sample, 200)
assert(#focuses==3 and #movements==0)
print("Halo transitions select existing workspaces once; pointer motion stays independent; stale sessions cannot focus")
end

local function testSettings()
-- Apply settings live: unregister old gesture and replace only XR bindings.
now=111;files["/proc/uptime"]="111";files[path..".active"]="42 111";omarchy_xr_controls.refresh()
files["/state/omarchy-xr/controls-settings.tsv"]="5\nALT + Up\nALT + Down\nCTRL + R\nCTRL + I\nCTRL + O\n"
omarchy_xr_controls.refresh()
assert(not bindings["CTRL + Up"] and bindings["ALT + Up"].enabled)
assert(omarchy_xr_controls.fingers==5 and gestures[#gestures].fingers==5)
assert(gestures[#gestures-1].fingers==3 and gestures[#gestures-1].action=="unset")
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
