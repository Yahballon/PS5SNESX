# Release checks (AI-assisted)

- Run `python3 snes/tools/test.py` in Linux/WSL with Clang 18, Make and zlib headers.
- Build with `PS5SNES_DEPS=/path/to/prepared/deps bash snes/tools/build.sh`.
  The native memory and signed executable checks must pass.
- On PS5, check startup, local/USB/NFS scanning, offline NAS behavior, selected
  cover refresh, controls, audio, battery saves and save/load states.
- Confirm title ID PPSA99205, content version 01.000.006 and v6 branding.
- Confirm the application ZIP contains eboot.bin, sce_sys, sce_module/libc.prx,
  lapy.elf, lapy-manifest.json, build-manifest.json and license notices.
- Run `python3 snes/tools/package_source.py` and then the same command with `--check`.
- Review the source archive and Git diff before committing or publishing.
  Include no ROMs, proprietary firmware, keys, credentials or user data.
- Attach the application ZIP and source ZIP separately with their checksums.
  Record the tested hardware/launcher and remaining limitations in release notes.

The previous release notes describe earlier verification. They are not evidence
that a newly built executable was tested on hardware.
