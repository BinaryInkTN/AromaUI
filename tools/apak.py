#!/usr/bin/env python3
"""apak - AromaUI package builder/inspector (.apak files).

An .apak file is a plain ZIP containing:
    manifest.json   (required)  package metadata, see Manifest Reference
    ui.aroma        (recommended) Incense UI markup, Window{} root
    assets/...      (optional)   images & data files
    plugin.so       (optional)   native plugin (native targets only)

Usage:
    python3 tools/apak.py pack <package_dir> [-o out.apak]
    python3 tools/apak.py info <package.apak | package_dir>
    python3 tools/apak.py init <dir> --id com.example.myapp --name "My App"
    python3 tools/apak.py init <dir> --id com.example.myapp --name "My App" --native
    python3 tools/apak.py validate <package.apak | package_dir>
"""

import argparse
import json
import os
import re
import shutil
import sys
import tempfile
import zipfile

MANIFEST_NAME = "manifest.json"
# Pruned from every .apak (build outputs, VCS, caches - never ship these).
# C sources + CMake inputs are dev-only too: an .apak is an install
# artifact (manifest + plugin.so + assets/ui), never source. First-party
# packages are unaffected (they pack a staged manifest/plugin.so/assets
# dir), but `pack .` on a native scaffold must not bundle plugin.c.
PACK_SKIP_DIRS = frozenset({
    "build", "dist", ".git", ".gradle", "__pycache__", "node_modules",
    ".idea", ".vscode",
})
PACK_SKIP_FILES = frozenset({
    "CMakeLists.txt", "Makefile", "CMakeCache.txt", "README.md",
})
PACK_SKIP_SUFFIXES = (".apak", ".pyc", ".o", ".a", ".c", ".h", ".cmake")
ID_RE = re.compile(r"^[a-z0-9_]+(\.[a-z0-9_]+)+$")
# Keep in sync with AROMA_PACKAGE_ABI_VERSION in include/aroma_package.h.
AROMA_ABI_VERSION = 1

TEMPLATE_MANIFEST = {
    "id": "{id}",
    "name": "{name}",
    "version": "1.0.0",
    "version_code": 1,
    "icon": "AROMA_ICON_WIDGETS",
    "author": "",
    "description": "",
    "entry": "ui.aroma",
    "min_abi": 1,
    "category": "Apps",
    "rating": 0.0,
    "rating_count": 0,
    "downloads": 0,
    "featured": False,
}

# Scaffold for a separately-developed native app (own repo, own build):
# manifest + plugin.c + CMakeLists.txt. The plugin owns its UI and logic
# and ships bundled as plugin.so inside the .apak, Android-APK style.
TEMPLATE_NATIVE_MANIFEST = {
    "id": "{id}",
    "name": "{name}",
    "version": "1.0.0",
    "version_code": 1,
    "icon": "AROMA_ICON_WIDGETS",
    "author": "",
    "description": "",
    "category": "Apps",
    "plugin": "plugin.so",
    "min_abi": 1,
}

