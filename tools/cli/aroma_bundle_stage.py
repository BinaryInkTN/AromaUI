"""Binary discovery, dependency collection, and AppDir staging."""

import os
import re
import shutil
import stat
import sys
from typing import Dict, List, Optional, Tuple
from xml.sax.saxutils import escape as _xml_escape

from aroma_output import Logger
from aroma_util import run_command

_LDD_LINE_RE = re.compile(r'\s*(\S+)\s+=>\s+(\S+)\s+\(0x[0-9a-fA-F]+\)')
_LDD_DIRECT_RE = re.compile(r'^\s*(\/\S+)\s+\(0x[0-9a-fA-F]+\)')
_SKIP_BINARY_DIRS = {"CMakeFiles", "tests", "testing", "aroma_sdk",
                     "vendors", "_deps", "ffbuild"}


def _project_language(cwd: str) -> str:
    import json
    try:
        with open(os.path.join(cwd, "aroma.json"), "r") as f:
            return json.load(f).get("project", {}).get("language", "c")
    except (OSError, ValueError):
        return "c"


def _is_elf_executable(path: str) -> bool:
    if not os.path.isfile(path) or not os.access(path, os.X_OK):
        return False
    try:
        with open(path, "rb") as fh:
            if fh.read(4) != b"\x7fELF":
                return False
    except OSError:
        return False
    return True


def find_linux_binary(cwd: str) -> Optional[str]:
    build_dir = os.path.join(cwd, "build")
    base = os.path.basename(os.path.abspath(cwd))
    candidates = [
        os.path.join(build_dir, base),
        os.path.join(build_dir, "src", base),
    ]
    for c in candidates:
        if _is_elf_executable(c):
            return c
    if not os.path.isdir(build_dir):
        return None

    # Prefer executables produced directly by the project at the top of the
    # build tree.  This avoids accidentally packaging one of the SDK's test
    # binaries when the CMake target name differs from the folder name.
    direct: List[str] = []
    for f in sorted(os.listdir(build_dir)):
        fp = os.path.join(build_dir, f)
        if f.endswith((".so", ".a", ".o", ".cmake", ".make", ".txt", ".log")):
            continue
        if _is_elf_executable(fp):
            direct.append(fp)
    for fp in direct:
        if os.path.basename(fp) == base:
            return fp
    if direct:
        direct.sort(key=lambda p: (len(p), p))
        return direct[0]

    found: List[str] = []
    for root, dirs, files in os.walk(build_dir):
        dirs[:] = [d for d in dirs if d not in _SKIP_BINARY_DIRS]
        for f in files:
            fp = os.path.join(root, f)
            if f.endswith((".so", ".a", ".o", ".cmake", ".make", ".txt",
                           ".log", ".json", ".yml", ".yaml")):
                continue
            if _is_elf_executable(fp):
                found.append(fp)
    if not found:
        return None
    for fp in found:
        if os.path.basename(fp) == base:
            return fp
    found.sort(key=lambda p: (p.count(os.sep), len(p), p))
    return found[0]


def _cmake_build_type(build_dir: str) -> Optional[str]:
    cache = os.path.join(build_dir, "CMakeCache.txt")
    try:
        with open(cache, "r", errors="replace") as fh:
            for line in fh:
                if line.startswith("CMAKE_BUILD_TYPE:STRING="):
                    return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return None


def _cargo_binary(cwd: str, release: bool) -> Optional[str]:
    profile = "release" if release else "debug"
    target_dir = os.path.join(cwd, "target", profile)
    if not os.path.isdir(target_dir):
        return None
    names: List[str] = []
    try:
        with open(os.path.join(cwd, "Cargo.toml"), "r") as fh:
            in_package = False
            for line in fh:
                line = line.strip()
                if line.startswith("["):
                    in_package = line == "[package]"
                    continue
                if in_package and line.startswith("name") and "=" in line:
                    names.append(line.split("=", 1)[1].strip().strip('"'))
    except OSError:
        pass
    for name in names:
        candidate = os.path.join(target_dir, name)
        if _is_elf_executable(candidate):
            return candidate
    found = []
    for f in sorted(os.listdir(target_dir)):
        fp = os.path.join(target_dir, f)
        if _is_elf_executable(fp) and not f.endswith(".so"):
            found.append(fp)
    return found[0] if found else None


