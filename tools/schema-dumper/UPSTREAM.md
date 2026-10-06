# Bundled schema dumper

Source: https://github.com/quex46/dezlock-dump

Pinned commit: `5df842984d061af1ef2e06872fe3c74364d136fc` (the corrected current schema layout).

The tracked upstream source is preserved in `source/`; its files were not modified.
`../build-data-tools.ps1` builds both the executable and worker DLL and packages their
configuration files. The updater invokes the dumper with explicit non-interactive options,
redirected standard input/output, a timeout, and administrator privileges.

The worker DLL is temporarily loaded into the running game by the upstream dumper and
unloads after extraction. The normal overlay continues using ReadProcessMemory.

No top-level license file was present in this upstream revision. This local bundle does
not relicense upstream code under the overlay's MIT license. Upstream vendor notices
remain in `source/vendor/`. This integration does not fetch or execute remote updates.
