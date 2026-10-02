"""AppImage bundling via appimagetool."""

import os
import shutil
import stat
import urllib.request
from typing import Dict, List, Optional

from aroma_output import Logger
from aroma_util import run_command

def ensure_appimagetool(out_dir: str, arch: Dict[str, str],
                        hint: Optional[str] = None) -> Optional[str]:
    candidates: List[str] = []
    if hint:
        candidates.append(hint)
    env_hint = os.environ.get("AROMA_APPIMAGETOOL")
    if env_hint:
        candidates.append(env_hint)
    which_tool = shutil.which("appimagetool")
    if which_tool:
        candidates.append(which_tool)
    for cand in candidates:
        if cand and os.path.isfile(cand) and os.access(cand, os.X_OK):
            return cand
    machine = arch.get("machine", "x86_64")
    suffix = "x86_64" if machine == "x86_64" else ("aarch64" if machine == "aarch64" else "x86_64")
    url = (f"https://github.com/AppImage/appimagetool/releases/download/continuous/"
           f"appimagetool-{suffix}.AppImage")
    dest = os.path.join(out_dir, f"appimagetool-{suffix}.AppImage")
    if os.path.isfile(dest) and os.access(dest, os.X_OK):
        return dest
    Logger.step(f"appimagetool not found, downloading ({suffix})...")
    try:
        os.makedirs(out_dir, exist_ok=True)
        urllib.request.urlretrieve(url, dest)
        st = os.stat(dest)
        os.chmod(dest, st.st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        Logger.success(f"Downloaded appimagetool to {dest}")
        return dest
    except Exception as e:
        Logger.warning(f"Could not download appimagetool: {e}")
        return None


def build_appimage_bundle(stage: Dict[str, str], meta: Dict[str, str],
                          out_dir: str, arch: Dict[str, str],
                          tool_hint: Optional[str] = None) -> Optional[str]:
    tool = ensure_appimagetool(out_dir, arch, tool_hint)
    if not tool:
        Logger.error("appimagetool not available. Install it or set "
                     "AROMA_APPIMAGETOOL / --appimagetool. AppDir kept for manual use.")
        return None
    out = os.path.join(out_dir, f"{meta['file_name']}-{meta['version']}-{arch['appimage']}.AppImage")
    if os.path.exists(out):
        os.remove(out)
    env = os.environ.copy()
    env["ARCH"] = arch["appimage"]
    env.setdefault("APPIMAGE_EXTRACT_AND_RUN", "1")
    Logger.step("Running appimagetool...")
    result = run_command([tool, stage["appdir"], out], capture_output=True, env=env)
    if result is None or result.returncode != 0:
        detail = ((result.stderr or result.stdout).strip()
                  if result and (result.stderr or result.stdout) else "unknown error")[-2000:]
        if "fuse" in detail.lower():
            Logger.info("Retrying appimagetool with --appimage-extract-and-run (no FUSE)...")
            result2 = run_command([tool, "--appimage-extract-and-run",
                                   stage["appdir"], out],
                                  capture_output=True, env=env)
            if result2 is not None and result2.returncode == 0:
                os.chmod(out, 0o755)
                return out
        Logger.error(f"appimagetool failed: {detail}")
        return None
    if not os.path.isfile(out):
        Logger.error("appimagetool succeeded but output file missing")
        return None
    os.chmod(out, 0o755)
    return out
