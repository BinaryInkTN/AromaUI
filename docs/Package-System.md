# AromaUI Package System (.apak)

Third-party apps for AromaUI hosts (e.g. the car infotainment) ship as
**`.apak` files** - plain ZIP bundles, Android-APK style:

```
my-app/
├── manifest.json   # required: metadata (id, name, version, ...)
├── ui.aroma        # Incense UI markup, Window{} root (optional if plugin builds UI)
├── assets/...      # images, data files (optional)
└── plugin.so       # native code plugin (optional, native targets only)
```

Build one with the packager (stdlib-only Python):

```sh
python3 tools/apak.py init my-app --id com.example.myapp --name "My App"
# ... edit manifest.json + ui.aroma ...
python3 tools/apak.py pack my-app -o myapp.apak
python3 tools/apak.py info myapp.apak
```

Install it on the device from the in-app **Packages** store
(app drawer → Packages → enter the `.apak` path → Install), or drop an
extracted package directory into the packages dir
(`./packages/<id>/` by default, `$AROMA_PACKAGES_DIR` override).

## Manifest reference

```json
{
  "id": "com.example.calc",
  "name": "Calculator",
  "version": "1.2.0",
  "version_code": 3,
  "icon": "AROMA_ICON_EXTENSION",
  "author": "Example Developer",
  "description": "Does math.",
  "entry": "ui.aroma",
  "plugin": "plugin.so",
  "min_abi": 1
}
```

| Field          | Required | Rules                                                        |
|----------------|----------|--------------------------------------------------------------|
| `id`           | yes      | reverse-dns, lowercase `[a-z0-9_]`, ≥2 parts (`com.example.x`) |
| `name`         | yes      | display name                                                 |
| `version`      | yes      | dotted string shown to users                                 |
| `version_code` | no (=0)  | integer; installs refuse **downgrades**                      |
| `icon`         | no       | `AROMA_ICON_*` name for the drawer (default `AROMA_ICON_WIDGETS`) |
| `author`       | no       |                                                              |
| `description`  | no       | short blurb shown on cards + detail page                     |
| `entry`        | no       | UI file, default `ui.aroma`; relative, no `..`               |
| `plugin`       | no       | bare `.so` filename inside the package (no subdirs)          |
| `min_abi`      | no (=1)  | refuses hosts older than this                                |
| `chrome`       | no       | `"host"` (default) or `"self"` - see below                   |
| `category`     | no       | store section, e.g. `"Games"` / `"Apps"` (default `"Apps"`)  |
| `rating`       | no (=0)  | 0.0-5.0 stars; 0 means unrated ("New" in the store)          |
| `rating_count` | no (=0)  | number of ratings behind the average                         |
| `downloads`    | no (=0)  | install count shown in the store                             |
| `featured`     | no       | `true` marks an editor's-choice app (surfaced by `/api/featured`) |

## Package types

**Pure-UI package** (works everywhere, including web): just `manifest.json`
+ `ui.aroma`. The host mounts the markup into an app card at runtime
(`IncenseLoadFileIntoParent`) and provides the open/close slide chrome.
No logic of its own - use it for static screens, or pair it with
host-registered callbacks.

**Window chrome.** `chrome: "host"` (default) means the host owns the open
animation, z-order, drawer handling and close button. `chrome: "self"` means
the native plugin owns the full chrome exactly like a built-in app (it must
animate its own card, send the drawer behind, and offer its own close
button). All first-party apps use `"self"`.

**Native-plugin package** (Linux/device targets): add `plugin.so`, a C
shared library exporting exactly one symbol:

```c
#include "aroma_package.h"

static bool demo_init(const AromaPackageManifest *manifest,
                      const char *install_dir,
                      const AromaPackageHost *host,   // fonts + screen size
                      struct AromaNode *app_root) { ... }

static bool demo_build_ui(struct AromaNode *app_root) { ... }

static const AromaPackageHooks s_hooks = {
    .init = demo_init,
    .build_ui = demo_build_ui,   // any hook may be NULL
    .show = NULL, .hide = NULL, .update = NULL, .destroy = NULL,
};

const AromaPackageHooks *aroma_package_entry(void) { return &s_hooks; }
```

