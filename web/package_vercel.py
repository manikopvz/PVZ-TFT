#!/usr/bin/env python3
"""Package an already tested Wasm distribution for `vercel deploy --prebuilt`."""
import argparse
import json
from pathlib import Path
import shutil


def package(source: Path, destination: Path) -> None:
    source, destination = source.resolve(), destination.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError('Source and destination must not overlap')
    for name in ('index.html', 'index.js', 'index.wasm', 'index.data'):
        file = source / name
        if not file.is_file() or not file.stat().st_size:
            raise ValueError(f'Missing or empty build output: {name}')
    output = destination / '.vercel' / 'output'
    if output.exists():
        shutil.rmtree(output)
    shutil.copytree(source, output / 'static')
    config = {
        'version': 3,
        'routes': [
            {'src': '/(.*)', 'headers': {'Cache-Control': 'public, max-age=0, must-revalidate'}, 'continue': True},
            {'handle': 'filesystem'},
        ],
        'overrides': {
            'index.html': {'contentType': 'text/html; charset=utf-8'},
            'index.wasm': {'contentType': 'application/wasm'},
            'index.data': {'contentType': 'application/octet-stream'},
        },
    }
    (output / 'config.json').write_text(json.dumps(config, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    package(args.source, args.destination)
