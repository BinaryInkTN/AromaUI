#!/usr/bin/env python3
import argparse
import sys
import os
import subprocess
import shutil
import platform
import re
import urllib.request
import zipfile
import ssl

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

def print_info(msg):
    print(f"{Colors.OKCYAN}i {msg}{Colors.ENDC}")

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
    script_path = os.path.realpath(__file__)
    return os.path.abspath(os.path.join(os.path.dirname(script_path), "../../"))

AROMA_ROOT = find_aroma_root()

def get_input(prompt, default=None, validator=None, error_msg="Invalid input"):
    while True:
        prompt_str = f"{Colors.BOLD}{prompt}{Colors.ENDC}"
        if default:
            prompt_str += f" [{default}]"
        prompt_str += ": "
        
        try:
            val = input(prompt_str).strip()
        except KeyboardInterrupt:
            print()
            sys.exit(0)

        if not val and default is not None:
            return default
        
        if not val:
            continue
            
        if validator and not validator(val):
            print_error(error_msg)
            continue
            
        return val

def install_android_sdk():
    default_sdk_root = os.path.expanduser("~/Android/Sdk")
    sdk_root = os.environ.get("ANDROID_HOME", default_sdk_root)

    print_step(f"Setting up Android SDK at {sdk_root}...")

    cmdline_tools_dir = os.path.join(sdk_root, "cmdline-tools")
    latest_dir = os.path.join(cmdline_tools_dir, "latest")
    
    if not os.path.exists(latest_dir):
        os.makedirs(cmdline_tools_dir, exist_ok=True)
        
        if platform.system() == "Linux":
            url = "https://dl.google.com/android/repository/commandlinetools-linux-10406996_latest.zip"
        elif platform.system() == "Darwin":
             url = "https://dl.google.com/android/repository/commandlinetools-mac-10406996_latest.zip"
        else:
             print_error("Auto-install only supported on Linux/Mac currently.")
             return False

        zip_path = os.path.join(sdk_root, "cmdline-tools.zip")
        
        print_info(f"Downloading Command Line Tools from {url}...")
        try:
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            
            with urllib.request.urlopen(url, context=ctx) as r, open(zip_path, 'wb') as f:
                shutil.copyfileobj(r, f)
        except Exception as e:
            print_error(f"Download failed: {e}")
            return False

        print_info("Extracting...")
        try:
            with zipfile.ZipFile(zip_path, 'r') as zip_ref:
                zip_ref.extractall(cmdline_tools_dir)
            os.remove(zip_path)
            
            extracted_folder = os.path.join(cmdline_tools_dir, "cmdline-tools")
            if os.path.exists(extracted_folder):
                shutil.move(extracted_folder, latest_dir)
            else:
                 print_error(f"Unexpected zip structure. Expected 'cmdline-tools' folder inside.")
                 return False
                 
        except Exception as e:
            print_error(f"Extraction failed: {e}")
            return False

    sdkmanager = os.path.join(latest_dir, "bin", "sdkmanager")
    if not os.path.exists(sdkmanager):
        print_error("sdkmanager not found after installation.")
        return False
    
    # Check for Java
    if not run_command(["which", "java"], capture_output=True) and not os.environ.get("JAVA_HOME"):
        print_error("Java is required for Android SDK but not found. Please install OpenJDK.")
        return False

    os.chmod(sdkmanager, 0o755)
        
    print_step("Installing SDK Components (Accepting Licenses)...")
    
    try:
        # Use bash explicitly to avoid permission issues with the script
        cmd = ["bash", sdkmanager, "--licenses"] if platform.system() == "Linux" else [sdkmanager, "--licenses"]
        p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        p.communicate(input=b"y\n" * 50)
    except Exception as e:
        print_error(f"Failed to accept licenses: {e}")
    
    packages = [
        "platform-tools",
        "platforms;android-34",
        "build-tools;34.0.0",
        "ndk;25.1.8937393",
        "cmake;3.22.1"
    ]
    
    print_info(f"Installing: {', '.join(packages)}")
    
    # Use bash here too
    install_cmd = ["bash", sdkmanager] + packages if platform.system() == "Linux" else [sdkmanager] + packages
    res = run_command(install_cmd)
    
    if res and res.returncode == 0:
        print_success("Android SDK & NDK installed successfully!")
        return True
    else:
        print_error("Failed to install components.")
        return False


