"""Binary discovery, dependency collection, and AppDir staging."""

import os
import re
import shutil
import stat
import sys
from typing import Dict, List, Optional

from aroma_bundle_meta import _sanitize_file_name
from aroma_output import Colors, Logger
from aroma_util import run_command

_LDD_LINE_RE = re.compile(r'\s*(\S+)\s+=>\s+(\S+)\s+\(0x[0-9a-fA-F]+\)')
_LDD_DIRECT_RE = re.compile(r'^\s*(\/\S+)\s+\(0x[0-9a-fA-F]+\)')


def find_linux_binary(cwd: str) -> Optional[str]:
    build_dir = os.path.join(cwd, "build")
    base = os.path.basename(os.path.abspath(cwd))
    candidates = [
        os.path.join(build_dir, base),
        os.path.join(build_dir, "src", base),
    ]
    for c in candidates:
        if os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    if not os.path.isdir(build_dir):
        return None
    found: List[str] = []
    for root, dirs, files in os.walk(build_dir):
        if "CMakeFiles" in root:
            continue
        dirs[:] = [d for d in dirs if d != "CMakeFiles"]
        for f in files:
            fp = os.path.join(root, f)
            if f.endswith((".so", ".a", ".o", ".cmake", ".make", ".txt", ".log")):
                continue
            if not os.path.isfile(fp) or not os.access(fp, os.X_OK):
                continue
            try:
                with open(fp, "rb") as fh:
                    if fh.read(4) != b"\x7fELF":
                        continue
            except OSError:
                continue
            found.append(fp)
    if not found:
        return None
    for fp in found:
        if os.path.basename(fp) == base:
            return fp
    found.sort(key=lambda p: (len(p), p))
    return found[0]


def ensure_linux_build(cwd: str, release: bool, rebuild: bool,
                       skip_build: bool) -> str:
    existing = find_linux_binary(cwd)
    if skip_build:
        if existing:
            return existing
        Logger.error("No Linux binary found. Run 'aroma build linux' first "
                     "or drop --skip-build.")
        sys.exit(1)
    if existing and not rebuild:
        return existing
    build_dir = os.path.join(cwd, "build")
    os.makedirs(build_dir, exist_ok=True)
    build_type = "Release" if release else "Debug"
    Logger.step(f"Configuring Linux build ({build_type}) for bundling...")
    result = run_command(["cmake", "..", f"-DCMAKE_BUILD_TYPE={build_type}"],
                         cwd=build_dir)
    if result is None or result.returncode != 0:
        Logger.error("CMake configuration failed")
        sys.exit(1)
    Logger.step("Compiling...")
    result = run_command(["make", f"-j{os.cpu_count() or 4}"], cwd=build_dir)
    if result is None or result.returncode != 0:
        Logger.error("Build failed")
        sys.exit(1)
    binary = find_linux_binary(cwd)
    if not binary:
        Logger.error("Build succeeded but no executable found in build/")
        sys.exit(1)
    Logger.success(f"Built {binary}")
    return binary


def ldd_shared_libs(binary: str) -> List[str]:
    ldd = shutil.which("ldd")
    if not ldd:
        return []
    result = run_command([ldd, binary], capture_output=True)
    if not result or not result.stdout:
        return []
    out = result.stdout
    if "not a dynamic executable" in out or "statically linked" in out:
        return []
    libs: List[str] = []
    for line in out.splitlines():
        if "linux-vdso" in line or "ld.so.cache" in line:
            continue
        m = _LDD_LINE_RE.search(line)
        if m:
            path = m.group(2)
        else:
            m2 = _LDD_DIRECT_RE.search(line)
            path = m2.group(1) if m2 else ""
        if not path or not path.startswith("/") or "(" in path:
            continue
        if _is_system_lib(os.path.basename(path)):
            continue
        if os.path.isfile(path) and path not in libs:
            libs.append(path)
    return libs


# Host-provided libs that must NOT be bundled (AppImage/linuxdeploy practice).
# Bundling libc/ld-linux breaks portability; deb/rpm cover them via Depends.
_SYSTEM_LIB_RES = [
    re.compile(p) for p in (
        r'^ld-linux.*\.so.*',
        r'^libc\.so.*',
        r'^libm\.so.*',
        r'^libdl\.so.*',
        r'^librt\.so.*',
        r'^libpthread\.so.*',
        r'^libresolv\.so.*',
        r'^libutil\.so.*',
        r'^libnss_.*\.so.*',
        r'^libnsl\.so.*',
    )
]


