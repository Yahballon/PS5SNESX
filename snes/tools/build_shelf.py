"""(AI-assisted) PS5SNES out-of-tree core build, native link and signed test package."""
from pathlib import Path
import concurrent.futures,hashlib,json,os,shutil,subprocess,zipfile
project=Path(__file__).resolve().parents[2]
nes=project/'snes';out=nes/'build-shelf';out.mkdir(exist_ok=True)
core=nes/'third_party/bsnes-mercury';sdk=Path(os.environ['PS5_PAYLOAD_SDK'])
deps=Path(os.environ['PS5SNES_DEPS']);nfs=Path(os.environ['LIBNFS_PREFIX']);z=Path(os.environ['LIBZ_PREFIX'])
native=project/'ps5/proto/native';wrapper=native/'tooling/prospero-clang18'
sources=subprocess.check_output(['make','-s','-f',str(nes/'tools/core-sources.mk'),'list'],cwd=core,text=True).splitlines()
incs=['-I'+str(nes/'ps5-compat'),'-I'+str(nes/'include'),'-I'+str(core),'-I'+str(core/'libco'),'-I'+str(core/'sfc'),'-I'+str(core/'target-libretro'),'-I'+str(nes/'include')]
base=['-O2','-march=znver2','-ffunction-sections','-fdata-sections','-fno-stack-protector','-fno-plt']
cflags=['-target','x86_64-sie-ps5','-isysroot',str(sdk),'-isystem',str(sdk/'target/include'),'-std=gnu99',
        '-DPROFILE_BALANCED','-DSTDC_HEADERS','-D__STDC_LIMIT_MACROS','-D__LIBRETRO__','-DSFC_LAGFIX',*base,*incs]
objects=[]
def compile_c(name):
    path=core/name;obj=nes/'build/obj/core'/Path(name).with_suffix('.o');obj.parent.mkdir(parents=True,exist_ok=True)
    if not obj.exists() or obj.stat().st_mtime < path.stat().st_mtime:
        if path.suffix=='.cpp':
            flags=[*base,'-fexceptions','-frtti','-Wno-vla-cxx-extension','-std=c++11','-DPROFILE_BALANCED','-DSTDC_HEADERS','-D__STDC_LIMIT_MACROS','-D__LIBRETRO__','-DSFC_LAGFIX',*incs]
            subprocess.run(['sh',str(wrapper),*flags,'-c',str(path),'-o',str(obj)],check=True)
        else:subprocess.run(['clang-18',*cflags,'-c',str(path),'-o',str(obj)],check=True)
    return obj
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    objects=list(pool.map(compile_c,sources))
print(f'SNES core: {len(objects)} PS5 objects (AI-assisted)',flush=True)
cpp=[*base,'-std=c++20','-fno-exceptions','-fno-rtti','-pthread','-DORBIS_NFS=1','-I'+str(nfs/'include'),
     '-I'+str(z/'include'),'-DORBIS_VULKAN=1','-DORBIS_NES=1','-DORBIS_CURL=1','-DCURL_STATICLIB',
     '-I'+str(project/'3rdparty/vulkan/include'),'-I'+str(project/'3rdparty/include'),
     '-DFE_FONT_DIR="'+str(project/'bin/resources/fonts')+'"','-DFE_ASSET_DIR="'+str(project/'ps5/frontend/assets')+'"','-I'+str(nes/'src'),'-I'+str(project/'ps5/frontend'),
     '-I'+str(project/'ps5/coreorbis/orbis-shims/filesystem-elevation'),*incs]
cpp_sources=[nes/'src'/n for n in ['core_session.cpp','games.cpp','shelf_games.cpp','shelf_runtime.cpp','main_shelf.cpp','loading.cpp','library_cache.cpp','controller_bindings.cpp']]
cpp_sources += [project/'ps5/frontend'/f'{name}.cpp' for name in ['fe_app','fe_covers','fe_renderer','fe_sound','fe_text','fe_vk','fe_ps5','fe_web','fe_i18n','fe_settings','fe_options','fe_exact_porpoise']]
cpp_sources += [project/'ps5/frontend/fe_nfs.cpp',project/'ps5/coreorbis/orbis-shims/filesystem-elevation/src/elevation.cpp']
cpp_sources += [project/'ps5/porpoise_original/src/ui_gfx.cpp']
cpp_sources += [native/'tooling/native'/f'{name}.cpp' for name in ['app_crt','app_cpp_runtime']]
for src in cpp_sources:
    obj=out/'obj'/f'{src.stem}.o';obj.parent.mkdir(exist_ok=True)
    subprocess.run(['sh',str(wrapper),*cpp,'-c',str(src),'-o',str(obj)],check=True);objects.append(obj)

