#!/usr/bin/env python3

import argparse
import json
import os
import platform
import re
import select
import shutil
import stat
import subprocess
import sys
import termios
import time
import tty
import xml.etree.ElementTree as ET
from typing import Any, Dict, List, Optional, Tuple

# Unified Path Constants
AROMA_DEFAULT_INSTALL_DIR = ".aroma"
AROMA_DEFAULT_SDK_DIR = "Android/Sdk"

def get_default_install_path() -> str:
    """Get default Aroma installation path"""
    home = os.path.expanduser("~")
    aroma_home = os.environ.get("AROMA_HOME")
    if aroma_home:
        return aroma_home
    return os.path.join(home, AROMA_DEFAULT_INSTALL_DIR)

def get_default_sdk_path() -> str:
    """Get default Android SDK path"""
    home = os.path.expanduser("~")
    android_home = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    if android_home:
        return android_home
    return os.path.join(home, AROMA_DEFAULT_SDK_DIR)

AROMA_INSTALL_DIR = get_default_install_path()
AROMA_SDK_DIR = get_default_sdk_path()

JAVA_SEARCH_DIRS = [
    "/usr/lib/jvm",
    "/Library/Java/JavaVirtualMachines",
]

SDK_SEARCH_PATHS = [
    AROMA_SDK_DIR,
    "~/Android/Sdk",
    "/usr/lib/android-sdk",
    "/Library/Android/sdk",
    "C:\\Android\\Sdk",
]

AROMA_COMPATIBLE_NDK_VERSIONS = [
    "25.2.9519653",
    "25.1.8937393",
    "24.0.8215888",
    "23.2.8568313",
]

PREFERRED_GRADLE_VERSIONS = ["8.4", "8.5", "8.6", "8.7"]

_ORIGINAL_TERMIOS = None


def get_terminal_size() -> Tuple[int, int]:
    try:
        size = os.get_terminal_size()
        return (size.columns, size.lines)
    except (OSError, ValueError):
        return (80, 24)


def set_terminal_raw():
    global _ORIGINAL_TERMIOS
    if sys.stdin.isatty():
        _ORIGINAL_TERMIOS = termios.tcgetattr(sys.stdin)
        tty.setraw(sys.stdin)


def restore_terminal():
    global _ORIGINAL_TERMIOS
    if _ORIGINAL_TERMIOS is not None and sys.stdin.isatty():
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, _ORIGINAL_TERMIOS)
        _ORIGINAL_TERMIOS = None


def get_keypress() -> Optional[str]:
    if not sys.stdin.isatty():
        return None
    try:
        if select.select([sys.stdin], [], [], 0.1)[0]:
            ch = sys.stdin.read(1)
            if ch == '\x03':
                raise KeyboardInterrupt()
            if ch == '\x1b':
                seq = ''
                if select.select([sys.stdin], [], [], 0.05)[0]:
                    seq += sys.stdin.read(2)
                return '\x1b' + seq
            return ch
    except (IOError, OSError):
        pass
    return None


class Colors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'
    REVERSE = '\033[7m'
    
    @classmethod
    def disable(cls):
        for attr in dir(cls):
            if not attr.startswith('_') and not callable(getattr(cls, attr)):
                val = getattr(cls, attr)
                if isinstance(val, str):
                    setattr(cls, attr, '')


class Logger:
    _verbose = False
    
    @classmethod
    def setup(cls, verbose: bool = False):
        cls._verbose = verbose
    
    @classmethod
    def info(cls, msg: str):
        if cls._verbose:
            print(f"{Colors.OKCYAN}INFO {msg}{Colors.ENDC}")


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