def validate_package_name(name):
    return re.match(r'^[a-z][a-z0-9_]*(\.[a-z0-9_]+)+$', name) is not None

def validate_int(val):
    try:
        int(val)
        return True
    except ValueError:
        return False

def cmd_doctor(args):
    print_step("Running Aroma Doctor...")
    
    print(f"OS: {platform.system()} {platform.release()}")
    
    cmake = run_command(["cmake", "--version"], capture_output=True)
    if cmake:
        print_success(f"CMake: {cmake.stdout.splitlines()[0]}")
    else:
        print_error("CMake not found")

    ninja = run_command(["ninja", "--version"], capture_output=True)
    if ninja:
        print_success(f"Ninja: {ninja.stdout.strip()}")
    else:
        print_info("Ninja not found (Recommended for fast builds)")

    gcc = run_command(["gcc", "--version"], capture_output=True)
    if gcc:
        print_success("GCC installed")
    else:
        print_error("GCC not found")
    
    android_home = os.environ.get("ANDROID_HOME")
    if not android_home:
        common_paths = [
            os.path.expanduser("~/Android/Sdk"),
            "/usr/lib/android-sdk",
            "/Library/Android/sdk"
        ]
        for p in common_paths:
            if os.path.exists(p):
                android_home = p
                break
    
    if android_home and os.path.exists(android_home):
        print_success(f"Android SDK: {android_home}")
        ndk_path = os.path.join(android_home, "ndk")
        if os.path.exists(ndk_path) and os.listdir(ndk_path):
            versions = sorted(os.listdir(ndk_path))
            print_success(f"Android NDK installed (found: {versions[-1]})")
        else:
            print_error("Android NDK not found in SDK (required for Android builds)")
            try_install = get_input("Install Android SDK & NDK?", default="Y", validator=lambda x: x.lower() in ['y', 'n'])
            if try_install.lower() == 'y':
                install_android_sdk()
                
    else:
        print_error("Android SDK not found (Set ANDROID_HOME)")
        try_install = get_input("Install Android SDK & NDK?", default="Y", validator=lambda x: x.lower() in ['y', 'n'])
        if try_install.lower() == 'y':
            install_android_sdk()

    print_step("Doctor summary complete.")

def cmd_create(args):
    print_step("Configure New Project")
    
    project_name = args.name
    if not project_name:
        project_name = get_input("Project Name", validator=lambda x: len(x) > 0)
    
    target_dir = os.path.abspath(project_name)
    if os.path.exists(target_dir):
        print_error(f"Directory '{project_name}' already exists.")
        return

    default_pkg = f"com.example.{project_name.lower().replace('-', '_')}"
    package_name = get_input("Android Package Name", default=default_pkg, validator=validate_package_name, error_msg="Invalid package name format (e.g. com.example.app)")
    
    min_sdk = get_input("Min Android SDK", default="24", validator=validate_int, error_msg="Must be an integer")
    target_sdk = get_input("Target Android SDK", default="34", validator=validate_int, error_msg="Must be an integer")
    compile_sdk = get_input("Compile Android SDK", default="34", validator=validate_int, error_msg="Must be an integer")

    print("\n" + Colors.BOLD + "Configuration Summary:" + Colors.ENDC)
    print(f"  Name:        {project_name}")
    print(f"  Directory:   {target_dir}")
    print(f"  Package:     {package_name}")
    print(f"  Min SDK:     {min_sdk}")
    print(f"  Target SDK:  {target_sdk}")
    print(f"  Compile SDK: {compile_sdk}")
    print(f"  Aroma Root:  {AROMA_ROOT}")
    
    confirm = get_input("\nCreate Project?", default="Y", validator=lambda x: x.lower() in ['y', 'n', 'yes', 'no'])
    if confirm.lower().startswith('n'):
        print("Aborted.")
        return

    print_step(f"Creating project {project_name}...")
    
    try:
        os.makedirs(target_dir)
        os.makedirs(os.path.join(target_dir, "src"))

        templates_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "templates")

        with open(os.path.join(templates_dir, "app", "main.c.tpl"), "r") as f:
            main_c = f.read().replace("{{PROJECT_NAME}}", project_name)
        with open(os.path.join(target_dir, "src", "main.c"), "w") as f:
            f.write(main_c)
            
        with open(os.path.join(templates_dir, "app", "CMakeLists.txt.tpl"), "r") as f:
            cmake_txt = f.read().replace("{{PROJECT_NAME}}", project_name).replace("{{AROMA_ROOT}}", AROMA_ROOT)
        with open(os.path.join(target_dir, "CMakeLists.txt"), "w") as f:
            f.write(cmake_txt)

        android_dest = os.path.join(target_dir, "android")
        android_tmpl = os.path.join(templates_dir, "android")
        
        if os.path.exists(android_tmpl):
            shutil.copytree(android_tmpl, android_dest)
            
            replacements = {
                "{{PROJECT_NAME}}": project_name,
                "{{PACKAGE_NAME}}": package_name,
                "{{MIN_SDK}}": min_sdk,
                "{{TARGET_SDK}}": target_sdk,
                "{{COMPILE_SDK}}": compile_sdk,
                "{{AROMA_ROOT}}": AROMA_ROOT
            }
            
            for root, dirs, files in os.walk(android_dest):
                for file in files:
                    file_path = os.path.join(root, file)
                    
                    if file.endswith(".tpl"):
                        new_path = file_path[:-4]
                        with open(file_path, "r") as f:
                            content = f.read()
                        
                        for k, v in replacements.items():
                            content = content.replace(k, str(v))
                            
                        with open(new_path, "w") as f:
                            f.write(content)
                        os.remove(file_path)
                        continue
                    
                    if file in ["build.gradle", "AndroidManifest.xml", "settings.gradle"]:
                        with open(file_path, "r") as f:
                            content = f.read()
                        
                        changed = False
                        for k, v in replacements.items():
                            if k in content:
                                content = content.replace(k, str(v))
                                changed = True
                        
                        if changed:
                            with open(file_path, "w") as f:
                                f.write(content)

            android_home = os.environ.get("ANDROID_HOME") or os.path.expanduser("~/Android/Sdk")
            if os.path.exists(android_home):
                with open(os.path.join(android_dest, "local.properties"), "w") as f:
                    f.write(f"sdk.dir={android_home}\n")

    except Exception as e:
        print_error(f"Failed to create project: {e}")
        return

    print_success(f"Project '{project_name}' created successfully!")
    print(f"\nTo run on Linux:\n  cd {project_name}\n  aroma run linux")
    print(f"\nTo run on Android:\n  cd {project_name}\n  aroma run android")

