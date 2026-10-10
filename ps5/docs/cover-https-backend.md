Cover HTTPS backend (AI-assisted)

Optional LIBCURL_PREFIX must contain PS5 static libcurl 8.18.0, libpsl,
OpenSSL, zstd and zlib archives and include/curl headers. Pass this prefix
when compiling Makefile.vk and running link-vk.sh. The test build uses
SHA-verified PacBrew v0.40.2 inputs. Existing native HTTP2 is retained for
builds without this prefix. After changing this option, rebuild fe_ps5.o.

CA trust is embedded from frontend/assets/ca-bundle.crt (the existing
installer Mozilla bundle). Keep it current together with installer/ca/cacert.pem.
Certificate and hostname verification are enabled. Only HTTPS URLs and
redirects are allowed, with a 16 MiB response limit and a 20 second timeout.
The boot log records backend version, HTTP/curl result and detailed failures.
Host checks do not establish compatibility with PS5 firmware.
