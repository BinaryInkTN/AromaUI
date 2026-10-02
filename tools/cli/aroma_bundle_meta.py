"""Bundle formats, architecture mapping, and metadata resolution."""

import os
import platform
import re
import sys
from typing import Any, Dict, List, Optional

from aroma_output import Colors, Logger

BUNDLE_FORMATS = [
    "appimage", "deb", "rpm", "flatpak", "snap",
    "tar.gz", "tar.xz", "tar.bz2", "zip", "dir",
]

BUNDLE_ALIASES = {
    "appimg": "appimage",
    "app-image": "appimage",
    "tgz": "tar.gz",
    "txz": "tar.xz",
    "tbz2": "tar.bz2",
    "tbz": "tar.bz2",
    "tar": "tar.gz",
    "folder": "dir",
    "directory": "dir",
    "flatpack": "flatpak",
}

BUNDLE_DEFAULT_DEPENDS_DEB = "libc6, libgl1, libegl1, libgles2, libcurl4t | libcurl4"
BUNDLE_DEFAULT_REQUIRES_RPM = "glibc, mesa-libGL, mesa-libEGL, libcurl"


def normalize_bundle_formats(positional: Optional[List[str]],
                              opt_formats: Optional[List[str]]) -> List[str]:
    raw: List[str] = []
    for src in (positional or []):
        for part in src.split(","):
            part = part.strip().lower()
            if part:
                raw.append(part)
    for entry in (opt_formats or []):
        for part in entry.split(","):
            part = part.strip().lower()
            if part:
                raw.append(part)
    expanded: List[str] = []
    for item in raw:
        if item == "all":
            expanded.extend(BUNDLE_FORMATS)
            continue
        expanded.append(BUNDLE_ALIASES.get(item, item))
    seen = set()
    out: List[str] = []
    for item in expanded:
        if item not in BUNDLE_FORMATS:
            Logger.error(f"Unknown bundle format '{item}'. "
                         f"Valid: {', '.join(BUNDLE_FORMATS)} (+ aliases {', '.join(sorted(BUNDLE_ALIASES))}, all)")
            sys.exit(1)
        if item not in seen:
            seen.add(item)
            out.append(item)
    return out


def get_linux_arch(override: Optional[str] = None) -> Dict[str, str]:
    machine = (override or platform.machine() or "x86_64").strip()
    m = machine.lower()
    if m in ("x86_64", "amd64"):
        return {"machine": "x86_64", "deb": "amd64", "rpm": "x86_64",
                "appimage": "x86_64", "generic": "x86_64", "snap": "amd64"}
    if m in ("aarch64", "arm64"):
        return {"machine": "aarch64", "deb": "arm64", "rpm": "aarch64",
                "appimage": "aarch64", "generic": "aarch64", "snap": "arm64"}
    if m in ("armv7l", "armhf", "arm"):
        return {"machine": "armv7l", "deb": "armhf", "rpm": "armv7hl",
                "appimage": "armhf", "generic": "arm", "snap": "armhf"}
    if m in ("i386", "i686", "x86"):
        return {"machine": "i686", "deb": "i386", "rpm": "i686",
                "appimage": "i686", "generic": "i386", "snap": "i386"}
    Logger.warning(f"Unknown architecture '{machine}', using it verbatim")
    safe = re.sub(r'[^A-Za-z0-9_.-]', '_', machine)
    return {"machine": safe, "deb": safe, "rpm": safe,
            "appimage": safe, "generic": safe, "snap": safe}


def _sanitize_deb_name(name: str) -> str:
    s = name.strip().lower().replace("_", "-").replace(" ", "-")
    s = re.sub(r'[^a-z0-9+.-]', '-', s)
    s = re.sub(r'-+', '-', s).strip("-.")
    return s or "aroma-app"


def _sanitize_snap_name(name: str) -> str:
    s = name.strip().lower().replace("_", "-").replace(" ", "-")
    s = re.sub(r'[^a-z0-9-]', '-', s)
    s = re.sub(r'-+', '-', s).strip("-")
    return (s or "aroma-app")[:40]