def cmd_build(args):
    print_step(f"Building for {args.platform}...")
    cwd = os.getcwd()
    
    if args.platform == "linux":
        build_dir = os.path.join(cwd, "build")
        os.makedirs(build_dir, exist_ok=True)
        
        res = run_command(["cmake", ".."], cwd=build_dir)
        if res is None or res.returncode != 0:
            print_error("CMake configuration failed")
            return
            
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
            
        # Check Android SDK presence before building
        android_home = os.environ.get("ANDROID_HOME")
        if not android_home:
            common_paths = [
                os.path.expanduser("~/Android/Sdk"),
                "/usr/lib/android-sdk",
                "/Library/Android/sdk"
            ]
            for p in common_paths:
                if os.path.exists(p):
                    android_home = p
                    break
        
        # If SDK missing or incomplete, auto-install
        should_install = False
        if not android_home or not os.path.exists(android_home):
            print_info("Android SDK not found.")
            should_install = True
        else:
            ndk_path = os.path.join(android_home, "ndk")
            if not os.path.exists(ndk_path) or not os.listdir(ndk_path):
                print_info("Android NDK not found.")
                should_install = True
        
        if should_install:
            print_info("triggering auto-installation...")
            if not install_android_sdk():
                print_error("Aborting build due to missing SDK/NDK.")
                return
            # Refresh ANDROID_HOME if it was installed to default location
            if not android_home:
                android_home = os.path.expanduser("~/Android/Sdk")
                os.environ["ANDROID_HOME"] = android_home

        # Determine NDK path
        ndk_path_root = os.path.join(android_home, "ndk")
        ndk_dir = None
        if os.path.exists(ndk_path_root):
            versions = sorted([d for d in os.listdir(ndk_path_root) if os.path.isdir(os.path.join(ndk_path_root, d))])
            if versions:
                ndk_dir = os.path.join(ndk_path_root, versions[-1])
                os.environ["ANDROID_NDK_HOME"] = ndk_dir
                
        # Ensure local.properties exists/is correct
        local_prop = os.path.join(android_dir, "local.properties")
        if not os.path.exists(local_prop) and android_home:
             with open(local_prop, "w") as f:
                 f.write(f"sdk.dir={android_home}\n")
                 if ndk_dir:
                     f.write(f"ndk.dir={ndk_dir}\n")

        gradlew = os.path.join(android_dir, "gradlew")
        if os.path.exists(gradlew):
            os.chmod(gradlew, 0o755)
            cmd = [gradlew, "assembleDebug"]
        else:
            print_info("gradlew not found, trying system gradle")
            cmd = ["gradle", "assembleDebug"]
            
        res = run_command(cmd, cwd=android_dir)
        if res is None or res.returncode != 0:
            print_error("Android build failed")
            return
            
        apk_path = os.path.join(android_dir, "app/build/outputs/apk/debug/app-debug.apk")
        if os.path.exists(apk_path):
             print_success(f"APK built at: {apk_path}")
        else:
             print_error("Build finished but APK not found.")

