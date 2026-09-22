# Third-party packages (.apak installs) live here on the device image.

At runtime the infotainment scans this directory: every subdirectory with a
`manifest.json` becomes an app in the drawer. Developers ship packages as
`.apak` files (built with `tools/apak.py`) and install them from the
in-app **Packages** store - files land here.

See `docs/Package-System.md` and `examples/package_examples/` to write one.
