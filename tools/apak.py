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
                for fn in sorted(files):
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
    p.set_defaults(fn=cmd_init)

    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