Build it (do **not** link libaroma - the host provides the symbols):

```sh
gcc -shared -fPIC -I<aromaui>/include -o plugin.so plugin.c
```

The host loads it with `dlopen` and checks the ABI (`min_abi` /
`AROMA_PACKAGE_ABI_VERSION`, currently 1). Link plugin hosts with
`-rdynamic` so the plugin resolves `aroma_*` from the host process.

## Developing apps as separate projects

Apps do not have to live in this repo. UI **and** logic ship bundled
inside the `.apak` (like an Android APK: code + resources in one
installable artifact), so an app is developed, versioned and built as
its own project with AromaUI as a headers-only dependency:

```sh
# Scaffold (run anywhere - the output dir is the new project):
python3 <aromaui>/tools/apak.py init my-app --id com.example.myapp \
    --name "My App" --native
cd my-app
# Build against any AromaUI checkout (headers only, never link libaroma):
cmake -S . -B build -DAromaUI_INCLUDE_DIR=<aromaui>/include
cmake --build build -j          # -> build/plugin.so
cp build/plugin.so .
python3 <aromaui>/tools/apak.py pack . -o myapp.apak
```

`pack` prunes `build/`, `dist/`, `.git/`, C sources, CMake inputs and
other dev junk automatically (an `.apak` ships only the manifest,
`plugin.so`, UI markup and assets). Install the `.apak` through Aroma Store (store server or
Settings → Packages sideload) and open it from the app drawer - the
host `dlopen`s the bundled `plugin.so` and drives `update()` every
frame, exactly like a first-party app. Keep the host close button clear
(it sits top-left at 20,20): start content around x=80.

## First-party packages (nav / media / contacts)

Navigation, Media and Contacts are not compiled into the infotainment
binary - they are real `.apak` packages built from
`examples/package_examples/{nav,media,contacts}/`:

- each builds a `plugin.so` (`MODULE` library, never linked against
  libaroma or the host),
- `tools/apak.py pack` produces `build/dist/com.aroma.<app>.apak`,
- the same build pre-installs them into `build/packages/` (system packages).

They use `chrome: "self"` and reuse their battle-tested open/close code
unchanged through a tiny in-file adapter (`aroma_package_entry` +
a static `AromaAppPlugin` mirror). Host symbols the plugins need are pinned in
`examples/car_infotainment/plugin_api.syms` (exported + LTO-protected via
`--export-dynamic-symbol`), so update that list if a plugin imports a new
host-only function. As first-party code they may use host UI-chrome
coordination (`state` nodes, `media_ui`, drawer/anim helpers) via the
host's `-rdynamic` export; third-party plugins must stick to the public
`aroma_*` API plus `AromaPackageHost` fonts. Native plugins need `dlopen`,
so the Emscripten (web) build ships settings/store/third-party UI
packages only.

Bluetooth lives in the packages, not the host: the media package owns
the A2DP/AVRCP speaker stack (`src/bt_speaker_api.c`) and the contacts
package owns the HFP/PBAP telephony stack (`src/bt_speaker_hfp.c`) -
each compiles its stack into its own `plugin.so` (linking system
D-Bus/PulseAudio itself, like any third-party native app). The host
owns no BT state and registers no BT callbacks; it reaches the stacks
through small service contracts (`media_bt_service.h`,
`contacts_bt_service.h`, resolved with `package_manager_symbol()`),
and every consumer polls the thread-safe getters (mini card monitor,
device card, call monitor, app update ticks). The contacts package
also pumps its HFP bus on its update tick (`bt_hfp_poll()`). The contacts package reads
connection/device identity from the media service (first-party
interop - telephony gates on the same connected phone the A2DP side
tracks).

