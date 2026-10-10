"""(AI-assisted) Package Git-visible PS5SNES source and verify its SHA-256 manifest."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / 'SOURCE-MANIFEST.json'
OUTPUT = ROOT / 'source-release/PS5SNES-source-v6-by-YAHBALLON.zip'
EXCLUDED = ('snes/third_party/fceumm/',)
FORBIDDEN = {'.sfc', '.smc', '.swc', '.fig', '.nes', '.iso', '.chd', '.rom',
             '.bios', '.sav', '.state', '.elf', '.self', '.prx', '.pkg', '.zip',
             '.exe', '.dll', '.obj', '.o', '.a', '.so', '.pyc'}


def source_files():
    result = subprocess.check_output(
        ['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'],
        cwd=ROOT)
    names = sorted(set(result.decode('utf-8').split('\0')) - {''})
    files = {}
    for name in names:
        if name == MANIFEST.name or name.startswith(EXCLUDED):
            continue
        path = ROOT / name
        if not path.exists():
            continue  # Locally removed tracked files.
        if path.is_symlink() or not path.is_file():
            raise SystemExit(f'Unsupported source entry: {name}')
        if not path.resolve().is_relative_to(ROOT):
            raise SystemExit(f'Entry outside repository: {name}')
        if path.suffix.lower() in FORBIDDEN:
            raise SystemExit(f'Binary/game/user artifact in source selection: {name}')
        if path.stat().st_size >= 100 * 1024 * 1024:
            raise SystemExit(f'File exceeds GitHub source size limit: {name}')
        files[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    for required in ['README.md', 'LICENSE', 'COPYING.GPLv3', 'INSTALL.md',
                     'snes/tools/build.sh', 'snes/third_party/bsnes-mercury/LICENSE']:
        if required not in files:
            raise SystemExit(f'Missing required source file: {required}')
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Verify without rewriting files')
    args = parser.parse_args()
    files = source_files()
    manifest = {'project': 'PS5SNES', 'version': 'v6 by YAHBALLON',
                'ai_assisted': True, 'core': 'bsnes-mercury balanced',
                'cover_entries': 723, 'files': files}
    if args.check:
        if json.loads(MANIFEST.read_text(encoding='utf-8')) != manifest:
            raise SystemExit('Source manifest is stale; run package_source.py again.')
        with zipfile.ZipFile(OUTPUT) as archive:
            expected = {'PS5SNES/' + name for name in files} | {'PS5SNES/' + MANIFEST.name}
            if len(archive.namelist()) != len(expected) or set(archive.namelist()) != expected:
                raise SystemExit('Archive file selection differs from the source manifest.')
            for name, digest in files.items():
                if hashlib.sha256(archive.read('PS5SNES/' + name)).hexdigest() != digest:
                    raise SystemExit(f'Archive hash mismatch: {name}')
            if archive.read('PS5SNES/' + MANIFEST.name) != MANIFEST.read_bytes():
                raise SystemExit('Archive manifest differs from repository manifest.')
        checksum = OUTPUT.with_suffix('.zip.sha256').read_text(encoding='utf-8')
        if checksum != f'{hashlib.sha256(OUTPUT.read_bytes()).hexdigest()}  {OUTPUT.name}\n':
            raise SystemExit('Archive checksum mismatch.')
        print(f'Verified {len(files)} source files, archive contents and ZIP checksum (AI-assisted).')
        return
    MANIFEST.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='\n')
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(OUTPUT, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in sorted([*files, MANIFEST.name]):
            info = zipfile.ZipInfo('PS5SNES/' + name, date_time=(2026, 1, 1, 0, 0, 0))
            info.create_system = 3
            executable = name.endswith('.sh') or name == 'ps5/proto/native/tooling/prospero-clang18'
            info.external_attr = (0o100755 if executable else 0o100644) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, (ROOT / name).read_bytes())
    OUTPUT.with_suffix('.zip.sha256').write_text(
        f'{hashlib.sha256(OUTPUT.read_bytes()).hexdigest()}  {OUTPUT.name}\n',
        encoding='utf-8', newline='\n')
    print(f'Packaged {len(files)} source files: {OUTPUT} (AI-assisted)')


if __name__ == '__main__':
    main()
