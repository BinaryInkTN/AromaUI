"""Environment health check (aroma doctor)."""

import os
import platform
import shutil
import sys

from aroma_constants import PREFERRED_GRADLE_VERSIONS
from aroma_detector import Detector
from aroma_output import Colors, Logger
from aroma_util import find_installed_java

def cmd_doctor(args):
    detector = Detector()
    system_tools = detector.detect_system_tools()
    
    def print_status(name: str, status: str, detail: str = "", indent: int = 2):
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
        print(line)
    
    def print_section(title: str):
        print(f"\n{Colors.BOLD}{Colors.HEADER}{title}{Colors.ENDC}")
    
    print(f"{Colors.BOLD}{' AromaUI Doctor '.center(60, '=')}{Colors.ENDC}")
    
    print_section("System")
    os_name = platform.system()
    os_ver = platform.release()
    print_status("OS", "OK", f"{os_name} {os_ver}")
    
    python_ver = sys.version.split()[0]
    print_status("Python", "OK", python_ver)
    
    java_list = find_installed_java()
    if java_list:
        java_vers = [f"JDK {ver}" for _, ver in java_list[:3]]
        print_status("Java", "OK", ", ".join(java_vers))
    else:
        print_status("Java", "FAIL", "Not found")
    
    print_section("System Build Tools")
    if system_tools["git"]["available"]:
        print_status("Git", "OK", system_tools["git"]["version"])
    else:
        print_status("Git", "FAIL", "Not found")
    
    if system_tools["cmake"]["available"]:
        print_status("CMake", "OK", system_tools["cmake"]["version"])
    else:
        print_status("CMake", "FAIL", "Not found")
    
    if system_tools["ninja"]["available"]:
        print_status("Ninja", "OK", system_tools["ninja"]["version"])
    else:
        print_status("Ninja", "WARN", "Not found (optional)")
    
    if system_tools["make"]["available"]:
        print_status("Make", "OK", system_tools["make"]["version"])
    else:
        print_status("Make", "FAIL", "Not found")
    
    if system_tools["gcc"]["available"]:
        print_status("GCC", "OK", system_tools["gcc"]["version"])
    else:
        print_status("GCC", "WARN", "Not found")
    
    if system_tools["gplusplus"]["available"]:
        print_status("G++", "OK", system_tools["gplusplus"]["version"])
    else:
        print_status("G++", "WARN", "Not found")
    
    print_section(f"Android SDK ({detector.sdk_root})")
    
    sdk_versions = detector.detect_all_sdk()
    if sdk_versions:
        print_status("SDK Platforms", "OK", f"{len(sdk_versions)} installed")
        for ver in sdk_versions[:5]:
            print_status(f"android-{ver}", "OK", indent=4)
        if len(sdk_versions) > 5:
            print(f"    ... and {len(sdk_versions) - 5} more")
    else:
        print_status("SDK Platforms", "FAIL", "Not found")
    
    build_tools = detector.detect_all_build_tools()
    if build_tools:
        print_status("Build Tools", "OK", f"{len(build_tools)} versions")
        for bt in build_tools[:3]:
            print_status(f"{bt}", "OK", indent=4)
        if len(build_tools) > 3:
            print(f"    ... and {len(build_tools) - 3} more")
    else:
        print_status("Build Tools", "FAIL", "Not found")
    
    ndk_details = detector.detect_ndk_details()
    if ndk_details["installed"]:
        ndk_versions = ndk_details["versions"]
        compat_versions = ndk_details["compatible"]
        status = "OK" if compat_versions else "WARN"
        detail = f"{len(ndk_versions)} version(s)"
        if compat_versions:
            detail += f" ({len(compat_versions)} compatible)"
        print_status("NDK", status, detail)
        for ver in ndk_versions[:3]:
            is_compat = " [compatible]" if ver in compat_versions else ""
            print_status(f"{ver}", "OK", f"Installed{is_compat}", indent=4)
        if ndk_details["ndk_build_available"]:
            print_status("ndk-build", "OK", "Available", indent=4)
        else:
            print_status("ndk-build", "WARN", "Not found", indent=4)
    else:
        print_status("NDK", "FAIL", "Not found")
    
    cmake_versions = detector.detect_all_cmake()
    if cmake_versions:
        print_status("CMake (SDK)", "OK", f"{len(cmake_versions)} version(s)")
        for ver in cmake_versions[:3]:
            print_status(f"{ver}", "OK", indent=4)
    else:
        print_status("CMake (SDK)", "INFO", "Not installed (using system cmake)")
    
    pt_details = detector.detect_platform_tools_details()
    if pt_details["installed"]:
        adb_ver = pt_details.get("adb_version", "unknown")
        print_status("Platform Tools", "OK", f"ADB {adb_ver}")
        if pt_details["fastboot_available"]:
            print_status("Fastboot", "OK", "Available", indent=4)
        else:
            print_status("Fastboot", "WARN", "Not found", indent=4)
    else:
        print_status("Platform Tools", "FAIL", "Not found")
    
    emu_details = detector.detect_emulator_details()
    if emu_details["installed"]:
        emu_ver = emu_details.get("version", "unknown")
        print_status("Emulator", "OK", f"Version {emu_ver}")
        sys_images = emu_details["system_images"]
        if sys_images:
            print_status("System Images", "OK", f"{len(sys_images)} available")
        else:
            print_status("System Images", "WARN", "None installed")
        avds = emu_details["avds"]
        if avds:
            print_status("AVDs", "OK", f"{len(avds)} configured")
        else:
            print_status("AVDs", "INFO", "No virtual devices configured")
    else:
        print_status("Emulator", "INFO", "Not installed")
    
    if detector.detect_licenses():
        print_status("Licenses", "OK", "Accepted")
    else:
        print_status("Licenses", "FAIL", "Not accepted")
    
    print_section(f"Aroma SDK ({detector.install_path})")
    
    if os.path.isdir(detector.install_path):
        has_cmake = os.path.exists(os.path.join(detector.install_path, "CMakeLists.txt"))
        has_include = os.path.exists(os.path.join(detector.install_path, "include", "aroma.h"))
        has_src = os.path.isdir(os.path.join(detector.install_path, "src"))
        if has_cmake and has_include and has_src:
            print_status("Core Library", "OK", "Installed")
        else:
            print_status("Core Library", "WARN", "Partially installed")
    else:
        print_status("Core Library", "FAIL", "Not installed")
    
    gradle_details = detector.detect_gradle_details()
    if gradle_details["installed"]:
        gradle_versions = gradle_details["versions"]
        preferred = [v for v in gradle_versions if v in PREFERRED_GRADLE_VERSIONS]
        status = "OK" if preferred else "WARN"
        detail = f"{len(gradle_versions)} version(s)"
        if preferred:
            detail += f" (preferred: {', '.join(preferred[:2])})"
        print_status("Gradle", status, detail)
    else:
        print_status("Gradle", "FAIL", "Not found")
    
    if gradle_details["wrapper_available"]:
        wrapper_path = gradle_details.get("wrapper_path", "")
        print_status("Gradle Wrapper", "OK", wrapper_path or "Available")
    elif gradle_details["wrapper_props_available"]:
        print_status("Gradle Wrapper", "WARN", "Properties found but no wrapper script")
    else:
        print_status("Gradle Wrapper", "INFO", "Not found")

    print_section("Linux Bundling Tools (aroma bundle linux)")
    bundle_tools = {
        "dpkg-deb (deb)": shutil.which("dpkg-deb"),
        "rpmbuild (rpm)": shutil.which("rpmbuild"),
        "appimagetool (AppImage)": (shutil.which("appimagetool")
                                    or os.environ.get("AROMA_APPIMAGETOOL")),
        "flatpak-builder (Flatpak)": shutil.which("flatpak-builder"),
        "snapcraft (Snap)": shutil.which("snapcraft"),
        "patchelf (optional rpath)": shutil.which("patchelf"),
    }
    for label, path in bundle_tools.items():
        if path and (os.path.isfile(path) if os.sep in str(path) else True):
            print_status(label, "OK", str(path) if str(path) != str(True) else "Available")
        else:
            optional = "patchelf" in label or "appimagetool" in label
            print_status(label, "INFO" if optional else "WARN",
                         "Not found (format still scaffolds metadata)" if optional
                         else "Not found")

    print_section("Summary")
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
        warnings.append("Gradle not found")
    
    if not issues and not warnings:
        print_status("Status", "OK", "All components ready!")
    else:
        if issues:
            print_status(f"Issues ({len(issues)})", "FAIL", "")
            for issue in issues:
                print(f"      - {issue}")
        if warnings:
            print_status(f"Warnings ({len(warnings)})", "WARN", "")
            for warning in warnings:
                print(f"      - {warning}")
    
    print(f"\n{Colors.BOLD}{'=' * 60}{Colors.ENDC}")