TEMPLATE_PLUGIN_C = """/* {name} ({id}): standalone native AromaUI app.
 *
 * Develop this in its own repo and build it on its own (see
 * CMakeLists.txt + README.md): the only input from AromaUI is the
 * public include/ dir. The host loads plugin.so with dlopen and drives
 * it through aroma_package_entry() - UI + logic ship bundled inside
 * the .apak, Android-APK style.
 *
 * Rules for third-party plugins:
 *   - use only the public aroma_* API (aroma.h) plus the AromaPackageHost
 *     fonts; never include host-app headers.
 *   - never link libaroma or the host; the host provides the symbols.
 *   - any hook may be NULL; init must reset ALL static state (a
 *     reinstall in the same process rebuilds from scratch).
 */

#include "aroma.h"
#include "aroma_package.h"

#include <stdio.h>
#include <time.h>

static AromaFont *s_font;
static AromaNode *s_counter_label;
static bool s_built;
static long s_start_time;

static bool on_reset_click(AromaNode *node, void *user_data)
{{
    (void)node;
    (void)user_data;
    s_start_time = (long)time(NULL);
    return true;
}}

static bool app_init(const AromaPackageManifest *manifest,
                     const char *install_dir,
                     const AromaPackageHost *host,
                     struct AromaNode *app_root)
{{
    (void)manifest;
    (void)install_dir;
    (void)app_root;
    s_font = host ? host->ui_font : NULL;
    s_counter_label = NULL;
    s_built = false;
    s_start_time = (long)time(NULL);
    return true;
}}

static bool app_build_ui(struct AromaNode *app_root)
{{
    if (!app_root || s_built)
        return s_built;
    AromaNode *title = aroma_label_create(app_root, "{name}",
                                          80, 24, LABEL_STYLE_LABEL_LARGE);
    if (title && s_font)
        aroma_label_set_font(title, s_font);
    s_counter_label = aroma_label_create(app_root, "Running: 0s",
                                         80, 70, LABEL_STYLE_LABEL_MEDIUM);
    if (s_counter_label && s_font)
        aroma_label_set_font(s_counter_label, s_font);
    AromaNode *reset = aroma_button_create(app_root, "Reset timer",
                                           80, 120, 160, 46);
    if (reset)
    {{
        aroma_button_set_on_click(reset, on_reset_click, NULL);
        if (s_font)
            aroma_button_set_font(reset, s_font);
        aroma_button_setup_events(reset, aroma_ui_request_redraw, NULL);
    }}
    s_built = true;
    return true;
}}

static void app_update(struct AromaNode *app_root)
{{
    (void)app_root;
    if (!s_built || !s_counter_label)
        return;
    char buf[64];
    snprintf(buf, sizeof(buf), "Running: %lds",
             (long)time(NULL) - s_start_time);
    aroma_label_set_text(s_counter_label, buf);
}}

static void app_destroy(void)
{{
    s_built = false;
    s_counter_label = NULL;
    s_font = NULL;
}}

static const AromaPackageHooks s_app_hooks = {{
    .init = app_init,
    .build_ui = app_build_ui,
    .show = NULL,
    .hide = NULL,
    .update = app_update,
    .destroy = app_destroy,
}};

const AromaPackageHooks *aroma_package_entry(void)
{{
    return &s_app_hooks;
}}
"""

TEMPLATE_NATIVE_CMAKE = """# {name}: standalone native AromaUI app (separate repo, separate build).
# Only input from AromaUI is its public include/ dir - point at any
# checkout (or an installed copy of include/):
#
#   cmake -S . -B build -DAromaUI_INCLUDE_DIR=/path/to/AromaUI/include
#   cmake --build build -j
#   python3 /path/to/AromaUI/tools/apak.py pack . -o {id}.apak
cmake_minimum_required(VERSION 3.15)
project({id}_plugin C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT DEFINED AromaUI_INCLUDE_DIR)
    set(AromaUI_INCLUDE_DIR "" CACHE PATH
        "AromaUI include directory (the include/ dir of an AromaUI checkout)")
endif()
if(NOT EXISTS "${{AromaUI_INCLUDE_DIR}}/aroma_package.h")
    message(FATAL_ERROR
        "AromaUI_INCLUDE_DIR is missing aroma_package.h: '${{AromaUI_INCLUDE_DIR}}'\\n"
        "Configure with -DAromaUI_INCLUDE_DIR=/path/to/AromaUI/include")
endif()

add_library(app_plugin MODULE
    plugin.c
)
set_target_properties(app_plugin PROPERTIES
    PREFIX ""
    SUFFIX ".so"
    OUTPUT_NAME "plugin"
)
target_include_directories(app_plugin PRIVATE
    "${{AromaUI_INCLUDE_DIR}}"
)
# NOTE: deliberately NOT linked against libaroma or the host - the host
# process provides all aroma_* symbols at dlopen time (it links with
# -rdynamic). Third-party plugins must use only the public aroma_* API.
"""

