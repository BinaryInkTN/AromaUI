"""Build and run commands for Linux, Android, and Web targets."""

import os
import platform
import stat
import subprocess
import sys
import time

import aroma_emu as _emu
from aroma_detector import Detector
from aroma_emu import (
    cleanup_emulator,
    install_and_launch_app,
    select_from_list,
    start_emulator,
)
from aroma_output import Colors, Logger
from aroma_project import ProjectCreator
from aroma_util import (
    find_emscripten_sdk,
    find_package_name,
    get_connected_devices,
    load_config,
    run_command,
    save_project_config,
)

def cmd_build(args):
    detector = Detector()
    cwd = os.getcwd()
    config = load_config()
    
    if args.platform == "android":
        sdk_versions = detector.detect_all_sdk()
        build_tools = detector.detect_all_build_tools()
        ndk_versions = detector.detect_all_ndk()
        cmake_versions = detector.detect_all_cmake()
        
        if not sdk_versions:
            print(f"{Colors.FAIL}No Android SDK platforms found. Install via Android SDK Manager.{Colors.ENDC}")
            sys.exit(1)
        
        saved_build = config.get("build", {})
        
        if args.sdk_version:
            sdk_ver = args.sdk_version
        elif saved_build.get("sdk_version") in sdk_versions:
            sdk_ver = saved_build["sdk_version"]
            print(f"{Colors.OKCYAN}Using saved SDK version: {sdk_ver}{Colors.ENDC}")
        else:
            idx = select_from_list("Select SDK Platform:", sdk_versions)
            if idx is None:
                sys.exit(0)
            sdk_ver = sdk_versions[idx]
        
        if args.build_tools:
            bt_ver = args.build_tools
        elif saved_build.get("build_tools") in build_tools:
            bt_ver = saved_build["build_tools"]
            print(f"{Colors.OKCYAN}Using saved build tools version: {bt_ver}{Colors.ENDC}")
        elif build_tools:
            idx = select_from_list("Select Build Tools:", build_tools)
            if idx is None:
                sys.exit(0)
            bt_ver = build_tools[idx]
        else:
            bt_ver = f"{sdk_ver}.0.0"
        
        if args.ndk_version:
            ndk_ver = args.ndk_version
        elif saved_build.get("ndk_version") in ndk_versions:
            ndk_ver = saved_build["ndk_version"]
            print(f"{Colors.OKCYAN}Using saved NDK version: {ndk_ver}{Colors.ENDC}")
        elif ndk_versions:
            idx = select_from_list("Select NDK:", ndk_versions)
            if idx is None:
                sys.exit(0)
            ndk_ver = ndk_versions[idx]
        else:
            ndk_ver = None
        
        if args.cmake_version:
            cmake_ver = args.cmake_version
        elif saved_build.get("cmake_version") in cmake_versions:
            cmake_ver = saved_build["cmake_version"]
            print(f"{Colors.OKCYAN}Using saved CMake version: {cmake_ver}{Colors.ENDC}")
        elif cmake_versions:
            idx = select_from_list("Select CMake:", cmake_versions)
            if idx is None:
                sys.exit(0)
            cmake_ver = cmake_versions[idx]
        else:
            cmake_ver = "3.22.1"
        
        build_config = {
            "build": {
                "sdk_version": sdk_ver,
                "build_tools": bt_ver,
                "ndk_version": ndk_ver,
                "cmake_version": cmake_ver,
            }
        }
        save_project_config(cwd, build_config)
        
        print(f"\n{Colors.BOLD}Building with:{Colors.ENDC}")
        print(f"  SDK:        {sdk_ver}")
        print(f"  Build Tools: {bt_ver}")
        if ndk_ver:
            print(f"  NDK:        {ndk_ver}")
        print(f"  CMake:      {cmake_ver}")
        print()
    
    elif args.platform == "linux":
        sdk_ver = ndk_ver = bt_ver = cmake_ver = None
    
    elif args.platform == "web":
        sdk_ver = ndk_ver = bt_ver = cmake_ver = None
        emsdk_dir = find_emscripten_sdk()
        if not emsdk_dir:
            print(f"{Colors.FAIL}ERROR: Emscripten SDK not found. Install it under vendors/emscripten or ~/.aroma/vendors/emscripten.{Colors.ENDC}")
            sys.exit(1)
        print(f"{Colors.OKCYAN}Using Emscripten SDK: {emsdk_dir}{Colors.ENDC}")
    
    _do_build(args.platform, detector.sdk_root, sdk_ver, bt_ver, ndk_ver, cmake_ver, args.release, args.aab)


