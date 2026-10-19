#!/usr/bin/env python3
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1]
s=(root/'snes/src/loading.cpp').read_text()
exact=(root/'ps5/frontend/fe_exact_porpoise.cpp').read_text()
options=(root/'ps5/frontend/fe_options.cpp').read_text()
urls=(root/'snes/src/cover_urls.h').read_text()
assert 'HYPERCUBE' in s and 'SNESXY' in s and 'snesxy_build::kVersion' in s
assert 'project(v,t*0.37f' in s and 'project(q,-t*0.29f' in s
assert all(x in s for x in ['progress.games','progress.entries','progress.directory','progress.sources_total'])
assert all(x in exact and x in options for x in ['royal-indigo','sunset-arcade','emerald-circuit','glacier-glass','midnight-gold'])
assert 'thumbnails.libretro.com/Nintendo%20-%20Super%20Nintendo%20Entertainment%20System/Named_Boxarts/' in urls
assert (root/'snes/sce_sys/param.json').exists()
assert json.loads((root/'snes/sce_sys/param.json').read_text())['titleId']=='PPSA99205'
print('PASS: SNESXY hypercube animation, 5 themes, SNES-specific cover fallback and title ID')
