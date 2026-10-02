"""Portable bundle formats: unpacked dir, tarballs, and zip."""

import os
import shutil
import tarfile
import zipfile
from typing import Dict

def build_dir_bundle(stage: Dict[str, str], meta: Dict[str, str],
                     out_dir: str, arch: Dict[str, str]) -> str:
    dest = os.path.join(out_dir, f"{meta['file_name']}-{meta['version']}-{arch['generic']}")
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    shutil.copytree(stage["appdir"], dest,
                    symlinks=True)
    return dest


def _tar_mode(fmt: str) -> str:
    return {"tar.gz": "w:gz", "tar.xz": "w:xz", "tar.bz2": "w:bz2"}[fmt]


def _tar_ext(fmt: str) -> str:
    return {"tar.gz": "tar.gz", "tar.xz": "tar.xz", "tar.bz2": "tar.bz2"}[fmt]


def build_tar_bundle(stage: Dict[str, str], meta: Dict[str, str],
                     out_dir: str, arch: Dict[str, str], fmt: str) -> str:
    ext = _tar_ext(fmt)
    out = os.path.join(out_dir, f"{meta['file_name']}-{meta['version']}-{arch['generic']}.{ext}")
    root_name = f"{meta['file_name']}-{meta['version']}"
    with tarfile.open(out, _tar_mode(fmt), format=tarfile.PAX_FORMAT) as tar:
        for root, _, files in os.walk(stage["appdir"]):
            for fn in files:
                full = os.path.join(root, fn)
                arc = os.path.join(root_name, os.path.relpath(full, stage["appdir"]))
                ti = tar.gettarinfo(full, arcname=arc)
                if ti.isfile():
                    with open(full, "rb") as fh:
                        tar.addfile(ti, fh)
                else:
                    tar.addfile(ti)
    return out


def build_zip_bundle(stage: Dict[str, str], meta: Dict[str, str],
                     out_dir: str, arch: Dict[str, str]) -> str:
    out = os.path.join(out_dir, f"{meta['file_name']}-{meta['version']}-{arch['generic']}.zip")
    root_name = f"{meta['file_name']}-{meta['version']}"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zf:
        for root, _, files in os.walk(stage["appdir"]):
            for fn in files:
                full = os.path.join(root, fn)
                arc = os.path.join(root_name, os.path.relpath(full, stage["appdir"]))
                zf.write(full, arc)
    return out