TEMPLATE_NATIVE_README = """# {name}

Standalone native AromaUI app (`{id}`). Develop, version and build this
like any own project - AromaUI is only a headers dependency.

## Build

```sh
cmake -S . -B build -DAromaUI_INCLUDE_DIR=/path/to/AromaUI/include
cmake --build build -j
# produces build/plugin.so
```

## Pack

```sh
python3 /path/to/AromaUI/tools/apak.py pack . -o {id}.apak
python3 /path/to/AromaUI/tools/apak.py info {id}.apak
```

Bump `version_code` in manifest.json on every update; the host refuses
downgrades. UI + logic ship bundled as `plugin.so` inside the `.apak`,
Android-APK style.

## Install & run

- Serve it: copy the `.apak` into a store repo and run
  `python3 /path/to/AromaUI/tools/aroma_store_server.py --dir <repo>`,
  then fetch it from the in-car Aroma Store store, or
- Sideload it: Settings → Packages → enter the `.apak` path → Install,
  then open it from the app drawer.

## API rules

- Public `aroma_*` API (aroma.h) plus the `AromaPackageHost` fonts only.
- Never include host-app headers, never link libaroma or the host.
- Any `AromaPackageHooks` hook may be NULL. `init` must reset ALL static
  state: a reinstall in the same process rebuilds from scratch.
"""


TEMPLATE_UI = """Window {{
    width: 1024
    height: 600
    title: "{name}"

    Container {{
        x: 0
        y: 0
        width: 1024
        height: 600

        Label {{
            text: "{name}"
            style: large
            x: 40
            y: 40
        }}

        Label {{
            text: "Hello from an .apak package!"
            style: medium
            color: "#888888"
            x: 40
            y: 90
        }}

        Button {{
            text: "Click Me"
            x: 40
            y: 150
            width: 160
            height: 48
        }}
    }}
}}
"""


def fail(msg):
    print(f"apak: error: {msg}", file=sys.stderr)
    return 1


def load_manifest_from_dir(path):
    mp = os.path.join(path, MANIFEST_NAME)
    if not os.path.isfile(mp):
        return None, f"missing {MANIFEST_NAME} in {path}"
    try:
        with open(mp, "r", encoding="utf-8") as f:
            return json.load(f), None
    except (json.JSONDecodeError, OSError) as e:
        return None, f"cannot parse {mp}: {e}"


def _has_drive_letter(path):
    return len(path) > 1 and path[1] == ":"


def validate_manifest(m):
    if not isinstance(m, dict):
        return "manifest root must be a JSON object"
    for field in ("id", "name", "version"):
        if not m.get(field) or not isinstance(m[field], str):
            return f"missing required string field: {field}"
    if not ID_RE.match(m["id"]):
        return 'invalid id: use reverse-dns lowercase, e.g. "com.example.calc"'
    vc = m.get("version_code", 0)
    # bool is a subclass of int - reject it explicitly.
    if not isinstance(vc, int) or isinstance(vc, bool) or vc < 0:
        return "version_code must be a non-negative integer"
    min_abi = m.get("min_abi", 1)
    if not isinstance(min_abi, int) or isinstance(min_abi, bool) or min_abi < 0:
        return "min_abi must be a non-negative integer"
    if min_abi > AROMA_ABI_VERSION:
        return f"min_abi {min_abi} is newer than this host supports ({AROMA_ABI_VERSION})"
    entry = m.get("entry", "ui.aroma")
    if (not isinstance(entry, str) or not entry or ".." in entry
            or entry.startswith("/") or entry.startswith("\\")
            or _has_drive_letter(entry)):
        return "entry must be a relative path without '..'"
    plugin = m.get("plugin", "")
    if plugin and (".." in plugin or plugin.startswith("/")
                   or plugin.startswith("\\") or "/" in plugin
                   or "\\" in plugin or _has_drive_letter(plugin)):
        return "plugin must be a bare filename inside the package"
    if m.get("chrome", "host") not in ("host", "self"):
        return 'chrome must be "host" or "self"'
    cat = m.get("category", "Apps")
    if cat is not None and (not isinstance(cat, str) or not cat.strip() or len(cat) > 63):
        return "category must be a non-empty string (max 63 chars)"
    if "rating" in m:
        try:
            r = float(m["rating"])
        except (TypeError, ValueError):
            return "rating must be a number between 0 and 5"
        if not (0.0 <= r <= 5.0):
            return "rating must be a number between 0 and 5"
    for key in ("rating_count", "downloads"):
        if key in m:
            v = m[key]
            if not isinstance(v, int) or isinstance(v, bool) or v < 0:
                return f"{key} must be a non-negative integer"
    if "featured" in m and not isinstance(m["featured"], (bool, int)):
        return "featured must be a boolean"
    return None


