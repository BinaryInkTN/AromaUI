"""Store-distributed formats: Flatpak manifests and snap metadata."""

import json
import os
import shutil
from typing import Dict, Optional

from aroma_bundle_meta import _sanitize_file_name
from aroma_bundle_stage import (
    copy_shared_libs,
    desktop_file_content,
    find_project_icon,
    ldd_shared_libs,
    metainfo_xml_content,
    write_placeholder_icon,
)
from aroma_output import Logger
from aroma_util import run_command

def build_flatpak_bundle(cwd: str, binary: str, meta: Dict[str, str],
                         out_dir: str, arch: Dict[str, str]) -> Optional[str]:
    flat_dir = os.path.join(cwd, "packaging", "flatpak")
    os.makedirs(flat_dir, exist_ok=True)
    app_id = meta["app_id"]
    binary_name = _sanitize_file_name(meta["name"])
    manifest = {
        "app-id": app_id,
        "runtime": "org.freedesktop.Platform",
        "runtime-version": "23.08",
        "sdk": "org.freedesktop.Sdk",
        "command": binary_name,
        "finish-args": [
            "--share=ipc", "--socket=x11", "--socket=wayland",
            "--socket=pulseaudio", "--device=dri",
            "--share=network", "--filesystem=host",
        ],
        "modules": [
            {
                "name": meta["package"],
                "buildsystem": "simple",
                "build-commands": [
                    f"install -Dm755 {binary_name}.bin /app/bin/{binary_name}",
                    f"install -Dm644 {meta['package']}.desktop /app/share/applications/{app_id}.desktop",
                    f"install -Dm644 {meta['package']}.metainfo.xml /app/share/metainfo/{app_id}.metainfo.xml",
                ],
                "sources": [
                    {"type": "file", "path": f"{binary_name}.bin"},
                    {"type": "file", "path": f"{meta['package']}.desktop"},
                    {"type": "file", "path": f"{meta['package']}.metainfo.xml"},
                ],
            }
        ],
    }
    icon_src = find_project_icon(cwd, meta.get("icon_hint", ""))
    if icon_src and icon_src.lower().endswith(".svg"):
        manifest["modules"][0]["build-commands"].append(
            f"install -Dm644 icon.svg /app/share/icons/hicolor/scalable/apps/{app_id}.svg")
        manifest["modules"][0]["sources"].append({"type": "file", "path": "icon.svg"})
        shutil.copy2(icon_src, os.path.join(flat_dir, "icon.svg"))
    shutil.copy2(binary, os.path.join(flat_dir, f"{binary_name}.bin"))
    with open(os.path.join(flat_dir, f"{meta['package']}.desktop"), "w") as f:
        f.write(desktop_file_content(meta, binary_name, app_id))
    with open(os.path.join(flat_dir, f"{meta['package']}.metainfo.xml"), "w") as f:
        f.write(metainfo_xml_content(meta, f"{app_id}.desktop"))
    manifest_path = os.path.join(flat_dir, f"{app_id}.json")
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)
    Logger.success(f"Flatpak manifest written: {manifest_path}")

    builder = shutil.which("flatpak-builder")
    if not builder:
        Logger.warning("flatpak-builder not found. Manifest generated; "
                       "install flatpak-builder to produce a .flatpak bundle.")
        return manifest_path
    build_tmp = os.path.join(out_dir, "flatpak-build")
    repo_dir = os.path.join(out_dir, "flatpak-repo")
    os.makedirs(build_tmp, exist_ok=True)
    os.makedirs(repo_dir, exist_ok=True)
    Logger.step("Running flatpak-builder...")
    result = run_command([builder, "--force-clean", "--repo", repo_dir,
                          build_tmp, manifest_path], capture_output=True)
    if result is None or result.returncode != 0:
        detail = ((result.stderr or result.stdout).strip()
                  if result and (result.stderr or result.stdout) else "unknown error")[-2000:]
        Logger.error(f"flatpak-builder failed: {detail}")
        return manifest_path
    flatpak_out = os.path.join(out_dir, f"{meta['file_name']}-{meta['version']}-{arch['generic']}.flatpak")
    result = run_command(["flatpak", "build-bundle", repo_dir,
                          flatpak_out, app_id], capture_output=True)
    if result is None or result.returncode != 0:
        Logger.warning("flatpak build dir ready, but 'flatpak build-bundle' failed "
                       "(flatpak CLI may be missing).")
        return build_tmp
    return flatpak_out


