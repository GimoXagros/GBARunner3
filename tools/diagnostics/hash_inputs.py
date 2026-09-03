#!/usr/bin/env python3
"""Hash local inputs. No network, payload copy, or upload."""
import argparse
import hashlib
import json
from pathlib import Path

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1048576), b''):
            h.update(block)
    return h.hexdigest()

def tree(path):
    entries = [(p.relative_to(path).as_posix(), sha(p)) for p in sorted(path.rglob('*')) if p.is_file()]
    canonical = ''.join(f'{digest}  {name}\n' for name, digest in entries)
    return dict(sha256=hashlib.sha256(canonical.encode()).hexdigest(), files=dict(entries))

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('rom', 'bios', 'save', 'nds', 'configs'):
        p.add_argument('--' + name, type=Path)
    p.add_argument('--output', type=Path)
    a = p.parse_args()
    r = {name: tree(path) if path.is_dir() else dict(size=path.stat().st_size, sha256=sha(path))
         for name in ('rom', 'bios', 'save', 'nds', 'configs') if (path := getattr(a, name))}
    text = json.dumps(r, ensure_ascii=False, indent=2) + '\n'
    if a.output:
        a.output.write_text(text, encoding='utf-8')
    print(text)

if __name__ == '__main__':
    main()