def _archive_name_unsafe(name):
    if not name:
        return True
    if name.startswith("/") or name.startswith("\\"):
        return True
    if _has_drive_letter(name):
        return True
    return ".." in name.replace("\\", "/").split("/")


def read_manifest_from_apak(apak_path):
    try:
        with zipfile.ZipFile(apak_path, "r") as z:
            for bad in z.namelist():
                if _archive_name_unsafe(bad):
                    return None, f"archive contains unsafe path: {bad}"
            try:
                raw = z.read(MANIFEST_NAME)
            except KeyError:
                return None, f"archive has no {MANIFEST_NAME}"
            try:
                return json.loads(raw.decode("utf-8")), None
            except (json.JSONDecodeError, UnicodeDecodeError) as e:
                return None, f"manifest in archive is not valid JSON: {e}"
    except zipfile.BadZipFile as e:
        return None, f"not a valid .apak/.zip file: {e}"
    except OSError as e:
        return None, f"cannot read {apak_path}: {e}"


def cmd_pack(args):
    src = args.package_dir
    if not os.path.isdir(src):
        return fail(f"not a directory: {src}")
    manifest, err = load_manifest_from_dir(src)
    if err:
        return fail(err)
    err = validate_manifest(manifest)
    if err:
        return fail(f"invalid manifest: {err}")
    entry = manifest.get("entry", "ui.aroma")
    entry_path = os.path.join(src, entry)
    has_ui = os.path.isfile(entry_path)
    plugin = manifest.get("plugin", "")
    if plugin:
        plugin_path = os.path.join(src, plugin)
        if not os.path.isfile(plugin_path):
            return fail(f"declared plugin '{plugin}' is missing in {src}")
        has_plugin = True
    else:
        has_plugin = False
    if not has_ui and not has_plugin:
        return fail(f"package has neither UI ({entry}) nor plugin ({plugin})")

    out = args.output
    if not out:
        out = f'{manifest["id"]}-{manifest["version"]}.apak'
    tmp_fd, tmp_path = tempfile.mkstemp(suffix=".apak")
    os.close(tmp_fd)
    try:
        with zipfile.ZipFile(tmp_path, "w", zipfile.ZIP_DEFLATED) as z:
            z.write(os.path.join(src, MANIFEST_NAME), MANIFEST_NAME)
            for root, _dirs, files in os.walk(src):
                # Prune build/VCS/cache dirs in place (os.walk honors it).
                _dirs[:] = sorted(
                    d for d in _dirs
                    if d not in PACK_SKIP_DIRS and not d.startswith("build-")
                )
                for fn in sorted(files):
                    if fn in PACK_SKIP_FILES or fn.endswith(PACK_SKIP_SUFFIXES):
                        continue
                    full = os.path.join(root, fn)
                    rel = os.path.relpath(full, src)
                    if rel == MANIFEST_NAME:
                        continue
                    z.write(full, rel)
    except OSError as e:
        os.unlink(tmp_path)
        return fail(f"cannot write archive: {e}")
    try:
        shutil.move(tmp_path, out)
    except OSError as e:
        os.unlink(tmp_path)
        return fail(f"cannot move archive to {out}: {e}")
    size = os.path.getsize(out)
    kind = "+".join([k for k, v in (("UI", has_ui), ("native", has_plugin)) if v]) or "empty"
    print(f"packed {manifest['id']} {manifest['version']} ({kind}) -> {out} "
          f"({size} bytes)")
    return 0


def cmd_info(args):
    target = args.target
    if os.path.isdir(target):
        manifest, err = load_manifest_from_dir(target)
        source = "directory"
    else:
        manifest, err = read_manifest_from_apak(target)
        source = "archive"
    if err:
        return fail(err)
    print(f"{source}: {target}")
    for key in ("id", "name", "version", "version_code", "icon", "author",
                "description", "entry", "plugin", "chrome", "min_abi",
                "category", "rating", "rating_count", "downloads", "featured"):
        if key in manifest:
            print(f"  {key}: {manifest[key]}")
    err = validate_manifest(manifest)
    print(f"  valid: {'yes' if not err else 'no: ' + err}")
    if source == "archive":
        try:
            with zipfile.ZipFile(target, "r") as z:
                print(f"  files: {len(z.namelist())}")
                for n in sorted(z.namelist()):
                    print(f"    {n}")
        except zipfile.BadZipFile:
            pass
    return 0