class Detector:
    def __init__(self):
        # Use unified SDK path resolution
        self.sdk_root = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
        if not self.sdk_root:
            for p in SDK_SEARCH_PATHS:
                path = os.path.expanduser(p)
                if os.path.isdir(path):
                    self.sdk_root = path
                    break
        if not self.sdk_root:
            self.sdk_root = AROMA_SDK_DIR
        
        # Use unified install path
        self.install_path = AROMA_INSTALL_DIR
    
    def detect_all_gradle(self) -> List[str]:
        """Detect all installed Gradle versions."""
        installed = []
        
        # Check Aroma install path first
        aroma_gradle = os.path.join(self.install_path, "gradle")
        if os.path.isdir(aroma_gradle):
            for entry in sorted(os.listdir(aroma_gradle)):
                if entry.startswith("gradle-") and os.path.isdir(os.path.join(aroma_gradle, entry)):
                    version = entry.replace("gradle-", "")
                    if os.path.isfile(os.path.join(aroma_gradle, entry, "bin", "gradle")):
                        if version not in installed:
                            installed.append(version)
        
        # Check legacy path for compatibility
        legacy_gradle = os.path.expanduser("~/aroma/gradle")
        if os.path.isdir(legacy_gradle) and legacy_gradle != aroma_gradle:
            for entry in sorted(os.listdir(legacy_gradle)):
                if entry.startswith("gradle-") and os.path.isdir(os.path.join(legacy_gradle, entry)):
                    version = entry.replace("gradle-", "")
                    if os.path.isfile(os.path.join(legacy_gradle, entry, "bin", "gradle")):
                        if version not in installed:
                            installed.append(version)
        
        # Check system Gradle wrapper dists
        wrapper_dists = os.path.expanduser("~/.gradle/wrapper/dists")
        if os.path.isdir(wrapper_dists):
            for entry in sorted(os.listdir(wrapper_dists)):
                if entry.startswith("gradle-"):
                    version = entry.replace("gradle-", "").split("-")[0]
                    if version not in installed:
                        installed.append(version)
        
        # Check Gradle wrapper properties for version
        wrapper_props_locations = [
            "gradle/wrapper/gradle-wrapper.properties",
            "android/gradle/wrapper/gradle-wrapper.properties",
        ]
        
        for loc in wrapper_props_locations:
            if os.path.exists(loc):
                try:
                    with open(loc, 'r') as f:
                        for line in f:
                            if "distributionUrl" in line:
                                match = re.search(r'gradle-([\d.]+)', line)
                                if match:
                                    version = match.group(1)
                                    if version not in installed:
                                        installed.append(version)
                                    break
                except:
                    pass
        
        # Check system Gradle installation
        system_gradle = shutil.which("gradle")
        if system_gradle:
            result = run_command([system_gradle, "--version"], capture_output=True)
            if result and result.returncode == 0:
                version_match = re.search(r'Gradle (\S+)', result.stdout)
                if version_match:
                    version = version_match.group(1)
                    if version not in installed:
                        installed.append(version)
        
        return installed
    
    def detect_gradle_details(self) -> Dict[str, Any]:
        """Get detailed Gradle information."""
        details = {
            "installed": False,
            "versions": [],
            "wrapper_available": False,
            "wrapper_path": None,
            "wrapper_props_available": False,
            "system_gradle": None,
        }
        
        versions = self.detect_all_gradle()
        if versions:
            details["installed"] = True
            details["versions"] = versions
        
        # Check for Gradle wrapper in multiple locations
        wrapper_locations = [
            "gradlew",                                              # Current directory
            "gradlew.bat",                                          # Windows current directory
            os.path.join(self.install_path, "gradlew"),            # Aroma install path
            os.path.join(self.install_path, "bin", "gradlew"),     # Aroma bin
            "android/gradlew",                                      # Android project
            "android/gradlew.bat",                                  # Windows Android project
        ]
        
        for loc in wrapper_locations:
            if os.path.exists(loc):
                details["wrapper_available"] = True
                details["wrapper_path"] = os.path.abspath(loc)
                break
        
        # Check for Gradle wrapper properties (project configured for Gradle)
        wrapper_props_locations = [
            "gradle/wrapper/gradle-wrapper.properties",
            "android/gradle/wrapper/gradle-wrapper.properties",
        ]
        
        for loc in wrapper_props_locations:
            if os.path.exists(loc):
                details["wrapper_props_available"] = True
                # Read the distribution URL to get the Gradle version
                try:
                    with open(loc, 'r') as f:
                        for line in f:
                            if "distributionUrl" in line:
                                # Extract version from URL
                                match = re.search(r'gradle-([\d.]+)', line)
                                if match:
                                    version = match.group(1)
                                    if version not in details["versions"]:
                                        details["versions"].append(version)
                                        details["installed"] = True
                                break
                except:
                    pass
                break
        
        # Check system Gradle
        system_gradle = shutil.which("gradle")
        if system_gradle:
            result = run_command([system_gradle, "--version"], capture_output=True)
            if result and result.returncode == 0:
                details["system_gradle"] = system_gradle
                version_match = re.search(r'Gradle (\S+)', result.stdout)
                if version_match:
                    version = version_match.group(1)
                    if version not in details["versions"]:
                        details["versions"].append(version)
        
        # Sort versions
        details["versions"].sort(reverse=True)
        
        return details
    
    def detect_all_sdk(self) -> List[str]:
        """Detect all installed SDK platform versions."""
        platforms_dir = os.path.join(self.sdk_root, "platforms")
        if not os.path.isdir(platforms_dir):
            return []
        
        versions = []
        for entry in os.listdir(platforms_dir):
            if entry.startswith("android-") and os.path.isdir(os.path.join(platforms_dir, entry)):
                v = entry.replace("android-", "")
                if os.path.exists(os.path.join(platforms_dir, entry, "android.jar")):
                    versions.append(v)
        versions.sort(key=lambda x: int(x) if x.isdigit() else 0, reverse=True)
        return versions
    
    def detect_all_build_tools(self) -> List[str]:
        """Detect all installed Build Tools versions."""
        bt_dir = os.path.join(self.sdk_root, "build-tools")
        if not os.path.isdir(bt_dir):
            return []
        
        versions = []
        for entry in os.listdir(bt_dir):
            bt_path = os.path.join(bt_dir, entry)
            if os.path.isdir(bt_path):
                # Verify it has essential tools
                has_aapt = os.path.exists(os.path.join(bt_path, "aapt")) or \
                          os.path.exists(os.path.join(bt_path, "aapt2"))
                has_zipalign = os.path.exists(os.path.join(bt_path, "zipalign"))
                if has_aapt or has_zipalign:
                    versions.append(entry)
        versions.sort(reverse=True)
        return versions
    
    def detect_all_ndk(self) -> List[str]:
        """Detect all installed NDK versions."""
        ndk_root = os.path.join(self.sdk_root, "ndk")
        if not os.path.isdir(ndk_root):
            return []
        
        installed = []
        for entry in os.listdir(ndk_root):
            ndk_path = os.path.join(ndk_root, entry)
            if os.path.isdir(ndk_path) and entry != "sources":
                props_file = os.path.join(ndk_path, "source.properties")
                version = entry
                if os.path.exists(props_file):
                    try:
                        with open(props_file, 'r') as f:
                            for line in f:
                                if line.startswith("Pkg.Revision"):
                                    version = line.split("=")[1].strip()
                                    break
                    except:
                        pass
                installed.append(version)
        
        # Sort with Aroma-compatible first
        compat = [v for v in installed if v in AROMA_COMPATIBLE_NDK_VERSIONS]
        other = [v for v in installed if v not in AROMA_COMPATIBLE_NDK_VERSIONS]
        compat.sort(reverse=True)
        other.sort(reverse=True)
        return compat + other
    
    def detect_ndk_details(self) -> Dict[str, Any]:
        """Get detailed NDK information."""
        details = {
            "installed": False,
            "versions": [],
            "compatible": [],
            "ndk_build_available": False,
            "ndk_stack_available": False,
        }
        
        versions = self.detect_all_ndk()
        if versions:
            details["installed"] = True
            details["versions"] = versions
            details["compatible"] = [v for v in versions if v in AROMA_COMPATIBLE_NDK_VERSIONS]
            
            # Check for ndk-build in first version
            if versions:
                ndk_dir = os.path.join(self.sdk_root, "ndk", versions[0])
                details["ndk_build_available"] = os.path.exists(os.path.join(ndk_dir, "ndk-build"))
                details["ndk_stack_available"] = os.path.exists(os.path.join(ndk_dir, "ndk-stack"))
        
        return details
    
    def detect_all_cmake(self) -> List[str]:
        """Detect all installed CMake versions in SDK."""
        cmake_dir = os.path.join(self.sdk_root, "cmake")
        if not os.path.isdir(cmake_dir):
            return []
        
        versions = []
        for entry in os.listdir(cmake_dir):
            cmake_path = os.path.join(cmake_dir, entry)
            if os.path.isdir(cmake_path):
                # Verify it has cmake binary
                if os.path.exists(os.path.join(cmake_path, "bin", "cmake")):
                    versions.append(entry)
        versions.sort(reverse=True)
        return versions
    
    def detect_platform_tools(self) -> bool:
        """Check if platform tools are installed."""
        adb = os.path.join(self.sdk_root, "platform-tools", "adb")
        if platform.system() == "Windows":
            adb += ".exe"
        return os.path.exists(adb) or shutil.which("adb") is not None
    
    def detect_platform_tools_details(self) -> Dict[str, Any]:
        """Get detailed platform tools information."""
        details = {
            "installed": False,
            "adb_version": None,
            "fastboot_available": False,
            "tools": [],
        }
        
        pt_dir = os.path.join(self.sdk_root, "platform-tools")
        if os.path.isdir(pt_dir):
            details["installed"] = True
            
            # Check ADB
            adb_path = os.path.join(pt_dir, "adb")
            if platform.system() == "Windows":
                adb_path += ".exe"
            
            if os.path.exists(adb_path):
                result = run_command([adb_path, "version"], capture_output=True)
                if result and result.returncode == 0:
                    version_match = re.search(r'version (\S+)', result.stdout)
                    if version_match:
                        details["adb_version"] = version_match.group(1)
            
            # Check fastboot
            fastboot_path = os.path.join(pt_dir, "fastboot")
            if platform.system() == "Windows":
                fastboot_path += ".exe"
            details["fastboot_available"] = os.path.exists(fastboot_path)
            
            # List available tools
            if os.path.isdir(pt_dir):
                for item in os.listdir(pt_dir):
                    if os.path.isfile(os.path.join(pt_dir, item)):
                        details["tools"].append(item)
        
        return details
    
    def detect_emulator(self) -> bool:
        """Check if emulator is installed."""
        emu = os.path.join(self.sdk_root, "emulator", "emulator")
        if platform.system() == "Windows":
            emu += ".exe"
        return os.path.exists(emu)
    
    def detect_emulator_details(self) -> Dict[str, Any]:
        """Get detailed emulator information."""
        details = {
            "installed": False,
            "version": None,
            "system_images": [],
            "avds": [],
        }
        
        emu_dir = os.path.join(self.sdk_root, "emulator")
        if os.path.isdir(emu_dir):
            details["installed"] = True
            
            # Get emulator version
            emu_bin = os.path.join(emu_dir, "emulator")
            if platform.system() == "Windows":
                emu_bin += ".exe"
            
            if os.path.exists(emu_bin):
                result = run_command([emu_bin, "-version"], capture_output=True)
                if result and result.returncode == 0:
                    version_match = re.search(r'version (\S+)', result.stdout)
                    if version_match:
                        details["version"] = version_match.group(1)
            
            # Check system images
            sys_img_dir = os.path.join(self.sdk_root, "system-images")
            if os.path.isdir(sys_img_dir):
                for android_ver in os.listdir(sys_img_dir):
                    android_path = os.path.join(sys_img_dir, android_ver)
                    if os.path.isdir(android_path):
                        for img_type in os.listdir(android_path):
                            img_path = os.path.join(android_path, img_type)
                            if os.path.isdir(img_path):
                                for arch in os.listdir(img_path):
                                    if os.path.isdir(os.path.join(img_path, arch)):
                                        details["system_images"].append(f"{android_ver}/{img_type}/{arch}")
            
            # Check AVDs
            avd_dir = os.path.expanduser("~/.android/avd")
            if os.path.isdir(avd_dir):
                for avd in os.listdir(avd_dir):
                    if avd.endswith(".avd"):
                        details["avds"].append(avd.replace(".avd", ""))
        
        return details
    
    def detect_licenses(self) -> bool:
        """Check if Android SDK licenses are accepted."""
        lic_dir = os.path.join(self.sdk_root, "licenses")
        return os.path.isdir(lic_dir) and len(os.listdir(lic_dir)) > 0
    
    def detect_system_tools(self) -> Dict[str, Any]:
        """Detect system build tools."""
        tools = {
            "cmake": {"available": False, "version": None, "path": None},
            "ninja": {"available": False, "version": None, "path": None},
            "gcc": {"available": False, "version": None, "path": None},
            "gplusplus": {"available": False, "version": None, "path": None},
            "make": {"available": False, "version": None, "path": None},
            "git": {"available": False, "version": None, "path": None},
            "adb": {"available": False, "version": None, "path": None},
        }
        
        # Check cmake
        cmake_path = shutil.which("cmake")
        if cmake_path:
            tools["cmake"]["available"] = True
            tools["cmake"]["path"] = cmake_path
            result = run_command([cmake_path, "--version"], capture_output=True)
            if result and result.returncode == 0:
                version_match = re.search(r'version (\S+)', result.stdout)
                if version_match:
                    tools["cmake"]["version"] = version_match.group(1)
        
        # Check ninja
        ninja_path = shutil.which("ninja")
        if ninja_path:
            tools["ninja"]["available"] = True
            tools["ninja"]["path"] = ninja_path
            result = run_command([ninja_path, "--version"], capture_output=True)
            if result and result.returncode == 0:
                tools["ninja"]["version"] = result.stdout.strip()
        
        # Check gcc
        gcc_path = shutil.which("gcc")
        if gcc_path:
            tools["gcc"]["available"] = True
            tools["gcc"]["path"] = gcc_path
            result = run_command([gcc_path, "--version"], capture_output=True)
            if result and result.returncode == 0:
                version_match = re.search(r'(\d+\.\d+\.\d+)', result.stdout)
                if version_match:
                    tools["gcc"]["version"] = version_match.group(1)
        
        # Check g++
        gpp_path = shutil.which("g++")
        if gpp_path:
            tools["gplusplus"]["available"] = True
            tools["gplusplus"]["path"] = gpp_path
            result = run_command([gpp_path, "--version"], capture_output=True)
            if result and result.returncode == 0:
                version_match = re.search(r'(\d+\.\d+\.\d+)', result.stdout)
                if version_match:
                    tools["gplusplus"]["version"] = version_match.group(1)
        
        # Check make
        make_path = shutil.which("make")
        if make_path:
            tools["make"]["available"] = True
            tools["make"]["path"] = make_path
            result = run_command([make_path, "--version"], capture_output=True)
            if result and result.returncode == 0:
                version_match = re.search(r'GNU Make (\S+)', result.stdout)
                if version_match:
                    tools["make"]["version"] = version_match.group(1)
        
        # Check git
        git_path = shutil.which("git")
        if git_path:
            tools["git"]["available"] = True
            tools["git"]["path"] = git_path
            result = run_command([git_path, "--version"], capture_output=True)
            if result and result.returncode == 0:
                tools["git"]["version"] = result.stdout.strip().replace("git version ", "")
        
        # Check adb (system)
        adb_path = shutil.which("adb")
        if adb_path:
            tools["adb"]["available"] = True
            tools["adb"]["path"] = adb_path
            result = run_command([adb_path, "version"], capture_output=True)
            if result and result.returncode == 0:
                version_match = re.search(r'version (\S+)', result.stdout)
                if version_match:
                    tools["adb"]["version"] = version_match.group(1)
        
        return tools


