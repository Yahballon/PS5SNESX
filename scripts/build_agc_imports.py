#!/usr/bin/env python3
"""Build upstream PS5 AGC import link-stubs, never package stub implementations.

PS5_Vulkan links these ELF import stubs when generating native module imports.
They are NOT to be included as runtime files: the real libSceAgc modules on a
compatible console must supply the imported symbols.
"""
from pathlib import Path
import os
import subprocess

root=Path(__file__).resolve().parents[1]
vulkan=Path(os.environ["PS5_VULKAN_DIR"])
sdk=Path(os.environ["PS5_PAYLOAD_SDK"])
out=root/"build/ps5-core/agc-imports"

def execute(*args):
    print("+"," ".join(map(str,args)),flush=True)
    subprocess.run([str(x) for x in args],check=True)

def main():
    sources={
        "libSceAgc":"agc_canary_link_stub.c",
        "libSceAgcDriver":"agc_driver_canary_link_stub.c",
    }
    out.mkdir(parents=True,exist_ok=True)
    for name,file in sources.items():
        source=vulkan/"vendor/ps5/sdk/stubs"/file
        if not source.is_file():
            raise RuntimeError("Missing PS5_Vulkan AGC import declaration: "+str(source))
        obj=out/(name+".o")
        elf=out/(name+".so")
        execute("sh",vulkan/"tooling/prospero-clang18","-std=c11","-O2","-fPIC",
                "-ffunction-sections","-fdata-sections","-c",source,"-o",obj)
        execute(sdk/"bin/prospero-lld","--shared","-soname",name+".prx",
                "-o",elf,obj)
        if not elf.is_file() or elf.stat().st_size<1000:
            raise RuntimeError("AGC import link stub was not built: "+name)
        exported=subprocess.check_output(["llvm-nm-18","-D","--defined-only",str(elf)],text=True)
        sym="sceAgcCreateShader" if name=="libSceAgc" else "sceAgcDriverSubmitDcb"
        if sym not in exported:
            raise RuntimeError(f"{elf} does not export {sym}")
        print(f"PASS {name} native link stub; host-link only, not packaged",flush=True)

if __name__=="__main__":
    main()
