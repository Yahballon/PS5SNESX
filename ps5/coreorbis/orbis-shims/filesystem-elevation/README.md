# Filesystem helper client (AI-assisted integration)

The three client/protocol files are copied without implementation changes from
blackbearreloaded/ps5-native-app-boilerplate's `examples/sandbox-elevation/`.
Their upstream copyright and GPL-3.0-or-later notices are retained.

PS5SX2 calls the client after its existing HEN request path. An actual `/data`
write/read/remove proof, rather than the process's effective UID, determines
whether filesystem access exists. Failure is logged and notified without
claiming success.

The test title must include `lapy.elf` and `lapy-manifest.json` at its top level.
The helper is built by upstream `tools/build-lapy-helper.py PPSA99203 OUTPUT`,
which pins Lapy at `54a095c0f19161825e845daa760a03b446e654fa`, uses the helper's
supported SDK v0.40 and verifies its exact-title one-request manifest and hashes.
The app itself continues to use the previously prepared SDK v0.42 fork.

The upstream host client tests passed, and the app was compiled, linked and
signed. This does not establish console compatibility on the user's firmware.

References:
- https://github.com/blackbearreloaded/ps5-native-app-boilerplate/blob/main/docs/SANDBOX_ELEVATION.md
- https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon

The helper's MIT license is packaged as `licenses/Lapy-MIT.txt`; its protocol
header is LGPL-2.1-or-later and the pinned ps5log input is GPL-3.0-or-later.
Upstream retains responsibility for its kernel backend; no copied/raw-pointer
kernel backend is implemented in PS5SX2.
