"""(AI-assisted) Build the actual SNES core on host and run a generated original cartridge."""
from pathlib import Path
import subprocess,tempfile,concurrent.futures,struct,zipfile
stage=Path(__file__).resolve().parent;project=Path(__file__).resolve().parents[2];app=project/'snes';core=app/'third_party/bsnes-mercury'
out=app/'build/host-test';out.mkdir(parents=True,exist_ok=True)
sources=subprocess.check_output(['make','-s','-f',str(app/'tools/core-sources.mk'),'list'],cwd=core,text=True).splitlines()
flags=['-O1','-DPROFILE_BALANCED','-DSTDC_HEADERS','-D__STDC_LIMIT_MACROS','-D__LIBRETRO__','-DSFC_LAGFIX',
       '-I'+str(core),'-I'+str(core/'libco'),'-I'+str(core/'sfc'),'-I'+str(core/'target-libretro'),'-Wno-vla-cxx-extension']
def compile(name):
    path=core/name;obj=out/Path(name).with_suffix('.o');obj.parent.mkdir(parents=True,exist_ok=True)
    if not obj.exists() or obj.stat().st_mtime<path.stat().st_mtime:
        compiler='clang++-18' if path.suffix=='.cpp' else 'clang-18'
        subprocess.run([compiler,*flags,*(['-std=c++11'] if path.suffix=='.cpp' else ['-std=gnu99']),'-c',str(path),'-o',str(obj)],check=True)
    return obj
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile,sources))
exe=out/'integration'
subprocess.run(['clang++-18','-std=c++20','-O1','-I'+str(app/'include'),'-I'+str(app/'src'),'-I'+str(project/'ps5/frontend'),
                str(stage/'test_snes_session.cpp'),str(app/'src/core_session.cpp'),str(app/'src/games.cpp'),str(app/'src/controller_bindings.cpp'),
                str(project/'ps5/frontend/fe_nfs.cpp'),*(str(p) for p in objects),'-lz','-pthread','-o',str(exe)],check=True)
# SPC700 program: configure a looping original BRR waveform on voice 0.
spc=bytearray()
def dsp(register,value):spc.extend([0x8f,register,0xf2,0x8f,value,0xf3])
for register,value in [(0x6c,0x20),(0x0c,127),(0x1c,127),(0x5d,3),(0x00,127),(0x01,127),(0x02,0),(0x03,0x10),(0x04,0),(0x05,0),(0x07,127),(0x4c,1)]:dsp(register,value)
spc.extend([0x2f,0xfe]);spc.extend(bytes(0x100-len(spc)));spc.extend([0,4,0,4]);spc.extend(bytes(0x200-len(spc)));spc.extend([0xc3]+[0x77]*8)
# 65816 in emulation mode: PPU backdrop, original SPC upload, controller B -> battery RAM.
code=bytearray([0x78,0xd8,0xa2,0xff,0x9a])
def write(address,value):code.extend([0xa9,value,0x8d,address&255,address>>8])
def wait(address,value):
    label=0x8000+len(code);code.extend([0xad,address&255,address>>8,0xc9,value,0xd0,0xf9])
for address,value in [(0x4200,0),(0x2100,0x80),(0x2121,0),(0x2122,0x1f),(0x2122,0),(0x2100,0x0f)]:write(address,value)
wait(0x2140,0xaa);wait(0x2141,0xbb)
write(0x2142,0);write(0x2143,2);write(0x2141,1);write(0x2140,0xcc);wait(0x2140,0xcc)
for i,byte in enumerate(spc):write(0x2141,byte);write(0x2140,i&255);wait(0x2140,i&255)
write(0x2142,0);write(0x2143,2);write(0x2141,0);write(0x2140,(len(spc)+1)&255)
loop=0x8000+len(code);write(0x4016,1);write(0x4016,0)
code.extend([0xad,0x16,0x40,0x29,1,0x8f,0,0,0x70,0x4c,loop&255,loop>>8])
rom=bytearray([0xea]*32768);rom[:len(code)]=code
header=0x7fc0;rom[header:header+21]=b'PS5SNES ORIGINAL TEST'.ljust(21,b' ')[:21]
rom[header+21:header+28]=bytes([0x20,2,5,1,0,0x33,0])
rom[header+28:header+32]=struct.pack('<HH',0xffff,0)
for offset in range(0x7fe0,0x8000,2):rom[offset:offset+2]=struct.pack('<H',0x8000)
with tempfile.TemporaryDirectory() as temporary:
    root=Path(temporary);games=root/'games';saves=root/'saves';games.mkdir();saves.mkdir()
    (games/'input.sfc').write_bytes(rom);(games/'headered.smc').write_bytes(bytes(512)+rom);(games/'invalid.sfc').write_bytes(b'bad cartridge')
    with zipfile.ZipFile(games/'input.zip','w',zipfile.ZIP_DEFLATED) as archive:archive.writestr('original-test.sfc',rom)
    subprocess.run([str(exe),str(games),str(saves)],check=True,timeout=45)