def cmd_validate(args):
    target = args.target
    if os.path.isdir(target):
        manifest, err = load_manifest_from_dir(target)
    else:
        manifest, err = read_manifest_from_apak(target)
    if err:
        return fail(err)
    err = validate_manifest(manifest)
    if err:
        return fail(f"invalid: {err}")
    print(f"valid: {manifest['id']} {manifest.get('version', '')}")
    return 0


def cmd_init(args):
    dest = args.dir
    os.makedirs(dest, exist_ok=True)
    mp = os.path.join(dest, MANIFEST_NAME)
    if os.path.exists(mp) and not args.force:
        return fail(f"{mp} exists (use --force to overwrite)")
    if args.native:
        return cmd_init_native(args, dest, mp)
    m = dict(TEMPLATE_MANIFEST)
    m["id"] = args.id
    m["name"] = args.name
    with open(mp, "w", encoding="utf-8") as f:
        json.dump(m, f, indent=2)
        f.write("\n")
    ui_path = os.path.join(dest, "ui.aroma")
    if not os.path.exists(ui_path) or args.force:
        with open(ui_path, "w", encoding="utf-8") as f:
            f.write(TEMPLATE_UI.format(name=args.name))
    print(f"created template in {dest}")
    print(f"  edit {mp} and ui.aroma, then:")
    print(f"  python3 tools/apak.py pack {dest}")
    return 0


def _write_init_file(path, content, force):
    if os.path.exists(path) and not force:
        return fail(f"{path} exists (use --force to overwrite)")
    with open(path, "w", encoding="utf-8") as f:
        f.write(content)
    return None


def cmd_init_native(args, dest, mp):
    """Scaffold a separately-developed native app: own repo, own CMake
    build against AromaUI's public headers; UI + logic ship bundled as
    plugin.so inside the .apak, Android-APK style."""
    m = dict(TEMPLATE_NATIVE_MANIFEST)
    m["id"] = args.id
    m["name"] = args.name
    with open(mp, "w", encoding="utf-8") as f:
        json.dump(m, f, indent=2)
        f.write("\n")
    files = {
        "plugin.c": TEMPLATE_PLUGIN_C.format(id=args.id, name=args.name),
        "CMakeLists.txt": TEMPLATE_NATIVE_CMAKE.format(id=args.id,
                                                        name=args.name),
        "README.md": TEMPLATE_NATIVE_README.format(id=args.id,
                                                    name=args.name),
    }
    for rel, content in files.items():
        err = _write_init_file(os.path.join(dest, rel), content, args.force)
        if err:
            return err
    print(f"created native app template in {dest}")
    print("  plugin.c + CMakeLists.txt + manifest.json (+ README.md)")
    print("  build:  cmake -S . -B build "
          "-DAromaUI_INCLUDE_DIR=<AromaUI>/include && cmake --build build -j")
    print(f"  pack:   python3 tools/apak.py pack {dest}")
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(prog="apak",
                                 description="AromaUI package tool (.apak)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("pack", help="build an .apak from a package directory")
    p.add_argument("package_dir")
    p.add_argument("-o", "--output", default=None)
    p.set_defaults(fn=cmd_pack)

    p = sub.add_parser("info", help="show package metadata and contents")
    p.add_argument("target")
    p.set_defaults(fn=cmd_info)

    p = sub.add_parser("validate", help="validate a package dir or .apak")
    p.add_argument("target")
    p.set_defaults(fn=cmd_validate)

    p = sub.add_parser("init", help="create a package template")
    p.add_argument("dir")
    p.add_argument("--id", required=True)
    p.add_argument("--name", required=True)
    p.add_argument("--force", action="store_true")
    p.add_argument("--native", action="store_true",
                   help="scaffold a separately-developed native app "
                        "(plugin.c + CMakeLists.txt, UI + logic bundled "
                        "as plugin.so) instead of a pure-UI package")
    p.set_defaults(fn=cmd_init)

    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
