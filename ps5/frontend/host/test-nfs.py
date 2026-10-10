"""(AI-assisted) Compile and run the portable NFS backend tests with Clang."""
import os
from pathlib import Path
import subprocess
import tempfile

host = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="ps5sx2-nfs-") as tmp:
    binary = Path(tmp) / ("nfs_test.exe" if os.name == "nt" else "nfs_test")
    subprocess.run([
        os.environ.get("CXX", "clang++"), "-std=c++20", "-Wall", "-Wextra",
        "-DORBIS_NFS=1", "-I" + str(host / "nfs-stub"), "-I" + str(host.parent),
        str(host.parent / "fe_nfs.cpp"), str(host / "nfs_test.cpp"),
        *([] if os.name == "nt" else ["-Wl,--wrap=write"]), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary), str(Path(tmp) / "nfs.txt")], check=True)
