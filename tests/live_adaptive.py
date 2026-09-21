"""Opt-in hardware check: real captures, synthetic camera, no desktop input.
Requires a running Studio layout. Does not reserve or reconfigure any outputs.
"""
import os,socket,subprocess,tempfile,time,re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
layout=Path(os.environ.get('XDG_STATE_HOME',str(Path.home()/'.local/state')))/'omarchy-xr/viewer.tsv'
with tempfile.TemporaryDirectory(prefix='xr-adaptive-') as temp:
    temp=Path(temp);pose=temp/'pose.sock';log=temp/'viewer.log'
    with log.open('w') as output:
        viewer=subprocess.Popen([str(root/'build/omarchy-xr'),'--layout',str(layout),'--pose-socket',str(pose),'--workspace-curvature','90','--fps','120'],stdout=output,stderr=output)
    channel=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM)
    try:
        start=time.monotonic();zoomed=False
        while time.monotonic()-start<28:
            now=time.monotonic();elapsed=now-start
            assert viewer.poll() is None,log.read_text()
            if pose.exists():
                yaw=180 if 6<elapsed<17 else 0
                channel.sendto(f'euler-nwu-v1 {now} 0 0 {yaw}'.encode(),str(pose))
                if elapsed>17 and not zoomed:
                    for _ in range(10):channel.sendto(b'zoom_out',str(pose))
                    zoomed=True
            time.sleep(1/120)
    finally:
        viewer.terminate()
        try:viewer.wait(timeout=5)
        except subprocess.TimeoutExpired:viewer.kill();viewer.wait()
        channel.close()
    text=log.read_text();print(text)
    assert viewer.returncode==0,viewer.returncode
    rows=re.findall(r'Capture: (\S+) (visible|paused) (dmabuf|shm) (\d+)x(\d+) source (\d+)x(\d+) requests (\d+) frames (\d+)',text)
    assert rows,'Missing capture statistics'
    for name in {r[0] for r in rows}:
        paused=[r for r in rows if r[0]==name and r[1]=='paused']
        assert len(paused)>=2 and paused[0][-2:]==paused[1][-2:],('Hidden output still captured',name,paused)
    assert any(r[1]=='visible' and int(r[3])<int(r[5])//2 for r in rows),'No reduced-resolution resumed capture'
    assert any(r[2]=='dmabuf' for r in rows) or os.environ.get('OMARCHY_XR_SHM_CAPTURE'),'GPU capture unavailable'
    print('Adaptive resolution, zero new hidden requests, wakeup, and clean GPU teardown passed')