def _is_system_lib(basename: str) -> bool:
    return any(p.match(basename) for p in _SYSTEM_LIB_RES)


def copy_shared_libs(libs: List[str], dest_lib_dir: str) -> List[str]:
    os.makedirs(dest_lib_dir, exist_ok=True)
    copied: List[str] = []
    seen_names = set()
    for lib in libs:
        name = os.path.basename(lib)
        if name in seen_names:
            continue
        seen_names.add(name)
        try:
            shutil.copy2(lib, os.path.join(dest_lib_dir, name))
            copied.append(name)
        except OSError as e:
            Logger.warning(f"Could not bundle {lib}: {e}")
    return copied


def find_project_icon(cwd: str, hint: str = "") -> Optional[str]:
    if hint:
        for cand in (hint, os.path.join(cwd, hint)):
            if os.path.isfile(cand):
                return cand
    names = ["icon.svg", "icon.png", "app.svg", "app.png",
             "logo.svg", "logo.png"]
    search_dirs = ["packaging", "assets", ".", "resources",
                   "share/icons", "icons"]
    for d in search_dirs:
        for n in names:
            p = os.path.join(cwd, d, n)
            if os.path.isfile(p):
                return p
    for pat in ("packaging/*icon*.png", "packaging/*icon*.svg",
                "assets/*icon*.png", "assets/*icon*.svg"):
        import glob as _glob
        for p in _glob.glob(os.path.join(cwd, pat)):
            if os.path.isfile(p):
                return p
    return None


def write_placeholder_icon(dest_svg: str, app_name: str):
    letter = (app_name.strip()[:1] or "A").upper()
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">'
           f'<rect width="256" height="256" rx="56" fill="#4a7dff"/>'
           f'<text x="128" y="172" font-family="sans-serif" font-size="140" '
           f'font-weight="bold" text-anchor="middle" fill="white">{letter}</text></svg>\n')
    os.makedirs(os.path.dirname(dest_svg), exist_ok=True)
    with open(dest_svg, "w") as f:
        f.write(svg)


def desktop_file_content(meta: Dict[str, str], exec_name: str,
                         icon_name: str) -> str:
    lines = [
        "[Desktop Entry]",
        "Type=Application",
        f"Name={meta['name']}",
        f"Comment={meta['short_description']}",
        f"Exec={exec_name}",
        f"Icon={icon_name}",
        f"Categories={meta['categories']}",
        "Terminal=false",
        "StartupNotify=true",
    ]
    return "\n".join(lines) + "\n"


def metainfo_xml_content(meta: Dict[str, str], desktop_id: str) -> str:
    desc = meta["description"].replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    homepage = meta["homepage"]
    url_lines = f"\n    <url type=\"homepage\">{homepage}</url>" if homepage else ""
    return (f'<?xml version="1.0" encoding="UTF-8"?>\n'
            f'<component type="desktop-application">\n'
            f'  <id>{meta["app_id"]}.desktop</id>\n'
            f'  <metadata_license>CC0-1.0</metadata_license>\n'
            f'  <project_license>{meta["license"]}</project_license>\n'
            f'  <name>{meta["name"]}</name>\n'
            f'  <summary>{meta["short_description"]}</summary>\n'
            f'  <description><p>{desc}</p></description>\n'
            f'  <launchable type="desktop-id">{desktop_id}</launchable>\n'
            f'  <provides><binary>{meta["file_name"]}</binary></provides>'
            f'{url_lines}\n'
            f'</component>\n')


