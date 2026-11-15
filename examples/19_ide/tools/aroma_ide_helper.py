#!/usr/bin/env python3
"""Small bridge between the C IDE and the AromaUI command-line tools.

The IDE deliberately keeps Android SDK/NDK/Gradle/adb orchestration in
tools/cli.  This helper only exposes the pieces the IDE needs without
duplicating that logic in C:

  adb-devices <project>       print the adb path, then one serial per line
  set-device <project> <id>   persist run.device_serial in aroma.json
  setup-android <project>     scaffold/refresh android/ from the CLI templates
  run-android <project> [id]  install and launch the debug APK on a device
"""

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve()
REPO = HERE.parents[3]  # .../examples/19_ide/tools/aroma_ide_helper.py
CLI_DIR = REPO / "tools" / "cli"
sys.path.insert(0, str(CLI_DIR))

from aroma_output import Colors  # noqa: E402

Colors.disable()


def load_config(root: Path) -> dict:
    path = root / "aroma.json"
    try:
        with open(path, "r") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def save_config(root: Path, config: dict) -> None:
    path = root / "aroma.json"
    with open(path, "w") as f:
        json.dump(config, f, indent=2)
        f.write("\n")


def project_info(root: Path, config: dict):
    project = config.get("project") or {}
    android = config.get("android") or {}
    name = project.get("name") or config.get("title") or root.name
    package = project.get("package") or "com.example." + re.sub(
        r"[^A-Za-z0-9]", "", str(name)
    ).lower()
    compile_sdk = int(
        project.get("compile_sdk")
        or android.get("sdk_version")
        or config.get("compile_sdk")
        or 34
    )
    min_sdk = int(project.get("min_sdk") or 24)
    target_sdk = int(project.get("target_sdk") or compile_sdk)
    return str(name), package, min_sdk, target_sdk, compile_sdk


def ensure_build_config(root: Path) -> int:
    from aroma_detector import Detector

    detector = Detector()
    sdk_versions = detector.detect_all_sdk()
    if not sdk_versions:
        print("No Android SDK platforms found. Install one with the Android SDK Manager.")
        return 1

    build_tools = detector.detect_all_build_tools()
    ndk_versions = detector.detect_all_ndk()
    cmake_versions = detector.detect_all_cmake()

    config = load_config(root)
    saved = config.get("build") or {}

    sdk = saved.get("sdk_version")
    if sdk not in sdk_versions:
        sdk = sdk_versions[0]
    build_tools_ver = saved.get("build_tools")
    if build_tools_ver not in build_tools:
        build_tools_ver = build_tools[0] if build_tools else f"{sdk}.0.0"
    ndk = saved.get("ndk_version")
    if ndk not in ndk_versions:
        ndk = ndk_versions[0] if ndk_versions else None
    cmake = saved.get("cmake_version")
    if cmake not in cmake_versions:
        cmake = cmake_versions[0] if cmake_versions else "3.22.1"

    config["build"] = {
        "sdk_version": sdk,
        "build_tools": build_tools_ver,
        "ndk_version": ndk,
        "cmake_version": cmake,
    }
    save_config(root, config)
    print(
        "Android toolchain: SDK %s, build-tools %s, NDK %s, CMake %s"
        % (sdk, build_tools_ver, ndk or "default", cmake)
    )
    return 0


