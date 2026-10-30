"""Copy only game resources into an Emscripten preload directory."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil

RESOURCE_DIRS = ('compiled', 'data', 'images', 'languages', 'particles', 'properties', 'reanim', 'sounds')

def package(source: Path, destination: Path):
    source, destination = source.resolve(), destination.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError('Asset output must be separate from the resource source')
    if not (source/'properties/resources.xml').is_file():
        raise ValueError(f'Missing game asset manifest: {source}/properties/resources.xml')
    for name in RESOURCE_DIRS:
        if not (source/name).is_dir(): raise ValueError(f'Missing resource directory: {source/name}')
    if destination.exists() and any(destination.iterdir()):
        if not (destination/'asset-manifest.json').is_file():
            raise ValueError('Refusing to overwrite an unrelated non-empty output directory')
        shutil.rmtree(destination)
    destination.mkdir(parents=True, exist_ok=True)
    records = []
    for name in RESOURCE_DIRS:
        folder = source/name
        for path in sorted(folder.rglob('*')):
            if not path.is_file(): continue
            relative = path.relative_to(source)
            target = destination/relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
            records.append({'path': relative.as_posix(), 'bytes': path.stat().st_size,
                            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    (destination/'asset-manifest.json').write_text(json.dumps(records, indent=2)+'\n')
    return records

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    records = package(args.source, args.destination)
    print(f'Packaged {len(records)} resources, {sum(x["bytes"] for x in records):,} bytes')
