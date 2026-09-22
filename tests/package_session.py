#!/usr/bin/python3
"""Session IPC double for the isolated package test; never contact a live desktop."""
import json
from pathlib import Path
import sys


def shell():
    path = Path.home() / 'session.json'
    state = json.loads(path.read_text()) if path.exists() else {'enabled': [], 'bar': []}
    command, *args = sys.argv[2:]
    plugins = []
    for manifest in (Path.home() / '.config/omarchy/plugins').glob('*/manifest.json'):
        if not manifest.parent.name.startswith('.'):
            data = json.loads(manifest.read_text())
            plugins.append({**data, 'enabled': data['id'] in state['enabled']})
    if command == 'listPlugins':
        print(json.dumps(plugins))
    elif command == 'enablePlugin':
        if args[0] not in {p['id'] for p in plugins}:
            raise SystemExit('unknown plugin')
        state['enabled'] = sorted(set(state['enabled'] + [args[0]]))
        print('ok')
    elif command == 'setPluginEnabled':
        state['enabled'].remove(args[0])
        print('ok')
    elif command == 'putBarWidget':
        state['bar'] = sorted(set(state['bar'] + [args[0]]))
        print('ok')
    elif command in ('rescanPlugins', 'summon'):
        print('ok')
    else:
        raise SystemExit('Unexpected shell IPC: ' + command)
    path.write_text(json.dumps(state))


def main():
    if Path(sys.argv[0]).name == 'hyprctl':
        responses = {'binds': '[]', 'devices': '{"mice": []}', 'reload': 'ok', 'configerrors': ''}
        print(responses[sys.argv[1]])
    else:
        shell()


if __name__ == '__main__':
    main()