def patch_android_cmake(root: Path, android_dir: Path) -> None:
    """Make the Android target compile every source file in src/, not only
    src/main.c.  The CLI's template intentionally starts with one file; an
    existing project may have several."""
    cmake_path = android_dir / "app" / "src" / "main" / "cpp" / "CMakeLists.txt"
    if not cmake_path.exists():
        return
    try:
        content = cmake_path.read_text()
    except OSError:
        return

    src_dir = root / "src"
    sources = sorted(src_dir.rglob("*.c")) if src_dir.is_dir() else []
    cpp_dir = cmake_path.parent
    has_android_main = False
    for path in sources:
        try:
            text = path.read_text(errors="ignore")
        except OSError:
            continue
        if re.search(r"\bandroid_main\s*\(", text):
            has_android_main = True
            break

    shim = cpp_dir / "aroma_android_entry.c"
    if sources and not has_android_main:
        shim.write_text(
            "#include <aroma.h>\n"
            "#include <android_native_app_glue.h>\n"
            "\n"
            "extern int main(void);\n"
            "\n"
            "void android_main(struct android_app *state)\n"
            "{\n"
            "    aroma_android_set_app(state);\n"
            "    main();\n"
            "}\n"
        )
    elif shim.exists():
        try:
            shim.unlink()
        except OSError:
            pass

    if sources:
        rel_sources = [
            os.path.relpath(str(path), str(cpp_dir)).replace(os.sep, "/")
            for path in sources
        ]
        if shim.exists():
            rel_sources.append(
                os.path.relpath(str(shim), str(cpp_dir)).replace(os.sep, "/")
            )
        old = "add_library(aroma_app SHARED ../../../../../src/main.c ${ANDROID_GLUE_SRC})"
        new = (
            "add_library(aroma_app SHARED "
            + " ".join(rel_sources)
            + " ${ANDROID_GLUE_SRC})"
        )
        if old in content:
            content = content.replace(old, new)
        else:
            content = re.sub(
                r"add_library\(aroma_app SHARED[^\n]*\)", new, content, count=1
            )

        include_dirs = sorted(
            {
                path.parent
                for path in src_dir.rglob("*.h")
                if path.parent != src_dir
            }
        )
        extra = " ".join(
            os.path.relpath(str(path), str(cpp_dir)).replace(os.sep, "/")
            for path in include_dirs
        )
        old_inc = (
            "target_include_directories(aroma_app PRIVATE "
            "${AROMA_ROOT}/include ${ANDROID_GLUE_DIR})"
        )
        new_inc = (
            "target_include_directories(aroma_app PRIVATE "
            "${AROMA_ROOT}/include ${ANDROID_GLUE_DIR} "
            "../../../../../src"
        )
        if extra:
            new_inc += " " + extra
        new_inc += ")"
        if old_inc in content:
            content = content.replace(old_inc, new_inc)

    try:
        cmake_path.write_text(content)
    except OSError:
        pass


def copy_assets(root: Path, android_dir: Path) -> None:
    assets = android_dir / "app" / "src" / "main" / "assets"
    assets.mkdir(parents=True, exist_ok=True)

    for rel in ("ui", "assets"):
        source = root / rel
        if source.is_dir():
            shutil.copytree(source, assets / rel, dirs_exist_ok=True)

    src_dir = root / "src"
    if not src_dir.is_dir():
        return
    for entry in src_dir.iterdir():
        if entry.is_file() and entry.suffix == ".aroma":
            shutil.copy2(entry, assets / entry.name)
        elif entry.is_dir() and entry.name in ("assets", "demos"):
            shutil.copytree(entry, assets / entry.name, dirs_exist_ok=True)


def cmd_adb_devices(root: Path) -> int:
    from aroma_detector import Detector
    from aroma_util import get_connected_devices

    detector = Detector()
    adb = detector.get_adb_command()
    print("ADB:%s" % (adb or ""))
    run = load_config(root).get("run") or {}
    if run.get("device_serial"):
        print("SELECTED:%s" % run["device_serial"])
    if adb:
        # `adb devices -l` includes the model/product metadata shown by the
        # IDE device manager. Keep the older helper path as a fallback.
        result = subprocess.run(
            [adb, "devices", "-l"], capture_output=True, text=True
        )
        if result.returncode == 0:
            for line in result.stdout.splitlines()[1:]:
                parts = line.split(None, 2)
                if len(parts) >= 2 and parts[1] == "device":
                    details = parts[2].strip() if len(parts) > 2 else ""
                    print("DEVICE:%s|%s" % (parts[0], details))
        else:
            for serial in get_connected_devices(adb):
                print("DEVICE:%s" % serial)
    return 0


def cmd_set_device(root: Path, serial: str) -> int:
    config = load_config(root)
    run = config.get("run")
    if not isinstance(run, dict):
        run = {}
    run["device_serial"] = serial
    config["run"] = run
    save_config(root, config)
    print("Android device: %s" % serial)
    return 0


