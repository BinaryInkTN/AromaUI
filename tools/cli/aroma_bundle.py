"""The aroma bundle command dispatcher."""

import os
import shutil
import sys
import tempfile
from typing import Dict, List, Optional

from aroma_bundle_appimage import build_appimage_bundle
from aroma_bundle_archives import (
    build_dir_bundle,
    build_tar_bundle,
    build_zip_bundle,
)
from aroma_bundle_deb_rpm import build_deb_bundle, build_rpm_bundle
from aroma_bundle_meta import (
    BUNDLE_FORMATS,
    get_linux_arch,
    normalize_bundle_formats,
    resolve_bundle_meta,
)
from aroma_bundle_stage import ensure_linux_build, stage_appdir
from aroma_bundle_store import build_flatpak_bundle, build_snap_bundle
from aroma_output import Colors, Logger
from aroma_util import load_config

def cmd_bundle(args):
    cwd = os.getcwd()
    config = load_config(getattr(args, "config", None))
    if getattr(args, "platform", "linux") != "linux":
        Logger.error(f"Bundling for '{args.platform}' is not supported yet. Use 'linux'.")
        sys.exit(1)

    formats = normalize_bundle_formats(getattr(args, "bundle_formats", None),
                                       getattr(args, "format", None))
    if not formats:
        formats = ["appimage", "deb", "tar.gz"]
        Logger.info(f"No format given, defaulting to {', '.join(formats)}")

    meta = resolve_bundle_meta(cwd, config, args)
    arch_arg = getattr(args, "arch", None)
    arch = get_linux_arch(None if arch_arg in (None, "", "auto") else arch_arg)

    out_dir = os.path.abspath(getattr(args, "out", None) or "dist")
    os.makedirs(out_dir, exist_ok=True)

    release = not getattr(args, "debug", False)
    binary = ensure_linux_build(cwd, release=release,
                                rebuild=bool(getattr(args, "rebuild", False)),
                                skip_build=bool(getattr(args, "skip_build", False)))
    Logger.step(f"Bundling {meta['name']} {meta['version']} ({arch['generic']}) "
                f"from {binary}")
    print(f"  Package:    {meta['package']}")
    print(f"  App-ID:     {meta['app_id']}")
    print(f"  Formats:    {', '.join(formats)}")
    print(f"  Output:     {out_dir}")

    needs_appdir = any(f in formats for f in ("appimage", "tar.gz", "tar.xz",
                                              "tar.bz2", "zip", "dir"))
    stage: Optional[Dict[str, str]] = None
    staging_parent: Optional[str] = None
    if needs_appdir:
        staging_parent = tempfile.mkdtemp(prefix="aroma-bundle-")
        stage = stage_appdir(cwd, binary, meta, staging_parent)
        Logger.success(f"Staged AppDir ({len(stage['bundled_libs'])} bundled libs)")

    results: Dict[str, str] = {}
    failures: List[str] = []
    tool_hint = getattr(args, "appimagetool", None)

    for fmt in formats:
        Logger.step(f"Building {fmt}...")
        try:
            if fmt == "dir":
                assert stage is not None
                out = build_dir_bundle(stage, meta, out_dir, arch)
            elif fmt in ("tar.gz", "tar.xz", "tar.bz2"):
                assert stage is not None
                out = build_tar_bundle(stage, meta, out_dir, arch, fmt)
            elif fmt == "zip":
                assert stage is not None
                out = build_zip_bundle(stage, meta, out_dir, arch)
            elif fmt == "deb":
                out = build_deb_bundle(cwd, binary, meta, out_dir, arch)
            elif fmt == "rpm":
                out = build_rpm_bundle(cwd, binary, meta, out_dir, arch)
            elif fmt == "appimage":
                assert stage is not None
                out = build_appimage_bundle(stage, meta, out_dir, arch, tool_hint)
            elif fmt == "flatpak":
                out = build_flatpak_bundle(cwd, binary, meta, out_dir, arch)
            elif fmt == "snap":
                out = build_snap_bundle(cwd, binary, meta, out_dir, arch)
            else:
                Logger.error(f"Unsupported format: {fmt}")
                failures.append(fmt)
                continue
            if out:
                results[fmt] = out
                Logger.success(f"{fmt}: {out}")
            else:
                failures.append(fmt)
        except SystemExit:
            raise
        except Exception as e:
            Logger.error(f"{fmt} failed: {e}")
            failures.append(fmt)

    if staging_parent and not getattr(args, "keep_staging", False):
        if stage and any(f == "appimage" and f in failures for f in failures):
            keep = os.path.join(out_dir, f"{meta['file_name']}-AppDir")
            if os.path.isdir(keep):
                shutil.rmtree(keep)
            shutil.copytree(stage["appdir"], keep, symlinks=True)
            Logger.info(f"Kept AppDir for manual appimagetool use: {keep}")
        shutil.rmtree(staging_parent, ignore_errors=True)
    elif staging_parent and getattr(args, "keep_staging", False) and stage:
        keep = os.path.join(out_dir, f"{meta['file_name']}-AppDir")
        if os.path.isdir(keep):
            shutil.rmtree(keep)
        shutil.copytree(stage["appdir"], keep, symlinks=True)
        Logger.info(f"Kept AppDir: {keep}")

    print(f"\n{Colors.BOLD}Bundle summary:{Colors.ENDC}")
    for fmt in formats:
        if fmt in results:
            print(f"  [{Colors.OKGREEN}OK{Colors.ENDC}] {fmt}: {results[fmt]}")
        else:
            print(f"  [{Colors.FAIL}XX{Colors.ENDC}] {fmt}: failed")
    if failures:
        sys.exit(1)
