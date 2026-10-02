"""Inventory of Android SDK components, Java toolchains, and build tools."""

import os
import platform
import re
import shutil
from typing import Any, Dict, List, Optional

from aroma_constants import (
    AROMA_COMPATIBLE_NDK_VERSIONS,
    AROMA_INSTALL_DIR,
    AROMA_SDK_DIR,
    PREFERRED_GRADLE_VERSIONS,
    SDK_SEARCH_PATHS,
)
from aroma_util import run_command

class Detector:
    def __init__(self):
        self.sdk_root = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
        if not self.sdk_root:
            for p in SDK_SEARCH_PATHS:
                path = os.path.expanduser(p)
                if os.path.isdir(path):
                    self.sdk_root = path
                    break
        if not self.sdk_root:
            self.sdk_root = AROMA_SDK_DIR
        
        self.install_path = AROMA_INSTALL_DIR
    
    def get_adb_command(self) -> Optional[str]:
        cmd = shutil.which("adb")
        if cmd:
            return cmd
        adb_path = os.path.join(self.sdk_root, "platform-tools", "adb")
        if platform.system() == "Windows":
            adb_path += ".exe"
        if os.path.exists(adb_path):
            return adb_path
        return None
    
    def get_emulator_command(self) -> Optional[str]:
        emu_path = os.path.join(self.sdk_root, "emulator", "emulator")
        if platform.system() == "Windows":
            emu_path += ".exe"
        if os.path.exists(emu_path):
            return emu_path
        return None
    
    def get_avdmanager_command(self) -> Optional[str]:
        for subdir in ["latest", "tools"]:
            avdmanager = os.path.join(self.sdk_root, "cmdline-tools", subdir, "bin", "avdmanager")
            if platform.system() == "Windows":
                avdmanager += ".bat"
            if os.path.exists(avdmanager):
                return avdmanager
        return None
    
    def detect_all_gradle(self) -> List[str]:
        installed = []
        
        aroma_gradle = os.path.join(self.install_path, "gradle")
        if os.path.isdir(aroma_gradle):
            for entry in sorted(os.listdir(aroma_gradle)):
                if entry.startswith("gradle-") and os.path.isdir(os.path.join(aroma_gradle, entry)):
                    version = entry.replace("gradle-", "")
                    if os.path.isfile(os.path.join(aroma_gradle, entry, "bin", "gradle")):
                        if version not in installed:
                            installed.append(version)
        
        legacy_gradle = os.path.expanduser("~/aroma/gradle")
        if os.path.isdir(legacy_gradle) and legacy_gradle != aroma_gradle:
            for entry in sorted(os.listdir(legacy_gradle)):
                if entry.startswith("gradle-") and os.path.isdir(os.path.join(legacy_gradle, entry)):
                    version = entry.replace("gradle-", "")
                    if os.path.isfile(os.path.join(legacy_gradle, entry, "bin", "gradle")):
                        if version not in installed:
                            installed.append(version)
        
        wrapper_dists = os.path.expanduser("~/.gradle/wrapper/dists")
        if os.path.isdir(wrapper_dists):
            for entry in sorted(os.listdir(wrapper_dists)):
                if entry.startswith("gradle-"):
                    version = entry.replace("gradle-", "").split("-")[0]
                    if version not in installed:
                        installed.append(version)
        
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
        
        wrapper_locations = [
            "gradlew",
            "gradlew.bat",
            os.path.join(self.install_path, "gradlew"),
            os.path.join(self.install_path, "bin", "gradlew"),
            "android/gradlew",
            "android/gradlew.bat",
        ]
        
        for loc in wrapper_locations:
            if os.path.exists(loc):
                details["wrapper_available"] = True
                details["wrapper_path"] = os.path.abspath(loc)
                break
        
        wrapper_props_locations = [
            "gradle/wrapper/gradle-wrapper.properties",
            "android/gradle/wrapper/gradle-wrapper.properties",
        ]
        
        for loc in wrapper_props_locations:
            if os.path.exists(loc):
                details["wrapper_props_available"] = True
                try:
                    with open(loc, 'r') as f:
                        for line in f:
                            if "distributionUrl" in line:
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
        
        details["versions"].sort(reverse=True)
        
        return details
    
    def detect_all_sdk(self) -> List[str]:
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
        bt_dir = os.path.join(self.sdk_root, "build-tools")
        if not os.path.isdir(bt_dir):
            return []
        
        versions = []
        for entry in os.listdir(bt_dir):
            bt_path = os.path.join(bt_dir, entry)
            if os.path.isdir(bt_path):
                has_aapt = os.path.exists(os.path.join(bt_path, "aapt")) or \
                          os.path.exists(os.path.join(bt_path, "aapt2"))
                has_zipalign = os.path.exists(os.path.join(bt_path, "zipalign"))
                if has_aapt or has_zipalign:
                    versions.append(entry)
        versions.sort(reverse=True)
        return versions
    
    def detect_all_ndk(self) -> List[str]:
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
        
        compat = [v for v in installed if v in AROMA_COMPATIBLE_NDK_VERSIONS]
        other = [v for v in installed if v not in AROMA_COMPATIBLE_NDK_VERSIONS]
        compat.sort(reverse=True)
        other.sort(reverse=True)
        return compat + other
    
    def detect_ndk_details(self) -> Dict[str, Any]:
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
            
            if versions:
                ndk_dir = os.path.join(self.sdk_root, "ndk", versions[0])
                details["ndk_build_available"] = os.path.exists(os.path.join(ndk_dir, "ndk-build"))
                details["ndk_stack_available"] = os.path.exists(os.path.join(ndk_dir, "ndk-stack"))
        
        return details
    
    def detect_all_cmake(self) -> List[str]:
        cmake_dir = os.path.join(self.sdk_root, "cmake")
        if not os.path.isdir(cmake_dir):
            return []
        
        versions = []
        for entry in os.listdir(cmake_dir):
            cmake_path = os.path.join(cmake_dir, entry)
            if os.path.isdir(cmake_path):
                if os.path.exists(os.path.join(cmake_path, "bin", "cmake")):
                    versions.append(entry)
        versions.sort(reverse=True)
        return versions
    
    def detect_platform_tools(self) -> bool:
        return self.get_adb_command() is not None
    
    def detect_platform_tools_details(self) -> Dict[str, Any]:
        details = {
            "installed": False,
            "adb_version": None,
            "fastboot_available": False,
            "tools": [],
        }
        
        adb_path = self.get_adb_command()
        if adb_path:
            details["installed"] = True
            result = run_command([adb_path, "version"], capture_output=True)
            if result and result.returncode == 0:
                version_match = re.search(r'version (\S+)', result.stdout)
                if version_match:
                    details["adb_version"] = version_match.group(1)
        
        pt_dir = os.path.join(self.sdk_root, "platform-tools")
        if os.path.isdir(pt_dir):
            fastboot_path = os.path.join(pt_dir, "fastboot")
            if platform.system() == "Windows":
                fastboot_path += ".exe"
            details["fastboot_available"] = os.path.exists(fastboot_path)
            
            for item in os.listdir(pt_dir):
                if os.path.isfile(os.path.join(pt_dir, item)):
                    details["tools"].append(item)
        
        return details
    
    def detect_emulator(self) -> bool:
        return self.get_emulator_command() is not None
    
    def detect_emulator_details(self) -> Dict[str, Any]:
        details = {
            "installed": False,
            "version": None,
            "system_images": [],
            "avds": [],
        }
        
        emu_bin = self.get_emulator_command()
        if emu_bin:
            details["installed"] = True
            result = run_command([emu_bin, "-version"], capture_output=True)
            if result and result.returncode == 0:
                version_match = re.search(r'version (\S+)', result.stdout)
                if version_match:
                    details["version"] = version_match.group(1)
        
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
        
        avd_dir = os.path.expanduser("~/.android/avd")
        if os.path.isdir(avd_dir):
            for avd in os.listdir(avd_dir):
                if avd.endswith(".avd"):
                    details["avds"].append(avd.replace(".avd", ""))
        
        return details
    
    def detect_licenses(self) -> bool:
        lic_dir = os.path.join(self.sdk_root, "licenses")
        return os.path.isdir(lic_dir) and len(os.listdir(lic_dir)) > 0
    
    def detect_system_tools(self) -> Dict[str, Any]:
        tools = {
            "cmake": {"available": False, "version": None, "path": None},
            "ninja": {"available": False, "version": None, "path": None},
            "gcc": {"available": False, "version": None, "path": None},
            "gplusplus": {"available": False, "version": None, "path": None},
            "make": {"available": False, "version": None, "path": None},
            "git": {"available": False, "version": None, "path": None},
            "adb": {"available": False, "version": None, "path": None},
        }
        
        cmake_path = shutil.which("cmake")
        if cmake_path:
            tools["cmake"]["available"] = True
            tools["cmake"]["path"] = cmake_path
            result = run_command([cmake_path, "--version"], capture_output=True)
            if result and result.returncode == 0:
                version_match = re.search(r'version (\S+)', result.stdout)
                if version_match:
                    tools["cmake"]["version"] = version_match.group(1)
        
        ninja_path = shutil.which("ninja")
        if ninja_path:
            tools["ninja"]["available"] = True
            tools["ninja"]["path"] = ninja_path
            result = run_command([ninja_path, "--version"], capture_output=True)
            if result and result.returncode == 0:
                tools["ninja"]["version"] = result.stdout.strip()
        
        gcc_path = shutil.which("gcc")
        if gcc_path:
            tools["gcc"]["available"] = True
            tools["gcc"]["path"] = gcc_path
            result = run_command([gcc_path, "--version"], capture_output=True)
            if result and result.returncode == 0:
                version_match = re.search(r'(\d+\.\d+\.\d+)', result.stdout)
                if version_match:
                    tools["gcc"]["version"] = version_match.group(1)
        
        gpp_path = shutil.which("g++")
        if gpp_path:
            tools["gplusplus"]["available"] = True
            tools["gplusplus"]["path"] = gpp_path
            result = run_command([gpp_path, "--version"], capture_output=True)
            if result and result.returncode == 0:
                version_match = re.search(r'(\d+\.\d+\.\d+)', result.stdout)
                if version_match:
                    tools["gplusplus"]["version"] = version_match.group(1)
        
        make_path = shutil.which("make")
        if make_path:
            tools["make"]["available"] = True
            tools["make"]["path"] = make_path
            result = run_command([make_path, "--version"], capture_output=True)
            if result and result.returncode == 0:
                version_match = re.search(r'GNU Make (\S+)', result.stdout)
                if version_match:
                    tools["make"]["version"] = version_match.group(1)
        
        git_path = shutil.which("git")
        if git_path:
            tools["git"]["available"] = True
            tools["git"]["path"] = git_path
            result = run_command([git_path, "--version"], capture_output=True)
            if result and result.returncode == 0:
                tools["git"]["version"] = result.stdout.strip().replace("git version ", "")
        
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