def cmd_setup_android(root: Path) -> int:
    if ensure_build_config(root) != 0:
        return 1

    from aroma_project import ProjectCreator

    config = load_config(root)
    name, package, min_sdk, target_sdk, compile_sdk = project_info(root, config)
    templates = REPO / "tools" / "cli" / "templates"
    android_dir = root / "android"
    template_android = templates / "android"

    if not android_dir.exists():
        if not template_android.is_dir():
            print("Android template not found: %s" % template_android)
            return 1
        shutil.copytree(template_android, android_dir)
        print("Created %s" % android_dir)

    replacements = {
        "{{PROJECT_NAME}}": name,
        "{{PACKAGE_NAME}}": package,
        "{{MIN_SDK}}": str(min_sdk),
        "{{TARGET_SDK}}": str(target_sdk),
        "{{COMPILE_SDK}}": str(compile_sdk),
        "{{AROMA_ROOT}}": str(REPO),
        "{{CHAQUOPY_CLASSPATH}}": "",
        "{{CHAQUOPY_APPLY}}": "",
        "{{CHAQUOPY_NDK}}": "",
        "{{CHAQUOPY_BLOCK}}": "",
        "{{AROMA_SHARED_LIBS}}": "",
    }

    creator = ProjectCreator(str(templates), config)
    try:
        creator._process_android_files(str(android_dir), replacements)
        settings = android_dir / "settings.gradle"
        if settings.exists():
            text = settings.read_text()
            text = re.sub(r'rootProject\.name\s*=\s*"[^"]*"',
                          'rootProject.name = "%s"' % name.replace('"', ''), text)
            settings.write_text(text)
        creator._setup_java_pkg(str(android_dir), package, replacements)
        creator._create_local_props(str(android_dir))
        if not creator._setup_gradle_wrapper(str(android_dir)):
            print("WARN: Gradle wrapper setup failed; the build may not start.")
    except Exception as exc:  # keep the IDE output useful instead of a traceback
        print("Android project setup failed: %s" % exc)
        return 1

    patch_android_cmake(root, android_dir)
    copy_assets(root, android_dir)

    project = config.get("project")
    if not isinstance(project, dict):
        project = {}
    project.update(
        {
            "name": name,
            "package": package,
            "language": "c",
            "min_sdk": min_sdk,
            "target_sdk": target_sdk,
            "compile_sdk": compile_sdk,
        }
    )
    config["project"] = project
    save_config(root, config)
    print("Android project ready: %s (%s)" % (name, package))
    return 0


def cmd_run_android(root: Path, serial: str = "") -> int:
    from aroma_detector import Detector
    from aroma_emu import install_and_launch_app
    from aroma_util import find_package_name, get_connected_devices

    detector = Detector()
    adb = detector.get_adb_command()
    if not adb:
        print("adb not found. Install Android platform-tools first.")
        return 1

    devices = get_connected_devices(adb)
    if not devices:
        print("No Android device or emulator is connected.")
        return 1
    if not serial:
        serial = devices[0]
    if serial not in devices:
        print("Device %s is not connected. Connected: %s" % (serial, ", ".join(devices)))
        return 1

    if cmd_setup_android(root) != 0:
        return 1

    cli = CLI_DIR / "aroma.py"
    if not cli.is_file():
        print("Aroma CLI not found: %s" % cli)
        return 1
    print("Building Android debug APK...")
    build = subprocess.run(
        [sys.executable, str(cli), "--no-color", "build", "android"],
        cwd=str(root),
        check=False,
    )
    if build.returncode != 0:
        print("Android build failed.")
        return build.returncode

    apk = root / "android" / "app" / "build" / "outputs" / "apk" / "debug" / "app-debug.apk"
    if not apk.exists():
        print("Debug APK not found: %s" % apk)
        print("The Android build completed without producing an APK.")
        return 1

    package = find_package_name(str(root))
    if not package:
        print("Could not determine the Android package name from android/app/build.gradle.")
        return 1

    print("Installing %s on %s..." % (apk.name, serial))
    ok = install_and_launch_app(adb, serial, str(apk), package)
    return 0 if ok else 1


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    command = sys.argv[1]
    root = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else Path.cwd()
    if not (root / "aroma.json").exists():
        print("No aroma.json in %s" % root)
        return 1

    if command == "adb-devices":
        return cmd_adb_devices(root)
    if command == "set-device" and len(sys.argv) > 3:
        return cmd_set_device(root, sys.argv[3])
    if command == "setup-android":
        return cmd_setup_android(root)
    if command == "run-android":
        return cmd_run_android(root, sys.argv[3] if len(sys.argv) > 3 else "")
    print("Unknown command: %s" % command)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
