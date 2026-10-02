"""System package formats: Debian (.deb) and RPM (.rpm)."""

import os
import re
import shutil
import stat
import tarfile
import tempfile
from typing import Dict, List, Optional

from aroma_bundle_meta import _sanitize_file_name
from aroma_bundle_stage import (
    _install_icons,
    copy_shared_libs,
    desktop_file_content,
    find_project_icon,
    ldd_shared_libs,
    metainfo_xml_content,
)
from aroma_output import Logger
from aroma_util import run_command

def build_deb_bundle(cwd: str, binary: str, meta: Dict[str, str],
                     out_dir: str, arch: Dict[str, str]) -> Optional[str]:
    if not shutil.which("dpkg-deb"):
        Logger.error("dpkg-deb not found. Install it (apt install dpkg-dev) to build .deb")
        return None
    binary_name = _sanitize_file_name(meta["name"])
    pkg = meta["package"]
    ver = meta["version_deb"]
    work = tempfile.mkdtemp(prefix="aroma-deb-")
    try:
        root = os.path.join(work, "root")
        lib_pkg = os.path.join(root, "usr", "lib", pkg)
        lib_dir = os.path.join(lib_pkg, "lib")
        os.makedirs(lib_dir, exist_ok=True)
        bin_real = os.path.join(lib_pkg, f"{binary_name}.bin")
        shutil.copy2(binary, bin_real)
        os.chmod(bin_real, os.stat(bin_real).st_mode |
                 stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        libs = ldd_shared_libs(binary)
        copy_shared_libs(libs, lib_dir)

        bin_link = os.path.join(root, "usr", "bin", binary_name)
        os.makedirs(os.path.dirname(bin_link), exist_ok=True)
        with open(bin_link, "w") as f:
            f.write(f'#!/bin/sh\nexport LD_LIBRARY_PATH="/usr/lib/{pkg}/lib:$LD_LIBRARY_PATH"\n'
                    f'exec "/usr/lib/{pkg}/{binary_name}.bin" "$@"\n')
        os.chmod(bin_link, 0o755)

        assets_src = os.path.join(cwd, "assets")
        if os.path.isdir(assets_src):
            dest = os.path.join(root, "usr", "share", pkg, "assets")
            shutil.copytree(assets_src, dest)

        apps_dir = os.path.join(root, "usr", "share", "applications")
        os.makedirs(apps_dir, exist_ok=True)
        icon_base = _sanitize_file_name(meta["name"])
        tmp_icons = os.path.join(work, "icons")
        os.makedirs(tmp_icons, exist_ok=True)
        icon_src = find_project_icon(cwd, meta.get("icon_hint", ""))
        _install_icons(icon_src, tmp_icons, icon_base, meta)
        for dirpath, _, filenames in os.walk(os.path.join(tmp_icons, "usr")):
            for fn in filenames:
                src = os.path.join(dirpath, fn)
                rel = os.path.relpath(src, os.path.join(tmp_icons, "usr"))
                dst = os.path.join(root, "usr", rel)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(src, dst)
        for ext in (".svg", ".png", ".xpm"):
            cand = os.path.join(tmp_icons, f"{icon_base}{ext}")
            if os.path.isfile(cand):
                for size in ("16x16", "32x32", "48x48", "64x64", "128x128",
                             "256x256", "512x512"):
                    d = os.path.join(root, "usr", "share", "icons",
                                     "hicolor", size, "apps")
                    if os.path.isfile(os.path.join(d, f"{icon_base}{ext}")):
                        break
                break

        desktop_name = f"{pkg}.desktop"
        with open(os.path.join(apps_dir, desktop_name), "w") as f:
            f.write(desktop_file_content(meta, f"/usr/bin/{binary_name}", icon_base))

        metainfo_dir = os.path.join(root, "usr", "share", "metainfo")
        os.makedirs(metainfo_dir, exist_ok=True)
        with open(os.path.join(metainfo_dir, f"{pkg}.metainfo.xml"), "w") as f:
            f.write(metainfo_xml_content(meta, desktop_name))

        doc_dir = os.path.join(root, "usr", "share", "doc", pkg)
        os.makedirs(doc_dir, exist_ok=True)
        with open(os.path.join(doc_dir, "copyright"), "w") as f:
            f.write(f"Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/\n"
                    f"Source: {meta['homepage'] or 'https://example.com'}\n\n"
                    f"Files: *\nCopyright: {meta['maintainer']}\nLicense: {meta['license']}\n")

        debian_dir = os.path.join(root, "DEBIAN")
        os.makedirs(debian_dir, exist_ok=True)
        desc_long = meta["description"].replace("\n", "\n ")
        control = (f"Package: {pkg}\nVersion: {ver}\nSection: utils\n"
                   f"Priority: optional\nArchitecture: {arch['deb']}\n"
                   f"Maintainer: {meta['maintainer']}\n"
                   f"Description: {meta['short_description']}\n {desc_long}\n"
                   f"Depends: {meta['depends']}\n")
        if meta["homepage"]:
            control += f"Homepage: {meta['homepage']}\n"
        with open(os.path.join(debian_dir, "control"), "w") as f:
            f.write(control)

        out = os.path.join(out_dir, f"{pkg}_{ver}_{arch['deb']}.deb")
        result = run_command(["dpkg-deb", "--build", root, out],
                             capture_output=True)
        if result is None or result.returncode != 0:
            detail = (result.stderr.strip() if result and result.stderr else "unknown error")
            Logger.error(f"dpkg-deb failed: {detail}")
            return None
        return out
    finally:
        shutil.rmtree(work, ignore_errors=True)


def build_rpm_bundle(cwd: str, binary: str, meta: Dict[str, str],
                     out_dir: str, arch: Dict[str, str]) -> Optional[str]:
    if not shutil.which("rpmbuild"):
        Logger.error("rpmbuild not found. Install it (dnf install rpm-build / apt install rpm) to build .rpm")
        return None
    binary_name = _sanitize_file_name(meta["name"])
    pkg = re.sub(r'[^a-zA-Z0-9+.-]', '-', meta["package"]).strip("-") or "aroma-app"
    ver = meta["version_rpm"]
    work = tempfile.mkdtemp(prefix="aroma-rpm-")
    try:
        topdir = os.path.join(work, "rpmbuild")
        for sub in ("BUILD", "RPMS", "SOURCES", "SPECS", "SRPMS"):
            os.makedirs(os.path.join(topdir, sub), exist_ok=True)
        payload = os.path.join(work, "payload")
        lib_pkg = os.path.join(payload, "usr", "lib", pkg)
        lib_dir = os.path.join(lib_pkg, "lib")
        os.makedirs(lib_dir, exist_ok=True)
        shutil.copy2(binary, os.path.join(lib_pkg, f"{binary_name}.bin"))
        copy_shared_libs(ldd_shared_libs(binary), lib_dir)
        wrapper = os.path.join(payload, "usr", "bin", binary_name)
        os.makedirs(os.path.dirname(wrapper), exist_ok=True)
        with open(wrapper, "w") as f:
            f.write(f'#!/bin/sh\nexport LD_LIBRARY_PATH="/usr/lib/{pkg}/lib:$LD_LIBRARY_PATH"\n'
                    f'exec "/usr/lib/{pkg}/{binary_name}.bin" "$@"\n')
        os.chmod(wrapper, 0o755)
        assets_src = os.path.join(cwd, "assets")
        if os.path.isdir(assets_src):
            shutil.copytree(assets_src, os.path.join(payload, "usr", "share", pkg, "assets"))
        apps_dir = os.path.join(payload, "usr", "share", "applications")
        os.makedirs(apps_dir, exist_ok=True)
        icon_base = _sanitize_file_name(meta["name"])
        tmp_icons = os.path.join(work, "icons")
        os.makedirs(tmp_icons, exist_ok=True)
        _install_icons(find_project_icon(cwd, meta.get("icon_hint", "")),
                       tmp_icons, icon_base, meta)
        for dirpath, _, filenames in os.walk(os.path.join(tmp_icons, "usr")):
            for fn in filenames:
                src = os.path.join(dirpath, fn)
                rel = os.path.relpath(src, os.path.join(tmp_icons, "usr"))
                dst = os.path.join(payload, "usr", rel)
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(src, dst)
        desktop_name = f"{pkg}.desktop"
        with open(os.path.join(apps_dir, desktop_name), "w") as f:
            f.write(desktop_file_content(meta, f"/usr/bin/{binary_name}", icon_base))
        metainfo_dir = os.path.join(payload, "usr", "share", "metainfo")
        os.makedirs(metainfo_dir, exist_ok=True)
        with open(os.path.join(metainfo_dir, f"{pkg}.metainfo.xml"), "w") as f:
            f.write(metainfo_xml_content(meta, desktop_name))
        payload_tar = os.path.join(topdir, "SOURCES", "payload.tar.gz")
        with tarfile.open(payload_tar, "w:gz", format=tarfile.PAX_FORMAT) as tar:
            for root, _, files in os.walk(payload):
                for fn in files:
                    full = os.path.join(root, fn)
                    arc = os.path.relpath(full, payload)
                    tar.add(full, arcname=arc)
        summary = meta["short_description"].replace("\n", " ")[:200] or pkg
        spec = (f"Name:           {pkg}\nVersion:        {ver}\nRelease:        1%{{?dist}}\n"
                f"Summary:        {summary}\nLicense:        {meta['license']}\n"
                f"URL:            {meta['homepage'] or 'https://example.com'}\n"
                f"BuildArch:      {arch['rpm']}\nRequires:       {meta['requires']}\n\n"
                f"%description\n{meta['description']}\n\n"
                f"%install\nmkdir -p %{{buildroot}}\ntar -xzf %{{SOURCE0}} -C %{{buildroot}}\n\n"
                f"%files\n/usr/bin/{binary_name}\n\"/usr/lib/{pkg}/\"\n"
                f"\"/usr/share/{pkg}/\"\n\"/usr/share/applications/{desktop_name}\"\n"
                f"\"/usr/share/icons/\"\n\"/usr/share/metainfo/{pkg}.metainfo.xml\"\n\n"
                f"%changelog\n* Tue Jan 01 2026 {meta['maintainer']} - {ver}-1\n- AromaUI bundle\n")
        spec_path = os.path.join(topdir, "SPECS", f"{pkg}.spec")
        with open(spec_path, "w") as f:
            f.write(spec)
        result = run_command(["rpmbuild", "-bb", spec_path,
                              "-D", f"_topdir {topdir}"], capture_output=True)
        if result is None or result.returncode != 0:
            detail = ((result.stderr or result.stdout).strip()
                      if result and (result.stderr or result.stdout) else "unknown error")
            Logger.error(f"rpmbuild failed: {detail}")
            return None
        rpms: List[str] = []
        for root, _, files in os.walk(os.path.join(topdir, "RPMS")):
            for fn in files:
                if fn.endswith(".rpm"):
                    rpms.append(os.path.join(root, fn))
        if not rpms:
            Logger.error("rpmbuild succeeded but no .rpm found")
            return None
        rpms.sort()
        final = os.path.join(out_dir, f"{pkg}-{ver}-1.{arch['rpm']}.rpm")
        shutil.copy2(rpms[0], final)
        return final
    finally:
        shutil.rmtree(work, ignore_errors=True)