def _sanitize_version_deb(version: str) -> str:
    v = version.strip() or "0.1.0"
    v = re.sub(r'[^A-Za-z0-9.+~:-]', '.', v)
    if v and not v[0].isdigit():
        v = "0." + v
    return v


def _sanitize_version_rpm(version: str) -> str:
    v = version.strip() or "0.1.0"
    v = re.sub(r'[^A-Za-z0-9.+~^]', '.', v).strip(".")
    v = v.replace("-", ".")
    if v and not v[0].isdigit():
        v = "0." + v
    return v or "0.1.0"


def _sanitize_file_name(name: str) -> str:
    s = re.sub(r'[^\w.+-]', '-', name.strip()).strip("-")
    return s or "aroma-app"


def resolve_bundle_meta(cwd: str, config: Dict[str, Any], args: Any) -> Dict[str, str]:
    project = config.get("project", {}) if isinstance(config.get("project"), dict) else {}
    bundle_cfg = config.get("bundle", {}) if isinstance(config.get("bundle"), dict) else {}
    base = os.path.basename(os.path.abspath(cwd))

    def pick(*vals, default=""):
        for v in vals:
            if v is not None and str(v).strip() != "":
                return str(v).strip()
        return default

    name = pick(getattr(args, "bundle_name", None), bundle_cfg.get("name"),
                project.get("name"), base, default=base or "AromaApp")
    version = pick(getattr(args, "bundle_version", None), bundle_cfg.get("version"),
                   project.get("version"), "0.1.0")
    description = pick(getattr(args, "bundle_description", None),
                       bundle_cfg.get("description"),
                       project.get("description"),
                       f"{name} built with AromaUI")
    maintainer = pick(getattr(args, "bundle_maintainer", None),
                      bundle_cfg.get("maintainer"), "Aroma Developer <dev@example.com>")
    lic = pick(getattr(args, "bundle_license", None),
               bundle_cfg.get("license"), project.get("license"), "MIT")
    homepage = pick(getattr(args, "bundle_homepage", None),
                    bundle_cfg.get("homepage"), project.get("homepage"), "")
    icon_hint = pick(getattr(args, "bundle_icon", None),
                     bundle_cfg.get("icon"), "")
    categories = pick(getattr(args, "bundle_categories", None),
                      bundle_cfg.get("categories"), "Utility;")
    if not categories.endswith(";"):
        categories += ";"
    pkg_raw = pick(getattr(args, "package_name", None),
                   bundle_cfg.get("package"), bundle_cfg.get("package_name"), "")
    pkg = _sanitize_deb_name(pkg_raw) if pkg_raw else _sanitize_deb_name(name)
    app_id = pick(getattr(args, "app_id", None), bundle_cfg.get("id"),
                  bundle_cfg.get("app_id"), "")
    if not app_id:
        if re.match(r'^[a-zA-Z][a-zA-Z0-9_]*(\.[a-zA-Z][a-zA-Z0-9_]*)+$', pkg):
            app_id = pkg
        elif "." in name and re.match(r'^[a-zA-Z][\w.]*$', name):
            app_id = name
        else:
            app_id = f"io.aroma.{pkg.replace('-', '_')}"
    depends = pick(getattr(args, "bundle_depends", None),
                   bundle_cfg.get("depends"), BUNDLE_DEFAULT_DEPENDS_DEB)
    requires = pick(getattr(args, "bundle_requires", None),
                    bundle_cfg.get("requires"), BUNDLE_DEFAULT_REQUIRES_RPM)
    return {
        "name": name,
        "file_name": _sanitize_file_name(name),
        "package": pkg,
        "snap_name": _sanitize_snap_name(pkg),
        "version": version,
        "version_deb": _sanitize_version_deb(version),
        "version_rpm": _sanitize_version_rpm(version),
        "description": description,
        "short_description": description.split("\n")[0][:100],
        "maintainer": maintainer,
        "license": lic,
        "homepage": homepage,
        "icon_hint": icon_hint,
        "categories": categories,
        "app_id": app_id,
        "depends": depends,
        "requires": requires,
    }
