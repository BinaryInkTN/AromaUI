#!/usr/bin/env python3
import argparse
import sys
import os
import subprocess
import shutil
import platform

# Colors for output
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

def print_step(msg):
    print(f"{Colors.OKBLUE}==>{Colors.ENDC} {Colors.BOLD}{msg}{Colors.ENDC}")

def print_success(msg):
    print(f"{Colors.OKGREEN}✓ {msg}{Colors.ENDC}")

def print_error(msg):
    print(f"{Colors.FAIL}✗ {msg}{Colors.ENDC}")

def run_command(cmd, cwd=None, env=None, capture_output=False):
    try:
        if capture_output:
            return subprocess.run(cmd, cwd=cwd, env=env, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        else:
            return subprocess.run(cmd, cwd=cwd, env=env, check=True)
    except subprocess.CalledProcessError as e:
        if capture_output:
            print(e.stderr)
        return None
    except FileNotFoundError:
        return None

def find_aroma_root():
    # Assuming this script is at <root>/tools/cli/aroma.py
    script_path = os.path.realpath(__file__)
    return os.path.abspath(os.path.join(os.path.dirname(script_path), "../../"))

AROMA_ROOT = find_aroma_root()

def cmd_doctor(args):
    print_step("Running Aroma Doctor...")
    
    # 1. Check OS
    print(f"OS: {platform.system()} {platform.release()}")
    
    # 2. Check CMake
    cmake = run_command(["cmake", "--version"], capture_output=True)
    if cmake:
        print_success(f"CMake: {cmake.stdout.splitlines()[0]}")
    else:
        print_error("CMake not found")

    # 3. Check Ninja
    ninja = run_command(["ninja", "--version"], capture_output=True)
    if ninja:
        print_success(f"Ninja: {ninja.stdout.strip()}")
    else:
        print_error("Ninja not found (Recommended for fast builds)")

    # 4. Check Compilers
    gcc = run_command(["gcc", "--version"], capture_output=True)
    if gcc:
        print_success("GCC installed")
    
    # 5. Check Android SDK
    android_home = os.environ.get("ANDROID_HOME")
    if not android_home:
        # Try local.properties logic or common paths
        common_paths = [
            os.path.expanduser("~/Android/Sdk"),
            "/usr/lib/android-sdk"
        ]
        for p in common_paths:
            if os.path.exists(p):
                android_home = p
                break
    
    if android_home and os.path.exists(android_home):
        print_success(f"Android SDK: {android_home}")
        # Check NDK
        ndk_path = os.path.join(android_home, "ndk")
        if os.path.exists(ndk_path) and os.listdir(ndk_path):
            print_success("Android NDK installed")
        else:
            print_error("Android NDK not found in SDK")
    else:
        print_error("Android SDK not found (Set ANDROID_HOME)")

    print_step("Doctor summary complete.")

def cmd_create(args):
    project_name = args.name
    target_dir = os.path.abspath(project_name)
    
    if os.path.exists(target_dir):
        print_error(f"Directory {project_name} already exists.")
        return

    print_step(f"Creating project {project_name}...")
    
    # 1. Create directory structure
    os.makedirs(target_dir)
    os.makedirs(os.path.join(target_dir, "src"))
    
    # 2. Create main.c
    template_dir = os.path.join(os.path.dirname(__file__), "templates", "app")
    
    with open(os.path.join(template_dir, "main.c.tpl"), "r") as f:
        main_c_content = f.read().replace("{{PROJECT_NAME}}", project_name)

    with open(os.path.join(target_dir, "src", "main.c"), "w") as f:
        f.write(main_c_content)

    # 3. Create CMakeLists.txt (Linux)
    with open(os.path.join(template_dir, "CMakeLists.txt.tpl"), "r") as f:
        cmake_content = f.read().replace("{{PROJECT_NAME}}", project_name).replace("{{AROMA_ROOT}}", AROMA_ROOT)

    with open(os.path.join(target_dir, "CMakeLists.txt"), "w") as f:
        f.write(cmake_content)

    # 4. Copy Android Project Template
    # Use the template from tools/cli/templates/android
    android_tmpl = os.path.join(os.path.dirname(__file__), "templates", "android")
    android_dest = os.path.join(target_dir, "android")
    
    if os.path.exists(android_tmpl):
        shutil.copytree(android_tmpl, android_dest)
    else:
        print_error(f"Android template not found at {android_tmpl}")
        # Create minimal android dir anyway so we don't crash later?
        # But without build files it won't work.
        pass

    # 5. Create Android helper script or update android/app/src/main/cpp/CMakeLists.txt
    # Modify android/app/src/main/cpp/CMakeLists.txt to include user source
    android_cmake = os.path.join(android_dest, "app", "src", "main", "cpp", "CMakeLists.txt")
    if os.path.exists(android_cmake):
        with open(android_cmake, "w") as f:
            f.write(f"""cmake_minimum_required(VERSION 3.22.1)
project("{project_name}_android")

set(AROMA_ROOT "{AROMA_ROOT}")
# 1. Setup FreeType
set(FT_DISABLE_ZLIB TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG TRUE CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI TRUE CACHE BOOL "" FORCE)
add_subdirectory(${{AROMA_ROOT}}/vendors/freetype freetype_build)

# 2. Setup AromaUI
add_subdirectory(${{AROMA_ROOT}}/src aroma_lib)

# 3. Setup Android Glue
# We compile glue directly into the shared lib to ensure the entry point symbol is exported
set(ANDROID_GLUE_DIR ${{ANDROID_NDK}}/sources/android/native_app_glue)
set(ANDROID_GLUE_SRC ${{ANDROID_GLUE_DIR}}/android_native_app_glue.c)

# 4. Main App
add_library(aroma_app SHARED ../../../../../src/main.c ${{ANDROID_GLUE_SRC}})

target_include_directories(aroma_app PRIVATE ${{AROMA_ROOT}}/include ${{ANDROID_GLUE_DIR}})

target_link_libraries(aroma_app android aroma log)
""")
            
    # Modify local.properties if we can find SDK
    android_home = os.environ.get("ANDROID_HOME") or os.path.expanduser("~/Android/Sdk")
    if os.path.exists(android_home):
        with open(os.path.join(android_dest, "local.properties"), "w") as f:
            f.write(f"sdk.dir={android_home}")

    print_success(f"Project '{project_name}' created!")
    print(f"\nTo run:\n  cd {project_name}\n  aroma run linux")

def cmd_build(args):
    print_step(f"Building for {args.platform}...")
    cwd = os.getcwd()
    
    if args.platform == "linux":
        build_dir = os.path.join(cwd, "build")
        os.makedirs(build_dir, exist_ok=True)
        
        # 1. CMake Config
        res = run_command(["cmake", ".."], cwd=build_dir)
        if res is None or res.returncode != 0:
            print_error("CMake configuration failed")
            return
            
        # 2. Make
        res = run_command(["make", "-j4"], cwd=build_dir)
        if res is None or res.returncode != 0:
            print_error("Build failed")
            return
            
        print_success("Build successful!")
        
    elif args.platform == "android":
        android_dir = os.path.join(cwd, "android")
        if not os.path.exists(android_dir):
            print_error("No 'android' directory found. Is this an Aroma project?")
            return
            
        # Check execution permissions for gradle wrapper
        gradlew = os.path.join(android_dir, "gradlew")
        if os.path.exists(gradlew):
            os.chmod(gradlew, 0o755)
            cmd = [gradlew, "assembleDebug"]
        else:
            cmd = ["gradle", "assembleDebug"]
            
        res = run_command(cmd, cwd=android_dir)
        if res is None or res.returncode != 0:
            print_error("Android build failed")
            return
            
        apk_path = os.path.join(android_dir, "app/build/outputs/apk/debug/app-debug.apk")
        print_success(f"APK built at: {apk_path}")

def cmd_run(args):
    # Ensure built first?
    cmd_build(args)
    cwd = os.getcwd()
    
    if args.platform == "linux":
        # Find executable. Usually name of dir.
        project_name = os.path.basename(cwd)
        exe_path = os.path.join(cwd, "build", project_name)
        
        if not os.path.exists(exe_path):
            # Fallback scan
            files = [f for f in os.listdir(os.path.join(cwd, "build")) if os.access(os.path.join(cwd, "build", f), os.X_OK) and not f.endswith(".so")]
            if files:
                exe_path = os.path.join(cwd, "build", files[0])
        
        if os.path.exists(exe_path):
            print_step(f"Launching {exe_path}...")
            subprocess.run([exe_path])
        else:
            print_error("Executable not found.")

    elif args.platform == "android":
        print_step("Installing to device...")
        android_dir = os.path.join(cwd, "android")
        apk_path = os.path.join(android_dir, "app/build/outputs/apk/debug/app-debug.apk")
        
        if not os.path.exists(apk_path):
            print_error("APK not found")
            return
            
        res = run_command(["adb", "install", "-r", apk_path])
        if res is not None and res.returncode == 0:
            print_success("Installed.")
            # Launch? We need package name.
            # grep package name from build.gradle
            build_gradle = os.path.join(android_dir, "app/build.gradle")
            package_name = "com.example.aromaapp" # Default fallback
            if os.path.exists(build_gradle):
                with open(build_gradle, 'r') as f:
                    for line in f:
                        if "applicationId" in line:
                            # applicationId "com.example.aromaapp"
                            import re
                            # Match text inside quotes
                            m = re.search(r'applicationId\s+[\"\']([^\"\']+)[\"\']', line)
                            if m:
                                package_name = m.group(1)
                                break
            
            print_step(f"Launching {package_name}...")
            # Assuming standard activity from template shell
            run_command(["adb", "shell", "am", "start", "-n", f"{package_name}/android.app.NativeActivity"])

def main():
    parser = argparse.ArgumentParser(prog="aroma", description="AromaUI CLI Tool")
    subparsers = parser.add_subparsers(dest="command", required=True)
    
    # commands
    subparsers.add_parser("doctor", help="Check environment")
    
    create_p = subparsers.add_parser("create", help="Create new project")
    create_p.add_argument("name", help="Project name")
    
    build_p = subparsers.add_parser("build", help="Build project")
    build_p.add_argument("platform", choices=["linux", "android"], default="linux")
    
    run_p = subparsers.add_parser("run", help="Run project")
    run_p.add_argument("platform", choices=["linux", "android"], default="linux")
    
    args = parser.parse_args()
    
    if args.command == "doctor":
        cmd_doctor(args)
    elif args.command == "create":
        cmd_create(args)
    elif args.command == "build":
        cmd_build(args)
    elif args.command == "run":
        cmd_run(args)

if __name__ == "__main__":
    main()
