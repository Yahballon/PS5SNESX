#!/usr/bin/env python3
"""SNESXY regression: PS5 process exit must be owned by system shell."""
from pathlib import Path
import json
r=Path(__file__).resolve().parents[1]
ui=(r/'ps5/frontend/fe_ps5.cpp').read_text()
main=(r/'snes/src/main_shelf.cpp').read_text()
crt=(r/'ps5/proto/native/tooling/native/app_crt.cpp').read_text()
app=json.loads((r/'app.json').read_text())
param=json.loads((r/'snes/sce_sys/param.json').read_text())
version=(r/'snes/src/snesxy_build_version.h').read_text()
assert 'if (app.ExitRequested())' in ui
assert 'snesxy_exit_to_shell("CoverFlow exit confirmed")' in ui
assert '::exit(0)' not in ui
assert 'sceSystemServiceLoadExec("exit",nullptr)' in main
assert 'snesxy_exit_to_shell("frontend completed")' in main
assert 'extern "C" [[noreturn]] void catchReturnFromMain(int status)' in main
assert 'snesxy_exit_to_shell("CRT main return")' in main
assert 'for(;;)sceKernelUsleep(100000)' in main
assert '__attribute__((weak)) void catchReturnFromMain' in crt
assert 'catchReturnFromMain(status);' in crt
assert app['version']=='1.0.9' and app['releaseTag']=='snesxy-v1.0.9'
assert param['titleId']=='PPSA99205' and param['contentVersion']=='01.000.009'
assert 'kVersion[] = "1.0.9"' in version
print("PASS: SNESXY PS5 shell exit, no SIGSYS libc exit, CRT interception")
