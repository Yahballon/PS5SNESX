# Installing PS5SNES v6 by YAHBALLON

(AI-assisted) Native PS5 SNES homebrew using bsnes-mercury (balanced profile), with the original PS5SX2 shelf.

Copy the complete PPSA99205 folder to /data/homebrew/PPSA99205/. Keep the included
sce_module/libc.prx, lapy.elf, lapy-manifest.json and sce_sys/icon0.png.
Launch through your compatible PS5 homebrew launcher. This app is separate from PS5NES.

ROM folders:
- /data/PS5SNES/games/
- /mnt/usb0/homebrew/PS5SNES/games/ (also USB1–USB7)
- /mnt/usb0/homebrew/PS5SNES/roms/
- /mnt/usb0/PS5SNES/games/
- /mnt/usb0/SNES/

Supported normal cartridges: .sfc, .smc, .swc and .fig, or a ZIP containing exactly
one supported cartridge. Stored/Deflate ZIP only; no encryption or ZIP64.
Maximum ROM 32 MiB, maximum file/archive 64 MiB. Common 512-byte copier headers are accepted.

NFS: Square → R1 / All games → Settings → Network → NFS URL.
Example: nfs://192.168.1.10/SNES. Save and restart. Configuration file:
/data/PS5SNES/nfs.txt. Cached library contents can be refreshed with Rescan game library.

Defaults: Cross = SNES B, Circle = A, Square = Y, Triangle = X, L1 = L, R1 = R,
Options = Start, touchpad/Create = Select. D-pad and left stick provide directions.
Customize all buttons in Square → R2 / Controls, globally or per game.

Save state: L3 + R3 + D-pad Up. Load state: L3 + R3 + D-pad Down.
Menu: L1 + R1 + touchpad click. Holding the menu combination for two seconds
remains an emergency exit after remapping. One state slot per ROM.

Data: /data/PS5SNES/{saves,settings,covers,cache,logs}/.
Fallback data root: /download0/PS5SNES/ if /data is inaccessible.
Boot log: /data/PS5SNES/logs/boot.log. QR settings page: port 8844.

The restricted filesystem helper targets PPSA99205 and may need local elfldr on port 9021.
Enhancement chips use HLE where supported; external chip firmware is not included.
The frontend exposes normal SNES cartridges and one active player, not
Super Game Boy, Satellaview, Sufami Turbo, MSU-1 external data or light guns.

Previous PS5 operation was reported by the user. The latest v6 cover update still
needs console testing. No ROMs or proprietary firmware are included.

Local and USB folders are scanned on every startup. An unreachable IPv4 NFS server
is skipped after a bounded connection check. Press R2 on the shelf to refresh the
selected cover; an existing image is retained if the download fails.

Additional folders include /data/SNES, /data/PS5SNES/roms and
/data/homebrew/PPSA99205/games or roms, plus USB homebrew/PPSA99205/games or roms.