class TUI:
    @staticmethod
    def clear():
        sys.stdout.write('\033[2J\033[H')
        sys.stdout.flush()
    
    @staticmethod
    def clear_line():
        sys.stdout.write('\033[2K\r')
        sys.stdout.flush()
    
    @staticmethod
    def move(row: int, col: int = 0):
        sys.stdout.write(f'\033[{row};{col}H')
        sys.stdout.flush()
    
    @staticmethod
    def hide_cursor():
        sys.stdout.write('\033[?25l')
        sys.stdout.flush()
    
    @staticmethod
    def show_cursor():
        sys.stdout.write('\033[?25h')
        sys.stdout.flush()
    
    @staticmethod
    def write(text: str):
        sys.stdout.write(text)
        sys.stdout.flush()
    
    @staticmethod
    def draw_header():
        w = get_terminal_size()[0]
        TUI.move(1, 0)
        TUI.clear_line()
        TUI.write(f"\033[1;36m{' AromaUI CLI '.center(w, '=')}\033[0m")
    
    @staticmethod
    def draw_footer(text: str = ""):
        h, w = get_terminal_size()
        TUI.move(h, 0)
        TUI.clear_line()
        TUI.write(f"\033[1;30;47m{text.center(w)}\033[0m")


class Menu:
    def __init__(self, title: str, options: List[str], default_idx: int = 0):
        self.title = title
        self.options = options
        self.selected = default_idx
        self.scroll_offset = 0
        self.max_visible = get_terminal_size()[1] - 10
    
    def draw(self, row: int, col: int):
        w = get_terminal_size()[0]
        
        # Title
        TUI.move(row, col)
        TUI.clear_line()
        TUI.write(f"{Colors.BOLD}{self.title}{Colors.ENDC}")
        
        # Options
        visible_start = self.scroll_offset
        visible_end = min(visible_start + self.max_visible, len(self.options))
        
        for i in range(visible_start, visible_end):
            TUI.move(row + 1 + i - visible_start, col)
            TUI.clear_line()
            
            prefix = "  "
            if i == self.selected:
                prefix = f"{Colors.REVERSE} >"
                suffix = f"  {Colors.ENDC}"
            else:
                prefix = "   "
                suffix = ""
            
            option_text = self.options[i]
            max_width = w - col - 10
            if len(option_text) > max_width:
                option_text = option_text[:max_width - 3] + "..."
            
            # Mark Aroma-compatible
            badge = ""
            option_lower = option_text.lower()
            if "25.2" in option_lower or "25.1" in option_lower or "24.0" in option_lower or "23.2" in option_lower:
                badge = f" {Colors.OKGREEN}[compatible]{Colors.ENDC}"
            elif "8.4" in option_lower or "8.5" in option_lower or "8.6" in option_lower or "8.7" in option_lower:
                badge = f" {Colors.OKGREEN}[stable]{Colors.ENDC}"
            
            TUI.write(f"{prefix}{option_text}{badge}{suffix}")
        
        # Scroll indicators
        if self.scroll_offset > 0:
            TUI.move(row + 1, col + w - 5)
            TUI.write(f"{Colors.OKCYAN}^^{Colors.ENDC}")
        if visible_end < len(self.options):
            TUI.move(row + 1 + min(self.max_visible, len(self.options)) - 1, col + w - 5)
            TUI.write(f"{Colors.OKCYAN}vv{Colors.ENDC}")
        
        # Footer
        TUI.move(row + min(self.max_visible, len(self.options)) + 2, col)
        TUI.clear_line()
        TUI.write(f"  Up/Down: Navigate  Enter: Select  Q: Back  ({self.selected + 1}/{len(self.options)})")
    
    def handle_key(self, key: str) -> Optional[int]:
        if key == '\x1b[A' or key == 'k':  # Up
            self.selected = (self.selected - 1) % len(self.options)
            if self.selected < self.scroll_offset:
                self.scroll_offset = self.selected
            elif self.selected >= self.scroll_offset + self.max_visible:
                self.scroll_offset = self.selected - self.max_visible + 1
        elif key == '\x1b[B' or key == 'j':  # Down
            self.selected = (self.selected + 1) % len(self.options)
            if self.selected < self.scroll_offset:
                self.scroll_offset = self.selected
            elif self.selected >= self.scroll_offset + self.max_visible:
                self.scroll_offset = self.selected - self.max_visible + 1
        elif key in ('\r', '\n', ' '):  # Enter/Space
            return self.selected
        elif key.lower() == 'q':
            return -1
        return None


