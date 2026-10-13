"""External Python packages for Python projects.

Single source of truth, merged in order:
  1. ``aroma.json`` -> ``project.python.requirements`` (list of strings)
  2. ``requirements.txt`` in the project root (pip format)

On Linux the CLI installs them with pip during ``aroma build/run linux``.
On Android they are rendered into the Chaquopy ``pip { install }`` block
of ``android/app/build.gradle`` on every ``aroma build android``.
"""

import json
import os
import re
import sys
from typing import List, Tuple

BEGIN_MARKER = "// aroma:begin-pip (managed by aroma CLI, do not edit)"
END_MARKER = "// aroma:end-pip"

REQUIREMENTS_SEED = (
    "# External Python packages for this project (one per line, pip format).\n"
    "# Installed with pip on Linux (`aroma build/run linux`) and bundled\n"
    "# via Chaquopy on Android (`aroma build android`).\n"
    "# Example:\n"
    "# requests==2.32.3\n"
)


def parse_requirements_text(text: str) -> List[str]:
    reqs = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        cut = re.search(r"\s+#", line)
        if cut:
            line = line[:cut.start()].rstrip()
        if line:
            reqs.append(line)
    return reqs


def read_python_requirements(cwd: str) -> List[str]:
    reqs: List[str] = []
    try:
        with open(os.path.join(cwd, "aroma.json"), "r") as f:
            cfg = json.load(f)
        listed = cfg.get("project", {}).get("python", {}).get("requirements", [])
        if isinstance(listed, list):
            reqs.extend(s.strip() for s in listed
                        if isinstance(s, str) and s.strip())
    except (OSError, ValueError):
        pass
    try:
        with open(os.path.join(cwd, "requirements.txt"), "r") as f:
            reqs.extend(parse_requirements_text(f.read()))
    except OSError:
        pass
    seen = set()
    out = []
    for r in reqs:
        if r not in seen:
            seen.add(r)
            out.append(r)
    return out


def split_pip_entries(reqs: List[str]) -> Tuple[List[str], List[str]]:
    installs = [r for r in reqs if not r.startswith("-")]
    options = [r for r in reqs if r.startswith("-")]
    return installs, options


def groovy_escape(s: str) -> str:
    return s.replace("\\", "\\\\").replace("$", "\\$").replace('"', '\\"')


def render_chaquopy_block(requirements: List[str]) -> str:
    installs, options = split_pip_entries(requirements)
    lines = ["", "chaquopy {", "    defaultConfig {", '        version "3.11"']
    if installs or options:
        lines.append("        " + BEGIN_MARKER)
        lines.append("        pip {")
        for spec in installs:
            lines.append('            install "%s"' % groovy_escape(spec))
        for opt in options:
            lines.append('            options "%s"' % groovy_escape(opt))
        lines.append("        }")
        lines.append("        " + END_MARKER)
    lines += ["    }", "}", ""]
    return "\n".join(lines)


def sync_android_pip(android_dir: str, requirements: List[str]) -> bool:
    from aroma_output import Colors

    path = os.path.join(android_dir, "app", "build.gradle")
    try:
        with open(path, "r") as f:
            text = f.read()
    except OSError:
        print(f"{Colors.FAIL}ERROR: Android build file not found: {path}{Colors.ENDC}")
        return False

    lines = text.split("\n")
    out = []
    in_managed = False
    for line in lines:
        if BEGIN_MARKER in line:
            in_managed = True
            continue
        if END_MARKER in line:
            in_managed = False
            continue
        if in_managed:
            continue
        out.append(line)

    installs, options = split_pip_entries(requirements)
    if installs or options:
        start = None
        for i, line in enumerate(out):
            if re.match(r"\s*chaquopy\s*\{\s*$", line):
                start = i
                break
        if start is None:
            print(f"{Colors.FAIL}ERROR: No chaquopy block in {path}. "
                  f"Is this a Python project (created with --python)?{Colors.ENDC}")
            return False
        for line in out[start:]:
            if re.match(r"\s*pip\s*\{\s*$", line):
                print(f"{Colors.FAIL}ERROR: {path} has a hand-written pip block. "
                      f"Move those entries into requirements.txt or "
                      f"aroma.json (project.python.requirements) so the CLI "
                      f"can manage them.{Colors.ENDC}")
                return False
            if re.match(r"\s*\}\s*$", line):
                break
        version_idx = None
        in_default = False
        for i in range(start, len(out)):
            if re.match(r"\s*defaultConfig\s*\{\s*$", out[i]):
                in_default = True
                continue
            if in_default and re.match(r'\s*version\s+"[^"]+"\s*$', out[i]):
                version_idx = i
                break
        if version_idx is None:
            print(f"{Colors.FAIL}ERROR: Could not find chaquopy defaultConfig "
                  f"version in {path}.{Colors.ENDC}")
            return False
        block = ["        " + BEGIN_MARKER, "        pip {"]
        for spec in installs:
            block.append('            install "%s"' % groovy_escape(spec))
        for opt in options:
            block.append('            options "%s"' % groovy_escape(opt))
        block += ["        }", "        " + END_MARKER]
        out[version_idx + 1:version_idx + 1] = block
        action = "Updating" if BEGIN_MARKER in text else "Adding"
        print(f"{Colors.OKBLUE}==>{Colors.ENDC} {action} Chaquopy pip block "
              f"({len(installs)} packages, {len(options)} options)...")

    with open(path, "w") as f:
        f.write("\n".join(out))
    return True


def deps_satisfied(requirements: List[str]) -> bool:
    try:
        from packaging.requirements import Requirement
        from importlib import metadata as _md
    except ImportError:
        return False
    for req in requirements:
        if req.startswith("-"):
            return False
        try:
            parsed = Requirement(req)
        except Exception:
            return False
        if parsed.marker is not None:
            try:
                if not parsed.marker.evaluate():
                    continue
            except Exception:
                return False
        try:
            installed = _md.version(parsed.name)
        except _md.PackageNotFoundError:
            return False
        except Exception:
            return False
        if parsed.url:
            continue
        try:
            if parsed.specifier and not parsed.specifier.contains(
                    installed, prereleases=True):
                return False
        except Exception:
            return False
    return True


def ensure_linux_deps(requirements: List[str]) -> bool:
    from aroma_output import Colors
    from aroma_util import run_command

    if not requirements:
        return True
    if deps_satisfied(requirements):
        return True
    print(f"{Colors.OKBLUE}==>{Colors.ENDC} Installing Python dependencies "
          f"({len(requirements)} requirements)...")
    result = run_command(
        [sys.executable, "-m", "pip", "install",
         "--disable-pip-version-check", "-q"] + requirements,
        capture_output=True, timeout=600)
    if result is None:
        print(f"{Colors.FAIL}ERROR: pip failed to start.{Colors.ENDC}")
        return False
    if result.returncode != 0:
        tail = (result.stderr or "").strip().splitlines()[-8:]
        print(f"{Colors.FAIL}ERROR: pip install failed:{Colors.ENDC}")
        for line in tail:
            print(f"  {line}")
        if "externally-managed-environment" in (result.stderr or ""):
            print("HINT: your Python is externally managed; use a virtualenv "
                  "(python3 -m venv .venv) and re-run.")
        return False
    return True
