"""Process, config, prompting, and host-detection helpers."""

import getpass
import json
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from typing import Any, Dict, List, Optional, Tuple

from aroma_constants import (
    _JAVA_RESERVED_WORDS,
    _PACKAGE_SEGMENT_RE,
    AROMA_INSTALL_DIR,
    JAVA_SEARCH_DIRS,
    get_default_install_path,
)
from aroma_output import Colors, Logger

def run_command(cmd: List[str], cwd: str = None, env: Dict = None,
                capture_output: bool = False, timeout: int = None) -> Optional[subprocess.CompletedProcess]:
    try:
        if capture_output:
            return subprocess.run(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=timeout)
        return subprocess.run(cmd, cwd=cwd, env=env, timeout=timeout)
    except:
        return None


def load_config(config_path: Optional[str] = None) -> Dict[str, Any]:
    paths = [config_path] if config_path else []
    paths += [
        os.path.join(os.getcwd(), "aroma.json"),
        os.path.join(os.path.dirname(os.path.realpath(__file__)), "aroma.json"),
        os.path.expanduser("~/.config/aroma/config.json"),
        os.path.join(AROMA_INSTALL_DIR, "config.json"),
    ]
    for path in paths:
        if path and os.path.exists(path):
            try:
                with open(path, 'r') as f:
                    return json.load(f)
            except:
                pass
    return {}


def save_project_config(cwd: str, config: Dict[str, Any]):
    config_path = os.path.join(cwd, "aroma.json")
    try:
        existing = {}
        if os.path.exists(config_path):
            with open(config_path, 'r') as f:
                existing = json.load(f)
        existing.update(config)
        with open(config_path, 'w') as f:
            json.dump(existing, f, indent=2)
    except Exception as e:
        Logger.warning(f"Could not save project config: {e}")


def resolve_value(key: str, config: Dict[str, Any], default: Any = None) -> Any:
    env_key = f"AROMA_{key.upper().replace('.', '_')}"
    if env_key in os.environ:
        raw = os.environ[env_key]
        if isinstance(default, list):
            return [p.strip() for p in raw.split(',') if p.strip()]
        if isinstance(default, bool):
            return raw.lower() in ('1', 'true', 'yes', 'y')
        if isinstance(default, int):
            try:
                return int(raw)
            except ValueError:
                return default
        return raw
    
    keys = key.lower().split('.')
    value = config
    for k in keys:
        if isinstance(value, dict) and k in value:
            value = value[k]
        else:
            return default
    return value


def secure_input(prompt: str, default: str = None, validator: callable = None,
                 error_msg: str = "Invalid input", secret: bool = False,
                 min_length: int = 0) -> str:
    while True:
        prompt_str = f"{Colors.BOLD}{prompt}{Colors.ENDC}"
        if default:
            prompt_str += f" [{default}]"
        prompt_str += ": "
        
        try:
            val = getpass.getpass(prompt_str).strip() if secret else input(prompt_str).strip()
        except KeyboardInterrupt:
            print()
            sys.exit(130)
        except EOFError:
            print()
            if default is not None:
                return default
            Logger.error("No input available and no default provided")
            sys.exit(1)
        
        if not val and default is not None:
            return default
        
        if not val:
            continue
        
        if min_length and len(val) < min_length:
            print(f"{Colors.FAIL}Minimum {min_length} characters required{Colors.ENDC}")
            continue
        
        if validator and not validator(val):
            print(f"{Colors.FAIL}{error_msg}{Colors.ENDC}")
            continue
        
        return val


def validate_package_name(name: str) -> bool:
    segments = name.split('.')
    if len(segments) < 2:
        return False
    for seg in segments:
        if not _PACKAGE_SEGMENT_RE.match(seg):
            return False
        if seg in _JAVA_RESERVED_WORDS:
            return False
    return True


def validate_int(val: str) -> bool:
    try:
        int(val)
        return True
    except ValueError:
        return False