def select_from_list(title: str, options: List[str], default_idx: int = 0) -> Optional[int]:
    if not options:
        return None
    
    menu = Menu(title, options, default_idx)
    TUI.clear()
    TUI.draw_header()
    
    set_terminal_raw()
    try:
        while True:
            TUI.clear()
            TUI.draw_header()
            menu.draw(3, 2)
            TUI.draw_footer("AromaUI CLI | Up/Down: Navigate  Enter: Select  Q: Back")
            
            key = get_keypress()
            if key:
                if key == '\x03':  # Ctrl+C
                    restore_terminal()
                    TUI.clear()
                    sys.exit(0)
                
                result = menu.handle_key(key)
                if result is not None:
                    return result if result >= 0 else None
    finally:
        restore_terminal()
        TUI.clear()


def print_component_status(name: str, status: str, detail: str = "", indent: int = 2):
    """Print a component status with color coding."""
    prefix = " " * indent
    
    if status == "OK":
        color = Colors.OKGREEN
        icon = "OK"
    elif status == "WARN":
        color = Colors.WARNING
        icon = "!!"
    elif status == "FAIL":
        color = Colors.FAIL
        icon = "XX"
    elif status == "INFO":
        color = Colors.OKCYAN
        icon = ">>"
    else:
        color = ""
        icon = "  "
    
    line = f"{prefix}[{color}{icon}{Colors.ENDC}] {name}"
    if detail:
        line += f": {color}{detail}{Colors.ENDC}"
    
    return line


