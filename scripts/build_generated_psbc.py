#!/usr/bin/env python3
"""Build PS5 shader compiler generated objects omitted by the first-pass make.

The upstream PSBC Makefile enumerates wildcard sources at parse time, BEFORE
it generates nir/spirv/aco sources. On a clean checkout the PS5 compiler
archive therefore omits their object files, which an executable link needs.
Build only the missing generated objects using the SAME SDK make configuration.
"""
from pathlib import Path
import os
import subprocess
import sys

root=Path(__file__).resolve().parents[1]
vulkan=Path(os.environ["PS5_VULKAN_DIR"])
sdk=Path(os.environ["PS5_PAYLOAD_SDK"])
work=vulkan/".deps/work/psbc-ps5"
tree=work/"third_party/opengnm-psbc"
driver=vulkan/"build/driver/ps5/libpsbc_driver.ps5.a"
out=root/"build/ps5-core/generated-psbc.ps5.a"
sources=[
  "src/amd/compiler/aco_opcodes.cpp",
  "src/compiler/nir/nir_constant_expressions.c",
  "src/compiler/nir/nir_intrinsics.c",
  "src/compiler/nir/nir_opcodes.c",
  "src/compiler/nir/nir_opt_algebraic.c",
  "src/compiler/spirv/spirv_info.c",
  "src/compiler/spirv/vtn_gather_types.c",
]
def objpath(name):
    s=Path(name)
    if name.endswith(".cpp"): return s.with_suffix(".ps5.cpp.o")
    return s.with_suffix(".ps5.o")
def run(*args,cwd=None):
    print("+", " ".join(map(str,args)), flush=True)
    subprocess.run([str(x) for x in args],cwd=cwd,check=True)
def main():
    assert driver.is_file(), "PSBC driver archive was not built"
    assert (tree/"Makefile").is_file(), "PSBC work copy was not staged"
    current=subprocess.check_output(["llvm-ar-18","t",str(driver)],text=True).splitlines()
    members=set(current)
    missing=[s for s in sources if objpath(s).name not in members]
    if not missing:
        raise RuntimeError("All expected generated objects were in the PSBC archive; inspect undefined symbols manually")
    targets=[str(objpath(s)) for s in missing]
    makefile=work/"toolchain/Makefile.opengnm-psbc-ps5"
    support=vulkan/"tooling/psbc/support.mk"
    run("make","-C",tree,"-f",makefile,"-f",support,"-j4",
        f"PS5_PAYLOAD_SDK={sdk}",f"CC={sdk/'bin/prospero-clang'}",
        f"CXX={sdk/'bin/prospero-clang++'}", *targets)
    objects=[tree/objpath(s) for s in missing]
    for obj in objects:
        if not obj.is_file() or obj.stat().st_size<100:
            raise RuntimeError(f"Missing generated PSBC object: {obj}")
    out.parent.mkdir(parents=True,exist_ok=True)
    if out.exists():out.unlink()
    run("llvm-ar-18","rcs",out,*objects)
    check=subprocess.check_output(["llvm-nm-18","-gC","--defined-only",str(out)],text=True)
    for label in ("nir_intrinsic_infos","nir_op_infos","nir_eval_const_opcode",
                  "nir_opt_algebraic","instr_info","spirv_op_to_string"):
        # If the PSBC driver already defines a symbol, our supplement need not.
        if label not in check:
            existing=subprocess.check_output(["llvm-nm-18","-gC","--defined-only",str(driver)],text=True)
            if label not in existing:
                raise RuntimeError("Generated PSBC archive has no required symbol: "+label)
    print(f"PASS PS5 generated NIR/SPIRV/ACO objects: {len(objects)}, archive {out.stat().st_size} bytes")
if __name__=="__main__":
    main()