qrcode=out/'obj/qrcodegen.o'
subprocess.run(['clang-18',*cflags,'-I'+str(project/'ps5/frontend/third_party/qrcodegen'),'-c',str(project/'ps5/frontend/third_party/qrcodegen/qrcodegen.c'),'-o',str(qrcode)],check=True)
objects.append(qrcode)
vulkan=deps/'PS5_Vulkan'
mesa=Path(os.environ.get('PS5SNES_MESA_UTIL_DIR',str(out/'mesa-util')))
if not all((mesa/name).exists() for name in ['u_thread.o','anon_file.o','os_file.o']):
    environment=dict(os.environ,PS5_VULKAN_DIR=str(vulkan))
    subprocess.run(['bash',str(project/'ps5/tools/build-mesa-util.sh'),str(mesa),str(sdk)],check=True,env=environment)
for name in ['u_thread.o','anon_file.o','os_file.o']:objects.append(mesa/name)
(out/'vulkan').mkdir(exist_ok=True)
vkarchive=out/'vulkan/libps5vk.ps5.a'
shutil.copy2(vulkan/'build/driver/ps5/libps5vk.ps5.a',vkarchive)
subprocess.run(['llvm-objcopy-18','--redefine-sym','vkGetInstanceProcAddr=ps5vk_driver_vkGetInstanceProcAddr',str(vkarchive)],check=True)
psbc=Path(os.environ['PS5SNES_PSBC_GENERATED'])
if not psbc.is_file() or psbc.stat().st_size<10000:
    raise RuntimeError('Missing generated PSBC archive: '+str(psbc))
vklibs=[vkarchive,vulkan/'.deps/native/vulkan-runtime/lib/libvk_runtime.ps5.a',vulkan/'build/driver/ps5/libpsbc_driver.ps5.a',psbc,vulkan/'.deps/native/psbc/lib/libpsbc_support.ps5.a']
netlibs=[z/'lib'/f'lib{name}.a' for name in ['curl','psl','ssl','crypto','zstd','z']]

stubs=sorted(str(p) for p in (sdk/'target/lib').glob('*.so') if p.name not in ('libScePosixForWebKit.so','libSceGLSlimVSH.so'))
agc=Path(os.environ['PS5SNES_AGC_STUB_DIR'])
agc_stubs=[agc/'libSceAgc.so',agc/'libSceAgcDriver.so']
for stub in agc_stubs:
    if not stub.is_file():raise RuntimeError('Missing AGC import stub: '+str(stub))
stubs.extend(str(p) for p in agc_stubs)
pie=out/'llvm-pie.elf'
link=['ld.lld-18','-m','elf_x86_64','-pie','-z','max-page-size=0x4000','-mllvm','-emulated-tls',
      '--hash-style=gnu','-T',str(native/'tooling/native/ps5-pie.ld'),
      '-T',str(project/'ps5/coreorbis/orbis-shims/ehframe.ld'),'--eh-frame-hdr',
      '--version-script',str(native/'tooling/native/app-symbols.map'),'--no-dynamic-linker','-e','_start',
      '-o',str(pie),*(str(p) for p in objects),str(nfs/'lib/libnfs.a'),'--whole-archive',*(str(p) for p in vklibs),'--no-whole-archive','--start-group',*(str(p) for p in netlibs),'--end-group',
      '--as-needed',*stubs,'-L'+str(sdk/'target/lib'),
      *(str(sdk/'target/lib'/f'lib{lib}.a') for lib in ['c++','c++abi','unwind','c']),'-lpthread']
subprocess.run(link,check=True)
exports=out/'orbis-exports.map'
subprocess.run(['python3',str(project/'ps5/genmap.py'),str(pie),str(sdk/'target/lib'),str(exports)],check=True)
subprocess.run(link+['--version-script',str(exports)],check=True)
# (AI-assisted) Build the project's PS5SX2 native writer, never the driver helper:
# it emits the already-tested flexible/internal memory process parameters.
tool=out/'host/ps5-native-tool';tool.parent.mkdir(exist_ok=True)
subprocess.run(['clang++-18','-std=c++20','-O2','-o',str(tool),
                *(str(native/'tooling/native'/f'{name}.cpp') for name in
                  ['native_app_builder','elf_object','self_container','sce_module_writer']),'-lz'],check=True)
