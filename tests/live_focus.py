"""Opt-in preview: retained selection fits again after looking away. No desktop input."""
import os,socket,subprocess,tempfile,time,re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
layout=Path(os.environ.get('XDG_STATE_HOME',str(Path.home()/'.local/state')))/'omarchy-xr/viewer.tsv'
with tempfile.TemporaryDirectory(prefix='xr-focus-') as temp:
    pose=Path(temp)/'pose.sock';log=Path(temp)/'viewer.log'
    with log.open('w') as output:
        viewer=subprocess.Popen([str(root/'build/omarchy-xr'),'--layout',str(layout),'--pose-socket',str(pose)],stdout=output,stderr=output)
    channel=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM);sent=set()
    try:
        start=time.monotonic()
        while time.monotonic()-start<10:
            now=time.monotonic();elapsed=now-start
            assert viewer.poll() is None,log.read_text()
            if pose.exists():
                pitch=90 if elapsed>5 else 0
                channel.sendto(f'euler-nwu-v1 {now} 0 {pitch} 0'.encode(),str(pose))
                for threshold,command in [(3,b'fit_target'),(7,b'fit_target'),(8,b'zoom_in')]:
                    if elapsed>threshold and threshold not in sent:
                        channel.sendto(command,str(pose));sent.add(threshold)
            time.sleep(1/120)
    finally:
        viewer.terminate();viewer.wait(timeout=5);channel.close()
    text=log.read_text();print(text)
    assert viewer.returncode==0
    fitted=re.findall(r'Camera: fit selected monitor face-on (\S+)',text)
    assert len(fitted)==2 and fitted[0]==fitted[1],fitted
    print('Selection retained while looking down; repeated fit and zoom; clean rendering passed')
