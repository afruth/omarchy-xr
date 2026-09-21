"""Opt-in offscreen pan integration against one existing XR capture output."""
import json,os,socket,subprocess,tempfile,time
from pathlib import Path
root=Path(__file__).resolve().parents[1]
outputs=json.loads(subprocess.check_output(['hyprctl','-j','monitors']))
source=next(m['name'] for m in outputs if m['name'].startswith('OMXR-'))
with tempfile.TemporaryDirectory(prefix='xr-pan-') as directory:
    temp=Path(directory);pose=temp/'pose.sock';layout=temp/'layout.tsv'
    layout.write_text(f'{source}\t0\t0\t3440\t1440\t60\n')
    with (temp/'viewer.log').open('w') as log:
        process=subprocess.Popen([str(root/'build/omarchy-xr'),'--layout',str(layout),'--pose-socket',str(pose),'--workspace-degrees','90','--workspace-follow'],env={**os.environ,'SDL_VIDEODRIVER':'offscreen'},stdout=log,stderr=log)
    channel=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM)
    serial=0
    def run(seconds,pan=False):
        global serial
        until=time.monotonic()+seconds
        while time.monotonic()<until:
            assert process.poll() is None,(temp/'viewer.log').read_text()
            if pose.exists():
                channel.sendto(f'euler-nwu-v1 {time.monotonic()} 0 0 0'.encode(),str(pose))
                if pan:
                    serial+=1
                    target=Path(str(pose)+'.controls.pan');part=target.with_suffix('.tmp')
                    part.write_text(f'{process.pid} {serial} 1 {serial*60} {serial*30} 1 {int(time.time())}')
                    part.replace(target)
            time.sleep(.02)
    try:
        # Wait for a rendered frame before sending camera commands. Otherwise
        # queued startup fit can override zoom in the same first frame.
        deadline=time.monotonic()+20
        while not Path(str(pose)+'.stats').exists():
            assert time.monotonic()<deadline,(temp/'viewer.log').read_text()
            run(.5)
        channel.sendto(b'fit_target',str(pose));run(1)
        for _ in range(10):channel.sendto(b'zoom_in',str(pose))
        run(6)
        before=json.loads(Path(str(pose)+'.stats').read_text())
        run(6,pan=True)
        after=json.loads(Path(str(pose)+'.stats').read_text())
        assert abs(before['zoomDepth']-after['zoomDepth'])<.001,(before,after)
        assert abs(after['panX'])>.1 and abs(after['panY'])>.01,after
        run(5,pan=True)
        bounded=json.loads(Path(str(pose)+'.stats').read_text())
        assert abs(bounded['panX']-after['panX'])<.001 and abs(bounded['panY']-after['panY'])<.001,(after,bounded)
        print('Live cylindrical pan: fixed zoom, two-axis movement and clamped edges passed')
        print({k:bounded[k] for k in ('fps','zoomDepth','panX','panY','workP95')})
    finally:
        process.terminate();process.wait(timeout=5);channel.close()