subprocess.run([str(tool),'link','--in',str(pie),'--out',str(out/'eboot.elf'),'--stub-dir',str(sdk/'target/lib'),
                '--stub',str(agc_stubs[0]),'--stub',str(agc_stubs[1]),
                 '--module-sdk','0x02000009','--companion-sdk','0x08050001','--file-name','eboot.elf'],check=True)
subprocess.run(['python3',str(nes/'tools/check_memory.py'),str(out/'eboot.elf')],check=True)
subprocess.run(['python3',str(project/'ps5/tools/prepare_console_elf.py'),'--input',str(out/'eboot.elf'),
                '--output',str(out/'eboot-console-ready.elf')],check=True)
app=out/'package/PPSA99205';(app/'sce_module').mkdir(parents=True,exist_ok=True)
subprocess.run([str(tool),'self','--sign','--in',str(out/'eboot.elf'),'--out',str(app/'eboot.bin'),'--magic','0x1D3D154F'],check=True)
subprocess.run([str(tool),'self','--inspect','--file',str(app/'eboot.bin')],check=True)
shutil.copytree(nes/'sce_sys',app/'sce_sys',dirs_exist_ok=True)
shutil.copy2(deps/'PS5_Vulkan/runtime/libc.prx',app/'sce_module/libc.prx')
porpoise_assets=project/'ps5/porpoise_original/assets'
for required in ['fonts/Nunito-Regular.ttf','fonts/Nunito-SemiBold.ttf','fonts/Nunito-Bold.ttf',
                 'fonts/Nunito-ExtraBold.ttf','fonts/NotoSansJP-Porpoise.ttf','ui/buttons.png']:
    if not (porpoise_assets/required).is_file():
        raise RuntimeError('Missing pinned Porpoise asset: '+required)
shutil.copytree(porpoise_assets,app/'porpoise-ui',dirs_exist_ok=True)
shutil.copy2(project/'ps5/porpoise_original/LICENSE',app/'porpoise-ui/LICENSE')
baseline=os.environ.get('PS5SNES_LAPY_BASELINE')
if baseline:
    with zipfile.ZipFile(baseline) as archive:
        for filename in ['lapy.elf','lapy-manifest.json']:
            (app/filename).write_bytes(archive.read('PPSA99205/'+filename))
else:
    subprocess.run(['python3',str(deps/'filesystem-helper/tools/build-lapy-helper.py'),'PPSA99205',str(out/'filesystem-helper')],check=True)
    for filename in ['lapy.elf','lapy-manifest.json']:
        shutil.copy2(out/'filesystem-helper'/filename,app/filename)
licenses=app/'licenses';licenses.mkdir(exist_ok=True)
for path,name in [(core/'LICENSE','bsnes-mercury-GPL.txt'),(project/'COPYING.GPLv3','GPLv3.txt'),
                  (out/'filesystem-helper/Lapy-MIT.txt','Lapy-MIT.txt'),(deps/'libnfs/COPYING','libnfs.txt')]:
    if path.exists():shutil.copy2(path,licenses/name)
if (nes/'licenses').exists():
    shutil.copytree(nes/'licenses',licenses,dirs_exist_ok=True)
revision_file=core/'UPSTREAM_REVISION.txt'
if revision_file.exists():commit=revision_file.read_text().strip()
else:commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=core,text=True).strip()
manifest={'ai_assisted':True,'name':'SNESXY v1.0.9 by YAHBALLON','title':'PPSA99205','core':'bsnes-mercury balanced',
          'frontend':'SNESXY 3D CoverFlow with 5 native themes',
          'frontend_source_sha256':{str(p.relative_to(project)):hashlib.sha256(p.read_bytes()).hexdigest() for p in cpp_sources if '/ps5/frontend/' in str(p)},
          'core_commit':commit,'core_source':'https://github.com/libretro/bsnes-mercury','console_tested':False,
          'files':{str(p.relative_to(app)):hashlib.sha256(p.read_bytes()).hexdigest() for p in app.rglob('*')
                   if p.is_file() and p.name != 'build-manifest.json'}}
(app/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
shutil.copy2(nes/'INSTALL.md',out/'package/INSTALL.md')
bundle=out/'SNESXY-v1.0.9-by-YAHBALLON.zip'
with zipfile.ZipFile(bundle,'w',zipfile.ZIP_DEFLATED) as archive:
    for path in app.rglob('*'):
        if path.is_file():archive.write(path,Path('PPSA99205')/path.relative_to(app))
    archive.write(nes/'INSTALL.md','INSTALL.md')
print('SNESXY native install package:',bundle,flush=True)
