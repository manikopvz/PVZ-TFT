"""Collect upstream notices alongside the linked web binary."""
from pathlib import Path
import argparse
import shutil

def collect(ports: Path, mixer: Path, destination: Path):
    notices = []
    for name in ['sdl2','libpng','libjpeg','zlib','ogg','vorbis','mpg123']:
        directory = ports/name
        if not directory.is_dir(): raise ValueError(f'Built codec sources missing: {directory}')
        candidates = [p for p in directory.rglob('*') if p.is_file() and
                      (p.name.lower().startswith(('copying','license','copyright')) or p.name.lower() in ['readme','readme.md','readme.txt'])]
        if not candidates: raise ValueError(f'No license or notice found for {name}')
        for p in candidates:
            target = destination/name/p.relative_to(directory)
            target.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(p,target);notices.append(target)
    for p in mixer.iterdir():
        if p.is_file() and (p.name.lower().startswith(('copying','license','gpl','lgpl')) or 'license' in p.name.lower()):
            target = destination/'SDL-Mixer-X'/p.name
            target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target);notices.append(target)
    return notices

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ports',type=Path);parser.add_argument('mixer',type=Path);parser.add_argument('destination',type=Path)
    args=parser.parse_args()
    print(f'Copied {len(collect(args.ports,args.mixer,args.destination))} third-party notice files')
