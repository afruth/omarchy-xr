"""Local panorama library, procedural backgrounds and live viewer settings."""
import hashlib
import json
import math
import os
from pathlib import Path
from typing import Any
import shutil
import subprocess
import tempfile


TRON_ID = 'builtin:tron'


def _magick(args):
    try:
        prefix = ['magick']
        if args[0] == 'identify':
            prefix.append(args.pop(0))
        return subprocess.run([*prefix, '-limit', 'thread', '2', '-limit', 'memory', '256MiB', '-limit', 'map', '512MiB', *args], check=True, capture_output=True, text=True, timeout=120).stdout
    except subprocess.CalledProcessError as exc:
        raise ValueError('Could not decode panorama: ' + exc.stderr[-300:]) from exc


def atomic(path, text):
    path = Path(path)
    temp = path.with_suffix(path.suffix + '.tmp')
    temp.write_text(text)
    temp.replace(path)


class Environment:
    def __init__(self, state, library=None):
        self.state = Path(state)
        self.library = Path(library) if library else Path(os.environ.get('XDG_DATA_HOME', str(Path.home()/'.local/share'))) / 'omarchy-xr/environments'
        self.library.mkdir(parents=True, exist_ok=True)
        self.profile = self.state/'environment.json'
        self.config: dict[str, Any] = {'id':'', 'brightness':25, 'rotation':0}
        if self.profile.exists():
            try:
                value = json.loads(self.profile.read_text())
                self.validate(value)
                self.config = self.normalized(value)
            except (ValueError, OSError, TypeError):
                pass
        self.publish()

    def items(self):
        result = []
        for path in sorted(self.library.glob('*/asset.json')):
            try:
                item = json.loads(path.read_text())
                if item['id'] == TRON_ID or item['id'] != path.parent.name or not (path.parent/'sky.bmp').is_file():
                    continue
                item['thumbnail'] = (path.parent/'thumbnail.jpg').as_uri()
                result.append(item)
            except (ValueError, OSError, KeyError, TypeError):
                continue
        builtin = {'id':TRON_ID, 'name':'Tron grid', 'kind':'procedural', 'thumbnail':''}
        return [builtin, *sorted(result, key=lambda item: item['name'].lower())]

    def validate(self, value):
        if not isinstance(value, dict) or not isinstance(value.get('id'), str):
            raise ValueError('Invalid environment')
        if value['id'] and value['id'] not in {item['id'] for item in self.items()}:
            raise ValueError('Environment image is unavailable. Import it again.')
        for key, low, high in (('brightness',0,100),('rotation',-180,180)):
            number = value.get(key)
            if isinstance(number, bool) or not isinstance(number, (int, float)) or not math.isfinite(number) or not low <= number <= high:
                raise ValueError('Invalid environment ' + key)
        if 'animated' in value and not isinstance(value['animated'], bool):
            raise ValueError('Invalid environment animated')

    @staticmethod
    def normalized(value):
        config = {key:value[key] for key in ('id','brightness','rotation')}
        if value['id'] == TRON_ID:
            config['animated'] = value.get('animated', True)
        return config

    def snapshot(self):
        return {'settings':self.config, 'items':self.items(), 'canImport':bool(shutil.which('magick'))}

    def set(self, value):
        self.validate(value)
        self.config = self.normalized(value)
        atomic(self.profile,json.dumps(self.config))
        self.publish()

    def publish(self):
        path = str(self.library/self.config['id']/'sky.bmp') if self.config['id'] else ''
        animation = ''
        if self.config['id'] == TRON_ID:
            path = TRON_ID
            animation = ' ' + str(int(self.config['animated']))
        # JSON string quoting is compatible with std::quoted for ordinary local paths.
        atomic(self.state/'environment.tsv', f'{self.config["brightness"]} {self.config["rotation"]} {json.dumps(path,ensure_ascii=False)}{animation}\n')

    def import_image(self, source, resolution=4096, name=None):
        source = self.import_source(source, resolution)
        identity = self.import_identity(source, resolution)
        target = self.library / identity
        if (target / 'asset.json').is_file():
            return identity
        with tempfile.TemporaryDirectory(prefix='.import-', dir=self.library) as directory:
            self.write_import(Path(directory), source, resolution, {"identity": identity, "title": name or source.stem.replace('_', ' '), "target": target})
        return identity

    def import_source(self, source, resolution):
        if type(resolution) is not int or resolution not in (4096, 8192):
            raise ValueError('Choose 4K or 8K')
        if not shutil.which('magick'):
            raise RuntimeError('Image import requires ImageMagick: sudo pacman -S imagemagick')
        source = Path(source).expanduser().resolve()
        if not source.is_file() or source.suffix.lower() not in ('.jpg', '.jpeg', '.png', '.bmp'):
            raise ValueError('Choose a 2:1 JPEG, PNG or BMP panorama')
        if source.stat().st_size > 512 * 1024 * 1024:
            raise ValueError('Image exceeds the 512 MB import limit')
        return source

    def import_identity(self, source, resolution):
        digest = hashlib.sha256()
        with source.open('rb') as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(chunk)
        return digest.hexdigest()[:24] + '-' + str(resolution)

    def write_import(self, work, source, resolution, record):
        identity, title, target = record["identity"], record["title"], record["target"]
        # Use a controlled filename: ImageMagick treats brackets and colons as syntax.
        image = work / ('source' + source.suffix.lower())
        shutil.copyfile(source, image)
        dimensions = _magick(['identify', '-ping', '-format', '%w %h', str(image)]).split()
        if len(dimensions) != 2:
            raise ValueError('Choose a single panoramic image')
        width, height = map(int, dimensions)
        if width != height * 2 or width > 40000 or height > 20000:
            raise ValueError('Panorama must have a 2:1 aspect ratio, at most 40000 × 20000')
        output_width = min(resolution, width)
        output_width -= output_width % 2
        if output_width < 512:
            raise ValueError('Panorama must be at least 512 × 256')
        _magick(['-define', f'jpeg:size={output_width}x{output_width // 2}', str(image), '-auto-orient', '-resize', f'{output_width}x{output_width // 2}!', '-colorspace', 'sRGB', '-alpha', 'off', '-depth', '8', 'BMP3:' + str(work / 'sky.bmp')])
        _magick([str(work / 'sky.bmp'), '-resize', '320x160!', '-quality', '85', str(work / 'thumbnail.jpg')])
        image.unlink()
        (work / 'asset.json').write_text(json.dumps({'id': identity, 'name': title, 'width': output_width, 'height': output_width // 2}))
        if target.exists():
            shutil.rmtree(target)
        work.rename(target)