def cmd_doctor(args):
    detector = Detector()
    system_tools = detector.detect_system_tools()
    
    TUI.clear()
    TUI.draw_header()
    
    w = get_terminal_size()[0]
    row = 3
    col = 2
    
    def print_line(text: str, color: str = ""):
        nonlocal row
        TUI.move(row, col)
        TUI.clear_line()
        TUI.write(f"{color}{text}{Colors.ENDC}")
        row += 1
    
    def print_section(title: str):
        nonlocal row
        row += 1
        TUI.move(row, col)
        TUI.clear_line()
        TUI.write(f"{Colors.BOLD}{Colors.HEADER}{title}{Colors.ENDC}")
        row += 1
    
    # ===== SYSTEM =====
    print_section("System")
    
    # OS
    os_name = platform.system()
    os_ver = platform.release()
    print_line(print_component_status("OS", "OK", f"{os_name} {os_ver}"))
    
    # Python
    python_ver = sys.version.split()[0]
    print_line(print_component_status("Python", "OK", python_ver))
    
    # Java
    java_list = find_installed_java()
    if java_list:
        java_vers = [f"JDK {ver}" for _, ver in java_list[:3]]
        print_line(print_component_status("Java", "OK", ", ".join(java_vers)))
    else:
        print_line(print_component_status("Java", "FAIL", "Not found"))
    
    # System Build Tools
    print_section("System Build Tools")
    
    # Git
    if system_tools["git"]["available"]:
        print_line(print_component_status("Git", "OK", system_tools["git"]["version"]))
    else:
        print_line(print_component_status("Git", "FAIL", "Not found"))
    
    # CMake
    if system_tools["cmake"]["available"]:
        print_line(print_component_status("CMake", "OK", system_tools["cmake"]["version"]))
    else:
        print_line(print_component_status("CMake", "FAIL", "Not found"))
    
    # Ninja
    if system_tools["ninja"]["available"]:
        print_line(print_component_status("Ninja", "OK", system_tools["ninja"]["version"]))
    else:
        print_line(print_component_status("Ninja", "WARN", "Not found (optional)"))
    
    # Make
    if system_tools["make"]["available"]:
        print_line(print_component_status("Make", "OK", system_tools["make"]["version"]))
    else:
        print_line(print_component_status("Make", "FAIL", "Not found"))
    
    # GCC
    if system_tools["gcc"]["available"]:
        print_line(print_component_status("GCC", "OK", system_tools["gcc"]["version"]))
    else:
        print_line(print_component_status("GCC", "WARN", "Not found"))
    
    # G++
    if system_tools["gplusplus"]["available"]:
        print_line(print_component_status("G++", "OK", system_tools["gplusplus"]["version"]))
    else:
        print_line(print_component_status("G++", "WARN", "Not found"))
    
    # ===== ANDROID SDK =====
    print_section(f"Android SDK ({detector.sdk_root})")
    
    # SDK Platforms
    sdk_versions = detector.detect_all_sdk()
    if sdk_versions:
        print_line(print_component_status("SDK Platforms", "OK", f"{len(sdk_versions)} installed"))
        for ver in sdk_versions[:5]:
            print_line(print_component_status(f"  android-{ver}", "OK", "", indent=4))
        if len(sdk_versions) > 5:
            print_line(f"    ... and {len(sdk_versions) - 5} more", Colors.OKCYAN)
    else:
        print_line(print_component_status("SDK Platforms", "FAIL", "Not found"))
    
    # Build Tools
    build_tools = detector.detect_all_build_tools()
    if build_tools:
        print_line(print_component_status("Build Tools", "OK", f"{len(build_tools)} versions"))
        for bt in build_tools[:3]:
            print_line(print_component_status(f"  {bt}", "OK", "", indent=4))
        if len(build_tools) > 3:
            print_line(f"    ... and {len(build_tools) - 3} more", Colors.OKCYAN)
    else:
        print_line(print_component_status("Build Tools", "FAIL", "Not found"))
    
    # NDK
    ndk_details = detector.detect_ndk_details()
    if ndk_details["installed"]:
        ndk_versions = ndk_details["versions"]
        compat_versions = ndk_details["compatible"]
        
        status = "OK" if compat_versions else "WARN"
        detail = f"{len(ndk_versions)} version(s)"
        if compat_versions:
            detail += f" ({len(compat_versions)} compatible)"
        
        print_line(print_component_status("NDK", status, detail))
        
        for ver in ndk_versions[:3]:
            is_compat = " [compatible]" if ver in compat_versions else ""
            print_line(print_component_status(f"  {ver}", "OK", f"Installed{is_compat}", indent=4))
        
        if ndk_details["ndk_build_available"]:
            print_line(print_component_status("  ndk-build", "OK", "Available", indent=4))
        else:
            print_line(print_component_status("  ndk-build", "WARN", "Not found", indent=4))
    else:
        print_line(print_component_status("NDK", "FAIL", "Not found"))
    
    # CMake (SDK)
    cmake_versions = detector.detect_all_cmake()
    if cmake_versions:
        print_line(print_component_status("CMake (SDK)", "OK", f"{len(cmake_versions)} version(s)"))
        for ver in cmake_versions[:3]:
            print_line(print_component_status(f"  {ver}", "OK", "", indent=4))
    else:
        print_line(print_component_status("CMake (SDK)", "INFO", "Not installed (using system cmake)"))
    
    # Platform Tools
    pt_details = detector.detect_platform_tools_details()
    if pt_details["installed"]:
        adb_ver = pt_details.get("adb_version", "unknown")
        print_line(print_component_status("Platform Tools", "OK", f"ADB {adb_ver}"))
        
        if pt_details["fastboot_available"]:
            print_line(print_component_status("  Fastboot", "OK", "Available", indent=4))
        else:
            print_line(print_component_status("  Fastboot", "WARN", "Not found", indent=4))
        
        # Show key tools
        key_tools = ["adb", "fastboot", "sqlite3", "etc1tool"]
        for tool in key_tools:
            if tool in pt_details["tools"] or f"{tool}.exe" in pt_details["tools"]:
                print_line(print_component_status(f"  {tool}", "OK", "", indent=4))
    else:
        print_line(print_component_status("Platform Tools", "FAIL", "Not found"))
    
    # Emulator
    emu_details = detector.detect_emulator_details()
    if emu_details["installed"]:
        emu_ver = emu_details.get("version", "unknown")
        print_line(print_component_status("Emulator", "OK", f"Version {emu_ver}"))
        
        # System Images
        sys_images = emu_details["system_images"]
        if sys_images:
            print_line(print_component_status("  System Images", "OK", f"{len(sys_images)} available"))
            for img in sys_images[:3]:
                print_line(print_component_status(f"    {img}", "OK", "", indent=4))
        else:
            print_line(print_component_status("  System Images", "WARN", "None installed"))
        
        # AVDs
        avds = emu_details["avds"]
        if avds:
            print_line(print_component_status("  AVDs", "OK", f"{len(avds)} configured"))
            for avd in avds[:3]:
                print_line(print_component_status(f"    {avd}", "OK", "", indent=4))
        else:
            print_line(print_component_status("  AVDs", "INFO", "No virtual devices configured"))
    else:
        print_line(print_component_status("Emulator", "INFO", "Not installed"))
    
    # Licenses
    if detector.detect_licenses():
        print_line(print_component_status("Licenses", "OK", "Accepted"))
    else:
        print_line(print_component_status("Licenses", "FAIL", "Not accepted"))
    
    # ===== AROMA SDK =====
    print_section(f"Aroma SDK ({detector.install_path})")
    
    # Core installation
    if os.path.isdir(detector.install_path):
        # Check for key Aroma files
        has_cmake = os.path.exists(os.path.join(detector.install_path, "CMakeLists.txt"))
        has_include = os.path.exists(os.path.join(detector.install_path, "include", "aroma.h"))
        has_src = os.path.isdir(os.path.join(detector.install_path, "src"))
        has_git = os.path.isdir(os.path.join(detector.install_path, ".git"))
        
        if has_cmake and has_include and has_src:
            print_line(print_component_status("Core Library", "OK", "Installed"))
        else:
            print_line(print_component_status("Core Library", "WARN", "Partially installed"))
        
        print_line(print_component_status("  CMakeLists", "OK" if has_cmake else "FAIL", "", indent=4))
        print_line(print_component_status("  include/aroma.h", "OK" if has_include else "FAIL", "", indent=4))
        print_line(print_component_status("  src/", "OK" if has_src else "FAIL", "", indent=4))
        print_line(print_component_status("  .git/", "OK" if has_git else "INFO", "Git managed" if has_git else "Not a git repo", indent=4))
        
        # Check bin directory
        bin_dir = os.path.join(detector.install_path, "bin")
        if os.path.isdir(bin_dir):
            bin_files = os.listdir(bin_dir)
            print_line(print_component_status("  bin/", "OK", f"{len(bin_files)} files", indent=4))
        else:
            print_line(print_component_status("  bin/", "WARN", "Not found", indent=4))
        
        # Check docs
        docs_dir = os.path.join(detector.install_path, "docs")
        if os.path.isdir(docs_dir):
            print_line(print_component_status("  docs/", "OK", "Available", indent=4))
        else:
            print_line(print_component_status("  docs/", "INFO", "Not installed", indent=4))
    else:
        print_line(print_component_status("Core Library", "FAIL", "Not installed"))
    
    # Gradle
    gradle_details = detector.detect_gradle_details()
    if gradle_details["installed"]:
        gradle_versions = gradle_details["versions"]
        preferred = [v for v in gradle_versions if v in PREFERRED_GRADLE_VERSIONS]
        
        status = "OK" if preferred else "WARN"
        detail = f"{len(gradle_versions)} version(s)"
        if preferred:
            detail += f" (preferred: {', '.join(preferred[:2])})"
        
        print_line(print_component_status("Gradle", status, detail))
        
        for ver in gradle_versions[:3]:
            is_pref = " [preferred]" if ver in PREFERRED_GRADLE_VERSIONS else ""
            print_line(print_component_status(f"  {ver}", "OK", f"Installed{is_pref}", indent=4))
    else:
        print_line(print_component_status("Gradle", "FAIL", "Not found"))
    
    # Gradle wrapper
    if gradle_details["wrapper_available"]:
        wrapper_path = gradle_details.get("wrapper_path", "")
        if wrapper_path:
            print_line(print_component_status("Gradle Wrapper", "OK", wrapper_path))
        else:
            print_line(print_component_status("Gradle Wrapper", "OK", "Available in project"))
    elif gradle_details["wrapper_props_available"]:
        print_line(print_component_status("Gradle Wrapper", "WARN", "Properties found but no wrapper script (run 'gradle wrapper')"))
    else:
        print_line(print_component_status("Gradle Wrapper", "INFO", "Not found (run 'gradle wrapper' in project)"))
    
    # System Gradle
    if gradle_details["system_gradle"]:
        print_line(print_component_status("System Gradle", "OK", gradle_details["system_gradle"]))
    
    # ===== SUMMARY =====
    row += 2
    print_section("Summary")
    
    # Calculate status
    issues = []
    warnings = []
    
    if not java_list:
        issues.append("Java JDK not found")
    if not system_tools["git"]["available"]:
        issues.append("Git not found")
    if not system_tools["cmake"]["available"]:
        issues.append("CMake not found")
    if not sdk_versions:
        issues.append("No Android SDK platforms")
    if not build_tools:
        issues.append("No Android Build Tools")
    if not ndk_details["installed"]:
        warnings.append("NDK not installed")
    if not pt_details["installed"]:
        issues.append("Platform Tools not installed")
    if not detector.detect_licenses():
        warnings.append("SDK licenses not accepted")
    if not os.path.isdir(detector.install_path):
        issues.append("Aroma core not installed")
    if not gradle_details["installed"] and not gradle_details["wrapper_available"]:
        warnings.append("Gradle not found (no installed version or wrapper)")
    
    if not issues and not warnings:
        print_line(print_component_status("Status", "OK", "All components ready!"))
    else:
        if issues:
            print_line(print_component_status(f"Issues ({len(issues)})", "FAIL", ""))
            for issue in issues:
                print_line(f"      - {issue}", Colors.FAIL)
        
        if warnings:
            print_line(print_component_status(f"Warnings ({len(warnings)})", "WARN", ""))
            for warning in warnings:
                print_line(f"      - {warning}", Colors.WARNING)
    
    row += 2
    TUI.draw_footer("Press any key to exit")
    
    sys.stdout.flush()
    set_terminal_raw()
    try:
        while True:
            if get_keypress():
                break
            time.sleep(0.1)
    finally:
        restore_terminal()
    TUI.clear()