def ensure_linux_build(cwd: str, release: bool, rebuild: bool,
                       skip_build: bool) -> str:
    language = _project_language(cwd)
    if language == "python":
        Logger.error("Bundling Python projects is not supported yet. "
                     "Use 'aroma build linux' for development or bundle a "
                     "C/Rust project.")
        sys.exit(1)
    existing = (_cargo_binary(cwd, release) if language == "rust"
                else find_linux_binary(cwd))
    if skip_build:
        if existing:
            return existing
        Logger.error("No Linux binary found. Run 'aroma build linux' first "
                     "or drop --skip-build.")
        sys.exit(1)

    if language == "rust":
        if existing and not rebuild:
            return existing
        from aroma_build import _prepare_rust_linux, _rust_cargo_env
        prep = _prepare_rust_linux(cwd)
        if prep is None:
            sys.exit(1)
        cmd = ["cargo", "build", "--manifest-path",
               os.path.join(cwd, "Cargo.toml")]
        if release:
            cmd.append("--release")
        Logger.step("Building Rust binary with cargo...")
        result = run_command(cmd, cwd=cwd, env=_rust_cargo_env(prep["lib"]),
                             timeout=1200)
        if result is None or result.returncode != 0:
            Logger.error("cargo build failed")
            sys.exit(1)
        binary = _cargo_binary(cwd, release)
        if not binary:
            Logger.error("Cargo build succeeded but no executable was found "
                         f"under target/{'release' if release else 'debug'}")
            sys.exit(1)
        Logger.success(f"Built {binary}")
        return binary

    build_dir = os.path.join(cwd, "build")
    build_type = "Release" if release else "Debug"
    current_type = _cmake_build_type(build_dir)
    type_mismatch = bool(current_type and current_type != build_type)
    if existing and not rebuild and not type_mismatch:
        return existing

    os.makedirs(build_dir, exist_ok=True)
    Logger.step(f"Configuring Linux build ({build_type}) for bundling...")
    result = run_command(["cmake", "-S", cwd, "-B", build_dir,
                          f"-DCMAKE_BUILD_TYPE={build_type}"],
                         cwd=cwd)
    if result is None or result.returncode != 0:
        Logger.error("CMake configuration failed")
        sys.exit(1)
    Logger.step("Compiling...")
    result = run_command(["cmake", "--build", build_dir, "--parallel",
                          str(os.cpu_count() or 4)], cwd=cwd)
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


def runtime_resource_entries(cwd: str,
                             meta: Dict[str, object]) -> List[Tuple[str, str]]:
    """Resolve the runtime files/dirs listed in bundle metadata.

    Paths are kept relative so an app launched with the resource root as its
    working directory still finds e.g. ``ui/app.aroma`` and ``assets/...``.
    """
    raw = meta.get("resources") or []
    entries: List[Tuple[str, str]] = []
    seen = set()
    for item in raw:
        value = str(item).strip()
        if not value:
            continue
        src = value if os.path.isabs(value) else os.path.join(cwd, value)
        if not os.path.exists(src):
            continue
        rel = os.path.normpath(value)
        if os.path.isabs(rel) or rel == ".." or rel.startswith("../"):
            rel = os.path.basename(rel)
        if rel.startswith("./"):
            rel = rel[2:]
        if not rel or rel in (".", "..") or rel in seen:
            continue
        seen.add(rel)
        entries.append((rel, src))
    return entries


def copy_runtime_resources(cwd: str, dest_root: str,
                           meta: Dict[str, object]) -> List[str]:
    """Copy runtime resources into dest_root, preserving project-relative paths."""
    copied: List[str] = []
    os.makedirs(dest_root, exist_ok=True)
    for rel, src in runtime_resource_entries(cwd, meta):
        dst = os.path.join(dest_root, rel)
        try:
            if os.path.isdir(src):
                shutil.copytree(src, dst, dirs_exist_ok=True, symlinks=True)
            else:
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(src, dst)
            copied.append(rel)
        except OSError as e:
            Logger.warning(f"Could not bundle resource {rel}: {e}")
    return copied