def build_snap_bundle(cwd: str, binary: str, meta: Dict[str, str],
                      out_dir: str, arch: Dict[str, str]) -> Optional[str]:
    snap_dir = os.path.join(cwd, "snap")
    os.makedirs(snap_dir, exist_ok=True)
    snap_name = meta["snap_name"]
    binary_name = _sanitize_file_name(meta["name"])
    stage_src = os.path.join(snap_dir, "stage")
    if os.path.isdir(stage_src):
        shutil.rmtree(stage_src)
    os.makedirs(os.path.join(stage_src, "bin"), exist_ok=True)
    os.makedirs(os.path.join(stage_src, "meta", "gui"), exist_ok=True)
    shutil.copy2(binary, os.path.join(stage_src, "bin", f"{binary_name}.bin"))
    wrapper = os.path.join(stage_src, "bin", binary_name)
    with open(wrapper, "w") as f:
        f.write(f'#!/bin/sh\nHERE="$SNAP/bin"\nexec "$HERE/{binary_name}.bin" "$@"\n')
    os.chmod(wrapper, 0o755)
    libs = ldd_shared_libs(binary)
    if libs:
        copy_shared_libs(libs, os.path.join(stage_src, "lib"))
    assets_src = os.path.join(cwd, "assets")
    if os.path.isdir(assets_src):
        shutil.copytree(assets_src, os.path.join(stage_src, "share", meta["package"], "assets"))
    icon_src = find_project_icon(cwd, meta.get("icon_hint", ""))
    gui_dir = os.path.join(snap_dir, "gui")
    os.makedirs(gui_dir, exist_ok=True)
    if icon_src and os.path.isfile(icon_src):
        ext = os.path.splitext(icon_src)[1].lower() or ".svg"
        shutil.copy2(icon_src, os.path.join(gui_dir, f"icon{ext}"))
        shutil.copy2(icon_src, os.path.join(stage_src, "meta", "gui", f"icon{ext}"))
        icon_yaml = f"snap/gui/icon{ext}"
    else:
        ext = ".svg"
        write_placeholder_icon(os.path.join(gui_dir, "icon.svg"), meta["name"])
        shutil.copy2(os.path.join(gui_dir, "icon.svg"),
                     os.path.join(stage_src, "meta", "gui", "icon.svg"))
        icon_yaml = "snap/gui/icon.svg"
    desktop_rel = "meta/gui/%s.desktop" % snap_name
    desktop_content = desktop_file_content(meta, binary_name, snap_name)
    with open(os.path.join(stage_src, "meta", "gui", f"{snap_name}.desktop"), "w") as f:
        f.write(desktop_content)
    with open(os.path.join(gui_dir, f"{snap_name}.desktop"), "w") as f:
        f.write(desktop_content)
    yaml_text = (
        f"name: {snap_name}\nversion: '{meta['version']}'\n"
        f"summary: {meta['short_description']}\n"
        f"description: |\n  {meta['description'].replace(chr(10), chr(10) + '  ')}\n"
        f"base: core22\nconfinement: strict\ngrade: stable\n"
        f"icon: {icon_yaml}\n"
        f"apps:\n  {snap_name}:\n    command: bin/{binary_name}\n"
        f"    desktop: {desktop_rel}\n"
        f"    plugs: [desktop, desktop-legacy, wayland, x11, opengl, audio-playback, network]\n"
        f"parts:\n  app:\n    plugin: dump\n    source: stage\n"
        f"    stage-packages: [libgl1, libegl1, libgles2, libcurl4]\n"
    )
    yaml_path = os.path.join(snap_dir, "snapcraft.yaml")
    with open(yaml_path, "w") as f:
        f.write(yaml_text)
    Logger.success(f"Snap metadata written: {yaml_path}")
    snapcraft = shutil.which("snapcraft")
    if not snapcraft:
        Logger.warning("snapcraft not found. Metadata generated; "
                       "install snapcraft to produce a .snap file.")
        return yaml_path
    Logger.step("Running snapcraft...")
    result = run_command([snapcraft, "pack", "--output",
                          os.path.join(out_dir, f"{snap_name}_{meta['version']}_{arch['snap']}.snap")],
                         cwd=cwd, capture_output=True)
    if result is None or result.returncode != 0:
        detail = ((result.stderr or result.stdout).strip()
                  if result and (result.stderr or result.stdout) else "unknown error")[-2000:]
        Logger.error(f"snapcraft failed: {detail}")
        return yaml_path
    outs = sorted([os.path.join(out_dir, f) for f in os.listdir(out_dir) if f.endswith(".snap")])
    return outs[-1] if outs else yaml_path