def _install_icons(icon_src: Optional[str], appdir: str, icon_base: str,
                   meta: Dict[str, str]) -> str:
    scalable_dir = os.path.join(appdir, "usr", "share", "icons",
                                "hicolor", "scalable", "apps")
    os.makedirs(scalable_dir, exist_ok=True)
    if icon_src and os.path.isfile(icon_src):
        ext = os.path.splitext(icon_src)[1].lower()
        if ext == ".svg":
            shutil.copy2(icon_src, os.path.join(appdir, f"{icon_base}.svg"))
            shutil.copy2(icon_src, os.path.join(scalable_dir, f"{icon_base}.svg"))
            png_src = os.path.splitext(icon_src)[0] + ".png"
            if os.path.isfile(png_src):
                shutil.copy2(png_src, os.path.join(appdir, f"{icon_base}.png"))
            return icon_base
        if ext in (".png", ".xpm"):
            shutil.copy2(icon_src, os.path.join(appdir, f"{icon_base}{ext}"))
            for size in ("16x16", "32x32", "48x48", "64x64", "128x128",
                         "256x256", "512x512"):
                d = os.path.join(appdir, "usr", "share", "icons",
                                 "hicolor", size, "apps")
                os.makedirs(d, exist_ok=True)
                shutil.copy2(icon_src, os.path.join(d, f"{icon_base}{ext}"))
            shutil.copy2(icon_src, os.path.join(scalable_dir, f"{icon_base}{ext}"))
            return icon_base
    dest_svg = os.path.join(appdir, f"{icon_base}.svg")
    write_placeholder_icon(dest_svg, meta["name"])
    shutil.copy2(dest_svg, os.path.join(scalable_dir, f"{icon_base}.svg"))
    return icon_base


def stage_appdir(cwd: str, binary: str, meta: Dict[str, str],
                 parent: str) -> Dict[str, str]:
    appdir = os.path.join(parent, "AppDir")
    if os.path.isdir(appdir):
        shutil.rmtree(appdir)
    bin_dir = os.path.join(appdir, "usr", "bin")
    lib_dir = os.path.join(appdir, "usr", "lib")
    apps_dir = os.path.join(appdir, "usr", "share", "applications")
    for d in (bin_dir, lib_dir, apps_dir):
        os.makedirs(d, exist_ok=True)

    binary_name = os.path.basename(binary)
    shutil.copy2(binary, os.path.join(bin_dir, binary_name))
    os.chmod(os.path.join(bin_dir, binary_name),
             os.stat(os.path.join(bin_dir, binary_name)).st_mode |
             stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    libs = ldd_shared_libs(binary)
    copied = copy_shared_libs(libs, lib_dir)

    assets_src = os.path.join(cwd, "assets")
    if os.path.isdir(assets_src):
        for dest in (os.path.join(appdir, "usr", "share", meta["package"], "assets"),
                     os.path.join(appdir, "assets")):
            if os.path.isdir(dest):
                shutil.rmtree(dest)
            shutil.copytree(assets_src, dest)

    icon_base = _sanitize_file_name(meta["name"])
    icon_src = find_project_icon(cwd, meta.get("icon_hint", ""))
    icon_name = _install_icons(icon_src, appdir, icon_base, meta)

    desktop_name = f"{icon_base}.desktop"
    desktop_path = os.path.join(appdir, desktop_name)
    with open(desktop_path, "w") as f:
        f.write(desktop_file_content(meta, binary_name, icon_name))
    shutil.copy2(desktop_path, os.path.join(apps_dir, desktop_name))

    metainfo_dir = os.path.join(appdir, "usr", "share", "metainfo")
    os.makedirs(metainfo_dir, exist_ok=True)
    with open(os.path.join(metainfo_dir, f"{meta['package']}.metainfo.xml"), "w") as f:
        f.write(metainfo_xml_content(meta, desktop_name))

    apprun = os.path.join(appdir, "AppRun")
    with open(apprun, "w") as f:
        f.write(f'#!/bin/sh\nHERE="$(dirname "$(readlink -f "$0")")"\n'
                f'export LD_LIBRARY_PATH="$HERE/usr/lib:$LD_LIBRARY_PATH"\n'
                f'export AROMA_APP_DIR="$HERE"\n'
                f'ASSETS="$HERE/usr/share/{meta["package"]}/assets"\n'
                f'if [ -d "$ASSETS" ]; then export AROMA_ASSETS_DIR="$ASSETS"; fi\n'
                f'exec "$HERE/usr/bin/{binary_name}" "$@"\n')
    st = os.stat(apprun)
    os.chmod(apprun, st.st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    try:
        patchelf = shutil.which("patchelf")
        if patchelf:
            real_bin = os.path.join(bin_dir, binary_name)
            run_command([patchelf, "--set-rpath", "$ORIGIN/../../lib:$ORIGIN",
                         real_bin], capture_output=True)
    except Exception:
        pass

    return {"appdir": appdir, "binary_name": binary_name,
            "desktop_name": desktop_name, "icon_name": icon_name,
            "bundled_libs": copied}