def launcher_script(resource_root: str, exec_path: str,
                    lib_dir: str = "") -> str:
    """POSIX launcher used by deb/rpm wrappers.

    Aroma apps resolve ``ui/`` and ``assets/`` relative to the process working
    directory.  Enter the installed resource root before exec so those lookups
    keep working after installation.
    """
    lines = [
        "#!/bin/sh",
        f'APP_ROOT="{resource_root}"',
        'export AROMA_APP_DIR="$APP_ROOT"',
        'export AROMA_RESOURCES_DIR="$APP_ROOT"',
        'if [ -d "$APP_ROOT/assets" ]; then '
        'export AROMA_ASSETS_DIR="$APP_ROOT/assets"; fi',
    ]
    if lib_dir:
        lines.append(f'export LD_LIBRARY_PATH="{lib_dir}${{LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}}"')
    lines.append('cd "$APP_ROOT" 2>/dev/null || true')
    lines.append(f'exec "{exec_path}" "$@"')
    return "\n".join(lines) + "\n"


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
    letter = _xml_escape((app_name.strip()[:1] or "A").upper())
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


def metainfo_xml_content(meta: Dict[str, str], desktop_id: str,
                          binary_name: str = "") -> str:
    desc = _xml_escape(meta["description"])
    name = _xml_escape(meta["name"])
    summary = _xml_escape(meta["short_description"])
    license_id = _xml_escape(meta["license"])
    homepage = _xml_escape(meta["homepage"])
    url_lines = f"\n    <url type=\"homepage\">{homepage}</url>" if homepage else ""
    provides = _xml_escape(binary_name or meta.get("file_name", ""))
    desktop_file = (desktop_id if desktop_id.endswith(".desktop")
                    else f"{desktop_id}.desktop")
    return (f'<?xml version="1.0" encoding="UTF-8"?>\n'
            f'<component type="desktop-application">\n'
            f'  <id>{desktop_file}</id>\n'
            f'  <metadata_license>CC0-1.0</metadata_license>\n'
            f'  <project_license>{license_id}</project_license>\n'
            f'  <name>{name}</name>\n'
            f'  <summary>{summary}</summary>\n'
            f'  <description><p>{desc}</p></description>\n'
            f'  <launchable type="desktop-id">{desktop_file}</launchable>\n'
            f'  <provides><binary>{provides}</binary></provides>'
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

    # Keep both layouts: the AppDir root is the working directory used by
    # AppRun, while usr/share/<package> is the install location used by
    # deb/rpm/flatpak/snap launchers.
    copy_runtime_resources(cwd, appdir, meta)
    share_root = os.path.join(appdir, "usr", "share", meta["package"])
    os.makedirs(share_root, exist_ok=True)
    copy_runtime_resources(cwd, share_root, meta)

    desktop_id = meta["desktop_id"]
    icon_base = desktop_id
    icon_src = find_project_icon(cwd, meta.get("icon_hint", ""))
    icon_name = _install_icons(icon_src, appdir, icon_base, meta)

    desktop_name = f"{desktop_id}.desktop"
    desktop_path = os.path.join(appdir, desktop_name)
    with open(desktop_path, "w") as f:
        f.write(desktop_file_content(meta, binary_name, icon_name))
    shutil.copy2(desktop_path, os.path.join(apps_dir, desktop_name))

    metainfo_dir = os.path.join(appdir, "usr", "share", "metainfo")
    os.makedirs(metainfo_dir, exist_ok=True)
    with open(os.path.join(metainfo_dir, f"{desktop_id}.metainfo.xml"), "w") as f:
        f.write(metainfo_xml_content(meta, desktop_name, binary_name))

    apprun = os.path.join(appdir, "AppRun")
    with open(apprun, "w") as f:
        f.write(f'#!/bin/sh\nHERE="$(dirname "$(readlink -f "$0")")"\n'
                f'export LD_LIBRARY_PATH="$HERE/usr/lib${{LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}}"\n'
                f'export AROMA_APP_DIR="$HERE"\n'
                f'export AROMA_RESOURCES_DIR="$HERE"\n'
                f'ASSETS="$HERE/usr/share/{meta["package"]}/assets"\n'
                f'if [ -d "$ASSETS" ]; then export AROMA_ASSETS_DIR="$ASSETS"; fi\n'
                f'cd "$HERE" || exit 1\n'
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