def cmd_run(args):
    cmd_build(args) 
    
    cwd = os.getcwd()
    
    if args.platform == "linux":
        project_name = os.path.basename(os.path.abspath(cwd))
        exe_path = os.path.join(cwd, "build", project_name)
        
        if not os.path.exists(exe_path):
            if os.path.exists(os.path.join(cwd, "build")):
                 files = [f for f in os.listdir(os.path.join(cwd, "build")) if os.access(os.path.join(cwd, "build", f), os.X_OK) and not f.endswith(".so") and not os.path.isdir(os.path.join(cwd, "build", f)) and f != "Makefile" and f != "cmake_install.cmake"]
                 if files:
                     exe_path = os.path.join(cwd, "build", files[0])
        
        if os.path.exists(exe_path):
            print_step(f"Launching {exe_path}...")
            subprocess.run([exe_path])
        else:
            print_error("Executable not found. Did you run 'aroma build linux'?")

    elif args.platform == "android":
        print_step("Checking connected devices...")
        res = run_command(["adb", "devices"], capture_output=True)
        if not res or "device" not in res.stdout.replace("List of devices attached", "").strip():
            print_error("No Android devices connected.")
            return

        print_step("Installing to device...")
        android_dir = os.path.join(cwd, "android")
        apk_path = os.path.join(android_dir, "app/build/outputs/apk/debug/app-debug.apk")
        
        if not os.path.exists(apk_path):
            print_error("APK not found. Please run 'aroma build android' first.")
            return
            
        res = run_command(["adb", "install", "-r", apk_path])
        if res is not None and res.returncode == 0:
            print_success("Installed.")
            package_name = None
            build_gradle = os.path.join(android_dir, "app/build.gradle")
            
            if os.path.exists(build_gradle):
                with open(build_gradle, 'r') as f:
                    for line in f:
                        if "namespace" in line or "applicationId" in line:
                             parts = line.split()
                             for p in parts:
                                 clean_p = p.strip("'\"")
                                 if "." in clean_p and not clean_p.startswith("com.android"):
                                     package_name = clean_p
                                     break
                        if package_name: break

            if not package_name:
                print_info("Could not auto-detect package name. Please launch manually.")
                return

            print_step(f"Launching {package_name}...")
            run_command(["adb", "shell", "am", "start", "-n", f"{package_name}/android.app.NativeActivity"])
        else:
            print_error("Installation failed.")

def main():
    parser = argparse.ArgumentParser(prog="aroma", description="AromaUI CLI Tool")
    subparsers = parser.add_subparsers(dest="command", required=True)
    
    subparsers.add_parser("doctor", help="Check environment")
    
    install_p = subparsers.add_parser("install-sdk", help="Install Android SDK & NDK")
    
    create_p = subparsers.add_parser("create", help="Create new project")
    create_p.add_argument("name", nargs="?", help="Project name")
    
    build_p = subparsers.add_parser("build", help="Build project")
    build_p.add_argument("platform", choices=["linux", "android"], default="linux", nargs="?")
    
    run_p = subparsers.add_parser("run", help="Run project")
    run_p.add_argument("platform", choices=["linux", "android"], default="linux", nargs="?")
    
    if len(sys.argv) == 1:
        parser.print_help()
        sys.exit(1)

    args = parser.parse_args()
    
    if args.command == "doctor":
        cmd_doctor(args)
    elif args.command == "install-sdk":
        install_android_sdk()
    elif args.command == "create":
        cmd_create(args)
    elif args.command == "build":
        cmd_build(args)
    elif args.command == "run":
        cmd_run(args)

if __name__ == "__main__":
    main()