def cmd_build(args):
    detector = Detector()
    
    if args.platform == "android":
        sdk_versions = detector.detect_all_sdk()
        build_tools = detector.detect_all_build_tools()
        ndk_versions = detector.detect_all_ndk()
        cmake_versions = detector.detect_all_cmake()
        
        if not sdk_versions:
            print(f"{Colors.FAIL}No Android SDK platforms found. Install via Android SDK Manager.{Colors.ENDC}")
            sys.exit(1)
        
        if args.sdk_version:
            sdk_ver = args.sdk_version
        else:
            idx = select_from_list("Select SDK Platform:", sdk_versions)
            if idx is None:
                sys.exit(0)
            sdk_ver = sdk_versions[idx]
        
        if args.build_tools:
            bt_ver = args.build_tools
        elif build_tools:
            idx = select_from_list("Select Build Tools:", build_tools)
            if idx is None:
                sys.exit(0)
            bt_ver = build_tools[idx]
        else:
            bt_ver = f"{sdk_ver}.0.0"
        
        if args.ndk_version:
            ndk_ver = args.ndk_version
        elif ndk_versions:
            idx = select_from_list("Select NDK:", ndk_versions)
            if idx is None:
                sys.exit(0)
            ndk_ver = ndk_versions[idx]
        else:
            ndk_ver = None
        
        if args.cmake_version:
            cmake_ver = args.cmake_version
        elif cmake_versions:
            idx = select_from_list("Select CMake:", cmake_versions)
            if idx is None:
                sys.exit(0)
            cmake_ver = cmake_versions[idx]
        else:
            cmake_ver = "3.22.1"
        
        print(f"\n{Colors.BOLD}Building with:{Colors.ENDC}")
        print(f"  SDK:        {sdk_ver}")
        print(f"  Build Tools: {bt_ver}")
        if ndk_ver:
            print(f"  NDK:        {ndk_ver}")
        print(f"  CMake:      {cmake_ver}")
        print()
    
    elif args.platform == "linux":
        sdk_ver = ndk_ver = bt_ver = cmake_ver = None
    
    _do_build(args.platform, detector.sdk_root, sdk_ver, bt_ver, ndk_ver, cmake_ver, args.release, args.aab)