Navigation is likewise self-contained: the map widget, its close
button and the fonts it uses live in the nav package (captured from
`AromaPackageHost` at init), with routing/geocoding logic beside them
- the host keeps no map state. The bundled games were born standalone
(pure `aroma.h` + `aroma_package.h`, zero host coupling).

Assets travel with their package: media bundles `assets/album_cover.jpg`,
nav bundles its `.mbtiles` tiles, routing data and POI database under
`assets/`. The build stages `assets/` into preinstalled packages and
`apak pack` includes it in the `.apak`; plugins resolve files against
the `install_dir` their `init` receives - never host asset paths.
Shared home-screen pieces stay in the host: the mini media card
(`media_home.c`), the incoming-call overlay, and the
`Packages` installer store itself.

## Host integration (for developers)

Core API (`include/aroma_package.h`, `src/core/aroma_package.c`,
`src/core/aroma_apak.c`):

- `aroma_package_parse_manifest()` / `..._load_manifest_file()` /
  `aroma_package_validate_manifest()` - parsing + policy.
- `aroma_apak_read_file()` / `aroma_apak_extract()` - minimal zip reader
  (stored + deflated, CRC-checked, rejects `..`/absolute paths; needs
  zlib, otherwise returns a clear error).
- `aroma_package_native_load()` / `..._unload()` - `dlopen` wrapper
  (stubbed on Emscripten).

The car infotainment wires it up in `package_manager.{c,h}`:
scan → drawer cards → mount UI → `dlopen` → show/hide/update, plus the
`apps/store` installer app. New hosts can copy that pattern.

New Incense loader APIs (`aroma_incense_loader.h`):

- `IncenseLoadFileIntoParent()` / `IncenseLoadStringIntoParent()` - mount a
  `Window{}` document's children into an existing node.
- `aroma_icon_codepoint_from_name()` - resolve `"AROMA_ICON_*"` for C code.

## Install / update / uninstall policy

- Install validates the manifest, refuses unknown `min_abi`, refuses
  archives with unsafe paths or CRC errors.
- Reinstalling the same `version_code` is allowed; **downgrades are
  refused**; upgrades replace the directory. A loaded package is
  **live-updated**: its UI is torn down first (same path as uninstall),
  then files are replaced and the host re-instantiates it - no restart
  needed, though a package open on screen will close.
- Uninstall deletes files immediately and hides the UI; node memory is
  reclaimed on restart (AromaUI has no node-tree destructor).
- New installs appear in the drawer **without a restart**; the store shows
  the result in its status line.

## Online store (Python server + in-app client)

The in-car store is **Aroma Store** (app drawer → Aroma Store), a simple,
generic client:

- **Games / Apps** sections (sidebar): every server package in its
  category, with live search across names, ids, authors and
  descriptions, and a hero banner featuring the first visible package.
- Each row shows the icon, name and one meta line built from real data
  only (author, category, version, size - plus rating and download
  counts when the publisher declared them; missing values are hidden,
  never invented). Tapping a row shows its description.
- **Download** buttons fetch the `.apak` with a live progress bar inside
  the row, then install through the normal
  `package_manager_install_apak` path (validation, downgrade refusal,
  live-update of loaded packages, drawer integration). Installed rows
  flip to **Open** (or **Update** when the server has a newer
  `version_code`) plus **Remove**.
- **Installed** section: the installed list with Open (Remove only for
  third-party packages - see below). Sideloading (local `.apak` path
  installer) and the store server URL setting (persisted, default
  `http://127.0.0.1:8080`) live in Settings → Packages.

It talks to a running Python server. Seed + serve in one command:

```sh
./tools/seed_store.sh ./store_repo 8080
# (builds nothing - copies build/dist/*.apak, validates, serves)
```

Or manually:

```sh
mkdir -p store_repo && cp examples/car_infotainment/build/dist/*.apak store_repo/
python3 tools/aroma_store_server.py --dir ./store_repo --port 8080
```

Server endpoints (all JSON except `/`):

