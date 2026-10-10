# PS5SX2 filesystem helper proposal (AI-assisted)

The Cloud HBA Store used a bundled filesystem helper. PS5SX2 currently starts
its existing HEN request path but does not verify that `/data` became writable.
The user's latest `mkdir /data/PCSX2` failure with errno 2 shows that `/data`
is still unavailable inside this app.

## Proposed integration

- Use the public boilerplate's Lapy client and pinned upstream helper, rather
  than implementing a new kernel backend.
- Build the helper for the exact title `PPSA99203`; its manifest requires one
  request, no resident service and no wildcard title.
- Call it once during startup, after existing HEN initialization and before
  opening the shelf and initializing emulation threads.
- First prove existing access by writing, reading, comparing and deleting a
  PID-specific temporary file under `/data`.
- If access is absent, try the cooperative resident service. Only if no service
  has claimed the request, submit the bundled helper to the already-running
  local ELF loader at `127.0.0.1:9021`.
- Accept success only after the `/data` write/read/remove proof passes. Report
  timeout, rejection or failed proof explicitly in the startup log.
- Include `lapy.elf`, its exact-title manifest and upstream license in the test
  package; preserve the existing NFS and descriptor-based settings writer.

## Permission and effect

The helper changes the app's filesystem privileges outside its sandbox. This
enables access to `/data`, and the capability is broader than just the settings
file. The upstream implementation also manages temporary helper processes.
No PS5 deployment or helper execution is performed from this computer; the
helper is invoked only when the user installs and launches the test app.

The public client's existing checks and upstream host tests will be retained.
A successful build and host tests do not establish compatibility on the user's
specific firmware and loader.

## Inputs

- Client: blackbearreloaded/ps5-native-app-boilerplate,
  `examples/sandbox-elevation/`.
- Helper: mpereiraesaa/PS5-Lapy-JB-Daemon at
  `54a095c0f19161825e845daa760a03b446e654fa`.
- Helper SDK: official public PS5 Payload SDK v0.40, as pinned by upstream.
- Documentation: https://github.com/blackbearreloaded/ps5-native-app-boilerplate/blob/main/docs/SANDBOX_ELEVATION.md

The user explicitly approved this filesystem privilege change on 2026-10-05.
The exact-title helper was built and its manifest/ELF/protocol hashes verified.
The upstream client/protocol/proof host tests passed. The client is integrated
after the existing HEN request path and before the settings frontend starts.
Console behavior still requires the user's test.

A separate alternative is to store configuration on the already-mounted USB
drive, as later HBA Store builds did. That does not grant `/data` access and
requires consistent routing of settings reads as well as writes.
