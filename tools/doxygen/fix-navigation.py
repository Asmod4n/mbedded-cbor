#!/usr/bin/env python3
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('html', type=Path)
args = parser.parse_args()
path = args.html / 'navtree.js'
source = path.read_text()
for name in ('text', 'name'):
    before = f'ahref.textContent = escapeHtml({name});'
    after = f'ahref.textContent = {name};'
    if source.count(before) == 1 and source.count(after) == 0:
        source = source.replace(before, after)
    elif source.count(before) != 0 or source.count(after) != 1:
        raise SystemExit(f'Unexpected Doxygen navigation code: {before}')
path.write_text(source)