- `GET /api/packages?category=Games&sort=rating` - index, newest
  `version_code` per id, with size + sha256 + download URL. Optional
  `q`/`query` (search), `category`, `featured=1`,
  `sort=downloads|rating|name`.
- `GET /api/search?q=memory` - alias for a `q`-filtered index.
- `GET /api/categories` - section list with counts.
- `GET /api/featured` - carousel entries (top-rated fallback).
- `GET /api/package/<id>` - one detail record.
- `GET /api/download/<id>` streams the `.apak` bytes.
- `/` shows a human-readable listing grouped by category.

In the car, open Aroma Store → check the server URL → hit Refresh, then
Get on any row (or View → Install). Downloads run on a worker thread
with live progress, then install through the normal
`package_manager_install_apak` path, so validation, downgrade refusal
and drawer integration all apply. Update-available rows are detected via
`version_code` comparison.
- Server is stdlib-only (`tools/aroma_store_server.py`); client is
  `apps/store/` on libcurl (native builds only — the web
  build shows a "needs native build" notice instead).

## Bundled games

`examples/package_examples/games/` ships two real, playable,
host-chrome native plugins (the recommended third-party pattern: the
host owns the window chrome + close button, the plugin only builds game
UI and drives itself from `update()`):

- **Tic Tac Toe** (`com.aroma.game.tictactoe`) - you (X) vs the computer
  (O, win/block/random), score across rounds.
- **Memory Match** (`com.aroma.game.memory`) - 4x4 flip-to-match, 8
  pairs, move counter, flip-back timer in `update()`.

They build + pack with the infotainment (`build/dist/*.apak`, served by
the store ready to install) but are deliberately **not** pre-installed,
so a fresh image starts with no games - grab them from Aroma Store.
Their manifests carry no ratings or download counts (those are only
shown when a publisher declares them), so rows show the honest core:
author, category, version and size. Cell buttons recolor via
`aroma_button_set_colors` (pinned in `plugin_api.syms`); tile faces are
label children since buttons have no public set-text API.

## First-run setup

On first boot (no completed setup in the settings file) the infotainment
shows an Android-style setup wizard over the lock screen: Welcome → device
name → Wi-Fi → Bluetooth → Display & voice → Finish. To relive the
first boot on purpose (testing), launch with `AROMA_FIRST_RUN=1` - the
persisted completion flag is ignored for that boot. Everything is
functional, not a mockup:

- Wi-Fi credentials persist and the wizard attempts a real `nmcli`
  connection when a network manager exists.
- The Bluetooth switch drives the real speaker stack;
  the device name is used for pairing.
- Dark/Light applies the theme live; the voice toggle gates the voice
  thread; Finish writes `setup_complete=1`.
- Choices live in `~/.config/aroma/infotainment.conf`
  (`$XDG_CONFIG_HOME` respected, `./aroma_setup.conf` fallback) and are
  re-applied at boot (theme, voice flag; see `setup_store.{h,c}`).
- The wizard can be re-shown programmatically via `setup_wizard_show()`.

## Animation convention

All apps and packages share one motion language
(`vehicle_view.h`: `APP_ANIM_MS` = 300ms, `APP_ANIM_OPEN_EASE` =
ease-out-cubic in, `APP_ANIM_CLOSE_EASE` = ease-in-out-quad out). The
renderer sorts draw tasks globally by `z_index`, so every node inside an
app root must sit above the root card - `vehicle_view_raise_subtree()`
enforces this for mounted content.

## Limitations (v1)

- No signature verification - only install packages you trust. (Planned:
  Ed25519 `signature` field + `apack sign`.)
- No permission sandbox - a native plugin runs with full host privileges,
  like Android's pre-install grant model but without the grant UI.
- Package `ui.aroma` files should avoid `on_click` callbacks unless the host
  registers those names (callback registration is global; prefix custom
  names with your package id).
- Asset paths in package UI resolve against the host working directory;
  prefer generated UI (native plugin) when you need bundled images.
- Drawer fits ~12 cards; excess packages load but overflow the drawer grid.
