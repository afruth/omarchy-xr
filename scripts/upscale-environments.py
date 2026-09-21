#!/usr/bin/env python3
"""Offline 4× panorama upscale using an installed Upscayl CLI and model."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--models', type=Path, required=True)
parser.add_argument('--binary', default='upscayl-bin')
parser.add_argument('--gpu', default='1', help='Vulkan device ID; check Upscayl output')
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
source = root / 'assets/environments'
target = source / 'upscaled'
target.mkdir(exist_ok=True)

def run(*command):
    subprocess.run([str(part) for part in command], check=True)

for path in sorted(source.glob('*.png')):
    output = target / path.name
    if output.exists():
        print(f'Skipping existing {output.name}', flush=True)
        continue
    width, height = map(int, subprocess.check_output(
        ['magick', 'identify', '-format', '%w %h', str(path)], text=True).split())
    if width != 2 * height or width * 4 > 8192:
        raise SystemExit(f'Unsupported panorama dimensions: {path}')
    print(f'Upscaling {path.name} to {width*4} × {height*4}', flush=True)
    # Give both horizontal boundaries real panorama context. Crop this padding
    # after inference; do not mirror the seam or stretch the panorama.
    with tempfile.TemporaryDirectory(prefix='xr-upscale-') as directory:
        work = Path(directory)
        run('magick', '-limit', 'thread', '2', path, '-virtual-pixel', 'tile',
            '-set', 'option:distort:viewport', f'{width+128}x{height}-64+0',
            '-distort', 'SRT', '0', '+repage', work/'padded.png')
        run(args.binary, '-i', work/'padded.png', '-o', work/'large.png',
            '-m', args.models, '-n', 'high-fidelity-4x', '-s', '4', '-z', '4',
            '-g', args.gpu, '-t', '128', '-j', '1:1:1')
        pending = target / (path.stem + '.pending.png')
        run('magick', '-limit', 'thread', '2', work/'large.png', '-crop',
            f'{width*4}x{height*4}+256+0', '+repage', pending)
        pending.replace(output)