def detect_java_version(java_bin: str = "java") -> Optional[Tuple[int, int]]:
    result = run_command([java_bin, "-version"], capture_output=True)
    if not result:
        return None
    output = result.stderr or result.stdout
    match = re.search(r'version "(\d+)(?:\.(\d+))?', output)
    if match:
        major = int(match.group(1))
        minor = int(match.group(2)) if match.group(2) else 0
        return (major, 0) if major > 1 else (major, minor)
    return None


def find_installed_java() -> List[Tuple[str, int]]:
    found = []
    for search_dir in JAVA_SEARCH_DIRS:
        if not os.path.isdir(search_dir):
            continue
        for entry in os.listdir(search_dir):
            java_bin = os.path.join(search_dir, entry, "bin", "java")
            if os.path.isfile(java_bin):
                version = detect_java_version(java_bin)
                if version:
                    found.append((java_bin, version[0]))
    
    sdkman_dir = os.path.expanduser("~/.sdkman/candidates/java")
    if os.path.isdir(sdkman_dir):
        for entry in os.listdir(sdkman_dir):
            java_bin = os.path.join(sdkman_dir, entry, "bin", "java")
            if os.path.isfile(java_bin):
                version = detect_java_version(java_bin)
                if version:
                    found.append((java_bin, version[0]))
    
    system_java = shutil.which("java")
    if system_java:
        version = detect_java_version(system_java)
        if version:
            found.append((system_java, version[0]))
    
    if "JAVA_HOME" in os.environ:
        java_bin = os.path.join(os.environ["JAVA_HOME"], "bin", "java")
        if os.path.isfile(java_bin):
            version = detect_java_version(java_bin)
            if version:
                found.append((java_bin, version[0]))
    
    return found


def find_emscripten_sdk() -> Optional[str]:
    candidates = []
    cwd = os.getcwd()
    candidates.append(os.path.join(cwd, "vendors", "emscripten"))
    for parent in [os.path.dirname(cwd), os.path.dirname(os.path.dirname(cwd))]:
        candidates.append(os.path.join(parent, "vendors", "emscripten"))
    script_dir = os.path.dirname(os.path.realpath(__file__))
    for _ in range(6):
        candidates.append(os.path.join(script_dir, "vendors", "emscripten"))
        script_dir = os.path.dirname(script_dir)
    install_dir = get_default_install_path()
    candidates.append(os.path.join(install_dir, "vendors", "emscripten"))
    home = os.path.expanduser("~")
    candidates.append(os.path.join(home, "Projects", "AromaUI", "vendors", "emscripten"))
    for path in candidates:
        if os.path.isdir(path) and os.path.isfile(os.path.join(path, "emsdk")):
            return path
    return None

def find_aroma_root() -> str:
    return os.path.abspath(os.path.join(os.path.dirname(os.path.realpath(__file__)), "..", ".."))


def get_connected_devices(adb_cmd: str) -> List[str]:
    result = run_command([adb_cmd, "devices"], capture_output=True)
    if not result:
        return []
    
    devices = []
    lines = result.stdout.strip().split('\n')
    for line in lines[1:]:
        if line.strip():
            parts = line.split()
            if len(parts) >= 2 and parts[1] == 'device':
                devices.append(parts[0])
    return devices


def find_package_name(cwd: str) -> Optional[str]:
    manifest = os.path.join(cwd, "android", "app", "src", "main", "AndroidManifest.xml")
    if os.path.exists(manifest):
        try:
            tree = ET.parse(manifest)
            root = tree.getroot()
            package = root.get('package')
            if package:
                return package
        except ET.ParseError:
            pass
    
    build_gradle = os.path.join(cwd, "android", "app", "build.gradle")
    if os.path.exists(build_gradle):
        with open(build_gradle, 'r') as f:
            for line in f:
                if 'namespace' in line or 'applicationId' in line:
                    match = re.search(r'["\']([^"\']+)["\']', line)
                    if match:
                        return match.group(1)
    return None