def _sync_project_assets(cwd: str, android_dir: str):
    import shutil
    src_dir = os.path.join(cwd, "src")
    assets_dir = os.path.join(android_dir, "app", "src", "main", "assets")
    if not os.path.isdir(src_dir):
        return
    os.makedirs(assets_dir, exist_ok=True)
    for entry in sorted(os.listdir(src_dir)):
        src = os.path.join(src_dir, entry)
        if entry == "main.c" or entry.startswith("CMakeLists"):
            continue
        if os.path.isfile(src) and entry.endswith(".aroma"):
            shutil.copy2(src, os.path.join(assets_dir, entry))
        elif os.path.isdir(src) and entry in ("demos", "assets"):
            dst = assets_dir if entry == "assets" else os.path.join(assets_dir, entry)
            shutil.copytree(src, dst, dirs_exist_ok=True)


def _do_build(build_platform: str, sdk_root: str, sdk_ver: str, bt_ver: str, ndk_ver: str, cmake_ver: str, release: bool, aab: bool):
    cwd = os.getcwd()
    emsdk_dir = find_emscripten_sdk()
    
    if build_platform == "linux":
        build_dir = os.path.join(cwd, "build")
        os.makedirs(build_dir, exist_ok=True)

        build_type = "Release" if release else "Debug"
        print(f"{Colors.OKBLUE}==>{Colors.ENDC} Configuring ({build_type})...")
        result = run_command(["cmake", "..", f"-DCMAKE_BUILD_TYPE={build_type}"],
                             cwd=build_dir)
        if result is None or result.returncode != 0:
            print(f"{Colors.FAIL}ERROR: CMake configuration failed{Colors.ENDC}")
            sys.exit(1)
        
        print(f"{Colors.OKBLUE}==>{Colors.ENDC} Building...")
        result = run_command(["make", f"-j{os.cpu_count() or 4}"], cwd=build_dir)
        if result is None or result.returncode != 0:
            print(f"{Colors.FAIL}ERROR: Build failed{Colors.ENDC}")
            sys.exit(1)
        
        print(f"{Colors.OKGREEN}OK Build successful{Colors.ENDC}")
    
    elif build_platform == "web":
        if not emsdk_dir:
            print(f"{Colors.FAIL}ERROR: Emscripten SDK not found. Install it under vendors/emscripten or ~/.aroma/vendors/emscripten.{Colors.ENDC}")
            sys.exit(1)
        
        build_dir = os.path.join(cwd, "build_web")
        os.makedirs(build_dir, exist_ok=True)
        
        emscripten_root = os.path.join(emsdk_dir, "upstream", "emscripten")
        emcmake = os.path.join(emscripten_root, "emcmake")
        emmake = os.path.join(emscripten_root, "emmake")
        if platform.system() == "Windows":
            emcmake += ".bat"
            emmake += ".bat"
        
        env = os.environ.copy()
        env["EMSDK"] = emsdk_dir
        
        print(f"{Colors.OKBLUE}==>{Colors.ENDC} Configuring (Emscripten)...")
        result = run_command([emcmake, "cmake", "..", "-DCMAKE_BUILD_TYPE=Release"], cwd=build_dir, env=env)
        if result is None or result.returncode != 0:
            print(f"{Colors.FAIL}ERROR: CMake configuration failed{Colors.ENDC}")
            sys.exit(1)
        
        print(f"{Colors.OKBLUE}==>{Colors.ENDC} Building...")
        result = run_command([emmake, "make", f"-j{os.cpu_count() or 4}"], cwd=build_dir, env=env)
        if result is None or result.returncode != 0:
            print(f"{Colors.FAIL}ERROR: Build failed{Colors.ENDC}")
            sys.exit(1)
        
        print(f"{Colors.OKGREEN}OK Build successful{Colors.ENDC}")
        print(f"  Output: {build_dir}")
    
    elif build_platform == "android":
        android_dir = os.path.join(cwd, "android")
        if not os.path.exists(android_dir):
            print(f"{Colors.FAIL}ERROR: Not an Aroma project (no android/ directory){Colors.ENDC}")
            sys.exit(1)
        _sync_project_assets(cwd, android_dir)
        
        gradlew = os.path.join(android_dir, "gradlew")
        if not os.path.exists(gradlew):
            print(f"{Colors.WARNING}WARN: gradlew not found in this project, attempting to generate it...{Colors.ENDC}")
            detector = Detector()
            creator = ProjectCreator(
                os.path.join(os.path.dirname(os.path.realpath(__file__)), "templates"),
                {},
            )
            if not creator._setup_gradle_wrapper(android_dir):
                print(f"{Colors.FAIL}ERROR: Gradle wrapper not found and could not be generated. "
                      f"Install a Gradle distribution under {os.path.join(detector.install_path, 'gradle')} "
                      f"and re-run the build.{Colors.ENDC}")
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
    config = load_config()
    
    if args.platform == "linux":
        build_dir = os.path.join(cwd, "build")
        exe = os.path.join(build_dir, os.path.basename(cwd))
        if not os.path.exists(exe):
            if os.path.isdir(build_dir):
                for f in sorted(os.listdir(build_dir)):
                    fp = os.path.join(build_dir, f)
                    if os.path.isfile(fp) and os.access(fp, os.X_OK) and not f.endswith('.so'):
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
        
        adb_cmd = detector.get_adb_command()
        if not adb_cmd:
            print(f"{Colors.FAIL}ERROR: ADB not found{Colors.ENDC}")
            sys.exit(1)
        
        saved_run = config.get("run", {})
        
        if args.emu:
            serial = start_emulator(detector)
            if not serial:
                sys.exit(1)
        else:
            devices = get_connected_devices(adb_cmd)
            if not devices:
                print(f"{Colors.FAIL}ERROR: No device connected{Colors.ENDC}")
                sys.exit(1)
            
            if len(devices) == 1:
                serial = devices[0]
            elif saved_run.get("device_serial") in devices:
                serial = saved_run["device_serial"]
                print(f"{Colors.OKCYAN}Using saved device: {serial}{Colors.ENDC}")
            else:
                emulators = [d for d in devices if d.startswith("emulator-")]
                if emulators:
                    serial = emulators[0]
                else:
                    serial = devices[0]
        
        run_config = {
            "run": {
                "device_serial": serial,
            }
        }
        save_project_config(cwd, run_config)
        
        pkg = find_package_name(cwd)
        if not pkg:
            print(f"{Colors.FAIL}ERROR: Could not determine package name{Colors.ENDC}")
            sys.exit(1)
        
        if not install_and_launch_app(adb_cmd, serial, apk_path, pkg):
            sys.exit(1)
        
        if serial.startswith("emulator-"):
            print(f"\n{Colors.OKGREEN}App launched on emulator. Press Ctrl+C to stop the emulator.{Colors.ENDC}")
            try:
                while True:
                    if _emu._EMULATOR_PROCESS and _emu._EMULATOR_PROCESS.poll() is not None:
                        break
                    devices = get_connected_devices(adb_cmd)
                    if serial not in devices:
                        break
                    time.sleep(1)
            except KeyboardInterrupt:
                print()
                Logger.info("Stopping emulator...")
                cleanup_emulator(adb_cmd)
    
    elif args.platform == "web":
        build_dir = os.path.join(cwd, "build_web")
        if not os.path.isdir(build_dir):
            print(f"{Colors.FAIL}ERROR: build_web/ not found. Run 'aroma build web' first.{Colors.ENDC}")
            sys.exit(1)
        
        port = 8080
        while port < 8100:
            try:
                import http.server
                import socketserver
                
                class ReusableTCPServer(socketserver.TCPServer):
                    allow_reuse_address = True
                
                class Handler(http.server.SimpleHTTPRequestHandler):
                    def __init__(self, *args, **kwargs):
                        super().__init__(*args, directory=build_dir, **kwargs)
                
                print(f"{Colors.OKBLUE}==>{Colors.ENDC} Serving at http://localhost:{port}")
                print("Press Ctrl+C to stop")
                
                with ReusableTCPServer(("", port), Handler) as httpd:
                    try:
                        httpd.serve_forever()
                    except KeyboardInterrupt:
                        print()
                        httpd.shutdown()
                break
            except OSError as e:
                if e.errno == 98:
                    port += 1
                else:
                    raise
        else:
            print(f"{Colors.FAIL}ERROR: Could not find available port in range 8080-8099{Colors.ENDC}")
            sys.exit(1)