def _do_build(platform: str, sdk_root: str, sdk_ver: str, bt_ver: str, ndk_ver: str, cmake_ver: str, release: bool, aab: bool):
    cwd = os.getcwd()
    
    if platform == "linux":
        build_dir = os.path.join(cwd, "build")
        os.makedirs(build_dir, exist_ok=True)
        
        print(f"{Colors.OKBLUE}==>{Colors.ENDC} Configuring...")
        result = run_command(["cmake", ".."], cwd=build_dir)
        if result is None or result.returncode != 0:
            print(f"{Colors.FAIL}ERROR: CMake configuration failed{Colors.ENDC}")
            sys.exit(1)
        
        print(f"{Colors.OKBLUE}==>{Colors.ENDC} Building...")
        result = run_command(["make", f"-j{os.cpu_count() or 4}"], cwd=build_dir)
        if result is None or result.returncode != 0:
            print(f"{Colors.FAIL}ERROR: Build failed{Colors.ENDC}")
            sys.exit(1)
        
        print(f"{Colors.OKGREEN}OK Build successful{Colors.ENDC}")
    
    elif platform == "android":
        android_dir = os.path.join(cwd, "android")
        if not os.path.exists(android_dir):
            print(f"{Colors.FAIL}ERROR: Not an Aroma project (no android/ directory){Colors.ENDC}")
            sys.exit(1)
        
        gradlew = os.path.join(android_dir, "gradlew")
        if not os.path.exists(gradlew):
            # Try to find gradle wrapper in install path
            gradlew_alt = os.path.join(AROMA_INSTALL_DIR, "gradlew")
            if os.path.exists(gradlew_alt):
                shutil.copy(gradlew_alt, gradlew)
                os.chmod(gradlew, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
            else:
                print(f"{Colors.FAIL}ERROR: Gradle wrapper not found{Colors.ENDC}")
                sys.exit(1)
        
        os.chmod(gradlew, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
        
        target = "assembleDebug"
        if release and aab:
            target = "bundleRelease"
        elif release:
            target = "assembleRelease"
        
        env = os.environ.copy()
        env["ANDROID_HOME"] = sdk_root
        if ndk_ver:
            env["ANDROID_NDK_HOME"] = os.path.join(sdk_root, "ndk", ndk_ver)
        
        print(f"{Colors.OKBLUE}==>{Colors.ENDC} Building Android ({target})...")
        result = run_command([gradlew, target], cwd=android_dir, timeout=600, env=env)
        if result is None or result.returncode != 0:
            print(f"{Colors.FAIL}ERROR: Android build failed{Colors.ENDC}")
            sys.exit(1)
        
        print(f"{Colors.OKGREEN}OK Build successful{Colors.ENDC}")


def cmd_run(args):
    detector = Detector()
    cwd = os.getcwd()
    
    if args.platform == "linux":
        build_dir = os.path.join(cwd, "build")
        exe = os.path.join(build_dir, os.path.basename(cwd))
        if not os.path.exists(exe):
            if os.path.isdir(build_dir):
                for f in os.listdir(build_dir):
                    fp = os.path.join(build_dir, f)
                    if os.access(fp, os.X_OK) and not f.endswith('.so'):
                        exe = fp
                        break
        
        if os.path.exists(exe):
            print(f"{Colors.OKBLUE}==>{Colors.ENDC} Launching {exe}")
            subprocess.run([exe])
        else:
            print(f"{Colors.FAIL}ERROR: Executable not found. Run 'aroma build linux' first.{Colors.ENDC}")
            sys.exit(1)
    
    elif args.platform == "android":
        android_dir = os.path.join(cwd, "android")
        if not os.path.exists(android_dir):
            print(f"{Colors.FAIL}ERROR: Not an Aroma project{Colors.ENDC}")
            sys.exit(1)
        
        apk_path = os.path.join(android_dir, "app", "build", "outputs", "apk", "debug", "app-debug.apk")
        if not os.path.exists(apk_path):
            print(f"{Colors.FAIL}ERROR: APK not found. Run 'aroma build android' first.{Colors.ENDC}")
            sys.exit(1)
        
        adb = shutil.which("adb") or os.path.join(detector.sdk_root, "platform-tools", "adb")
        
        manifest = os.path.join(android_dir, "app", "src", "main", "AndroidManifest.xml")
        pkg = None
        if os.path.exists(manifest):
            try:
                tree = ET.parse(manifest)
                pkg = tree.getroot().get('package')
            except:
                pass
        
        if not pkg:
            print(f"{Colors.FAIL}ERROR: Could not determine package name{Colors.ENDC}")
            sys.exit(1)
        
        print(f"{Colors.OKBLUE}==>{Colors.ENDC} Installing...")
        result = run_command([adb, "install", "-r", apk_path], capture_output=True)
        if result is None or result.returncode != 0:
            print(f"{Colors.FAIL}ERROR: Installation failed{Colors.ENDC}")
            sys.exit(1)
        
        print(f"{Colors.OKGREEN}OK Installed{Colors.ENDC}")
        print(f"{Colors.OKBLUE}==>{Colors.ENDC} Launching...")
        run_command([adb, "shell", "am", "start", "-n", f"{pkg}/android.app.NativeActivity"])
        print(f"{Colors.OKGREEN}OK Launched{Colors.ENDC}")


def main():
    parser = argparse.ArgumentParser(
        prog="aroma",
        description="AromaUI CLI Tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"Examples:\n  aroma doctor\n  aroma build android\n  aroma run linux\n\nPaths:\n  Install: {AROMA_INSTALL_DIR}\n  SDK:     {AROMA_SDK_DIR}"
    )
    
    parser.add_argument("--config", help="Config file path")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--no-color", action="store_true")
    
    subparsers = parser.add_subparsers(dest="command")
    subparsers.add_parser("doctor", help="Check installed components")
    
    p = subparsers.add_parser("build", help="Build project")
    p.add_argument("platform", choices=["linux", "android", "web"], nargs="?", default="linux")
    p.add_argument("--release", action="store_true")
    p.add_argument("--aab", action="store_true")
    p.add_argument("--sdk-version", help="Android SDK version")
    p.add_argument("--build-tools", help="Build tools version")
    p.add_argument("--ndk-version", help="NDK version")
    p.add_argument("--cmake-version", help="CMake version")
    
    p = subparsers.add_parser("run", help="Run project")
    p.add_argument("platform", choices=["linux", "android", "web"], nargs="?", default="linux")
    
    if len(sys.argv) == 1:
        parser.print_help()
        sys.exit(1)
    
    args = parser.parse_args()
    
    Logger.setup(verbose=args.verbose)
    if args.no_color:
        Colors.disable()
    
    if args.command == "doctor":
        cmd_doctor(args)
    elif args.command == "build":
        cmd_build(args)
    elif args.command == "run":
        cmd_run(args)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()