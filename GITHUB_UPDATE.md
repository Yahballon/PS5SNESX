# Preparing the GitHub release (AI-assisted)

The PS5SNES console build entry point is `snes/tools/build.sh`; the inherited
root PCSX2 desktop project is retained for provenance and shared dependencies.
GitHub templates report PS5SNES problems here rather than to PCSX2 support.

Run the host integration test in Linux/WSL:

```bash
python3 snes/tools/test.py
```

Generate the source ZIP and refresh its SHA-256 manifest from the repository root:

```bash
python3 snes/tools/package_source.py
python3 snes/tools/package_source.py --check
```

Output: `source-release/PS5SNES-source-v6-by-YAHBALLON.zip` and its `.sha256` file.
The ZIP contains one PS5SNES directory. Git metadata, ignored build output,
prepared dependencies, unused FCEUmm NES sources and user data are excluded.
All retained upstream source and notices remain available in the repository.
Regenerate after changing source files; the manifest excludes its own hash.

Review the diff and untracked files before committing. Only README.md and LICENSE
were tracked in the initial local checkout, so adding the source tree is necessary.
Do not upload private SDKs, game files, credentials or local dependency caches.

Build the console application separately using BUILDING.md. Validate startup,
local/USB/NFS scanning, covers, controls, saves and states on a PS5 before publishing
an application release. Host tests alone do not establish console compatibility.
Upload the application ZIP and the source ZIP as separate release assets. Use
RELEASE_NOTES.md as the description and disclose AI-assisted work in the commit.
