#!/usr/bin/env python3
import argparse
import os
import platform
import re
import shutil
import ssl
import subprocess
import sys
import time
import urllib.request
import zipfile
from typing import Any, Dict, List, Optional, Tuple


class Config:
    ANDROID_SDK_VERSION = "34"
    NDK_VERSION = "25.1.8937393"
    CMAKE_VERSION = "3.22.1"
    CMDLINE_TOOLS_VERSION = "10406996"

    SDK_PACKAGES = [
        "platform-tools",
        f"platforms;android-{ANDROID_SDK_VERSION}",
        f"build-tools;{ANDROID_SDK_VERSION}.0.0",
        f"ndk;{NDK_VERSION}",
        f"cmake;{CMAKE_VERSION}",
    ]

    COMMON_SDK_PATHS = [
        os.path.expanduser("~/Android/Sdk"),
        "/usr/lib/android-sdk",
        "/Library/Android/sdk",
    ]


class Colors:
    HEADER = "\033[95m"
    OKBLUE = "\033[94m"
    OKCYAN = "\033[96m"
    OKGREEN = "\033[92m"
    WARNING = "\033[93m"
    FAIL = "\033[91m"
    ENDC = "\033[0m"
    BOLD = "\033[1m"
    UNDERLINE = "\033[4m"


def print_step(msg: str) -> None:
    print(f"{Colors.OKBLUE}==>{Colors.ENDC} {Colors.BOLD}{msg}{Colors.ENDC}")


def print_success(msg: str) -> None:
    print(f"{Colors.OKGREEN}✓ {msg}{Colors.ENDC}")


def print_error(msg: str) -> None:
    print(f"{Colors.FAIL}✗ {msg}{Colors.ENDC}")


def print_info(msg: str) -> None:
    print(f"{Colors.OKCYAN}i {msg}{Colors.ENDC}")


def print_warning(msg: str) -> None:
    print(f"{Colors.WARNING}⚠ {msg}{Colors.ENDC}")


def run_command(
    cmd: List[str],
    cwd: str = None,
    env: Dict = None,
    capture_output: bool = False,
    check: bool = False,
) -> Optional[subprocess.CompletedProcess]:
    try:
        if capture_output:
            return subprocess.run(
                cmd,
                cwd=cwd,
                env=env,
                check=check,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
        return subprocess.run(cmd, cwd=cwd, env=env, check=check)
    except (subprocess.CalledProcessError, FileNotFoundError):
        if capture_output:
            print_error(f"Command failed: {' '.join(cmd)}")
        return None


def get_input(
    prompt: str, default: str = None, validator=None, error_msg: str = "Invalid input"
) -> str:
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


def validate_package_name(name: str) -> bool:
    return bool(re.match(r"^[a-z][a-z0-9_]*(\.[a-z0-9_]+)+$", name))


def validate_int(val: str) -> bool:
    try:
        int(val)
        return True
    except ValueError:
        return False


def find_aroma_root() -> str:
    script_path = os.path.realpath(__file__)
    return os.path.abspath(os.path.join(os.path.dirname(script_path), "../../"))


AROMA_ROOT = find_aroma_root()


class AndroidSDK:
    def __init__(self):
        self.sdk_root = os.environ.get(
            "ANDROID_HOME", os.path.expanduser("~/Android/Sdk")
        )
        self.cmdline_tools = os.path.join(self.sdk_root, "cmdline-tools", "latest")
        self.sdkmanager = os.path.join(self.cmdline_tools, "bin", "sdkmanager")
        self.avdmanager = os.path.join(self.cmdline_tools, "bin", "avdmanager")

    def is_installed(self) -> bool:
        return os.path.exists(self.sdkmanager)

    def find_existing(self) -> Optional[str]:
        for path in Config.COMMON_SDK_PATHS:
            if os.path.exists(path):
                self.sdk_root = path
                self.cmdline_tools = os.path.join(path, "cmdline-tools", "latest")
                self.sdkmanager = os.path.join(self.cmdline_tools, "bin", "sdkmanager")
                if os.path.exists(self.sdkmanager):
                    return path
        return None

    def install(self) -> bool:
        print_step(f"Setting up Android SDK at {self.sdk_root}...")

        if not self._install_cmdline_tools():
            return False

        if not self._check_java():
            return False

        self._accept_licenses()

        if not self._install_packages():
            return False

        print_success("Android SDK & NDK installed successfully!")
        return True

    def _install_cmdline_tools(self) -> bool:
        if os.path.exists(self.sdkmanager):
            return True

        os.makedirs(os.path.dirname(self.cmdline_tools), exist_ok=True)

        system = platform.system()
        urls = {
            "Linux": f"https://dl.google.com/android/repository/commandlinetools-linux-{Config.CMDLINE_TOOLS_VERSION}_latest.zip",
            "Darwin": f"https://dl.google.com/android/repository/commandlinetools-mac-{Config.CMDLINE_TOOLS_VERSION}_latest.zip",
        }

        if system not in urls:
            print_error("Auto-install only supported on Linux/Mac currently.")
            return False

        url = urls[system]
        zip_path = os.path.join(self.sdk_root, "cmdline-tools.zip")

        print_info(f"Downloading Command Line Tools...")
        try:
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE

            with (
                urllib.request.urlopen(url, context=ctx) as r,
                open(zip_path, "wb") as f,
            ):
                shutil.copyfileobj(r, f)
        except Exception as e:
            print_error(f"Download failed: {e}")
            return False

        print_info("Extracting...")
        try:
            with zipfile.ZipFile(zip_path, "r") as zip_ref:
                zip_ref.extractall(os.path.dirname(self.cmdline_tools))
            os.remove(zip_path)

            extracted = os.path.join(
                os.path.dirname(self.cmdline_tools), "cmdline-tools"
            )
            if os.path.exists(extracted):
                shutil.move(extracted, self.cmdline_tools)
            os.chmod(self.sdkmanager, 0o755)
            return True
        except Exception as e:
            print_error(f"Extraction failed: {e}")
            return False

    def _check_java(self) -> bool:
        if run_command(["which", "java"], capture_output=True) or os.environ.get(
            "JAVA_HOME"
        ):
            return True
        print_error("Java is required but not found. Please install OpenJDK.")
        return False

    def _accept_licenses(self) -> None:
        try:
            cmd = (
                ["bash", self.sdkmanager, "--licenses"]
                if platform.system() == "Linux"
                else [self.sdkmanager, "--licenses"]
            )
            p = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            p.communicate(input=b"y\n" * 50)
        except Exception:
            pass

    def _install_packages(self) -> bool:
        print_info(f"Installing: {', '.join(Config.SDK_PACKAGES)}")

        cmd_prefix = (
            ["bash", self.sdkmanager]
            if platform.system() == "Linux"
            else [self.sdkmanager]
        )
        res = run_command(cmd_prefix + Config.SDK_PACKAGES)

        return res is not None and res.returncode == 0

    def get_ndk_path(self) -> Optional[str]:
        ndk_root = os.path.join(self.sdk_root, "ndk")
        if not os.path.exists(ndk_root):
            return None

        versions = [
            d for d in os.listdir(ndk_root) if os.path.isdir(os.path.join(ndk_root, d))
        ]
        if not versions:
            return None

        return os.path.join(ndk_root, sorted(versions)[-1])


class KeystoreManager:
    def __init__(self, project_dir: str):
        self.project_dir = project_dir
        self.android_dir = os.path.join(project_dir, "android")
        self.keystore_props = os.path.join(self.android_dir, "keystore.properties")
        self.keystore_dir = os.path.join(self.android_dir, "keystore")

    def has_keystore(self) -> bool:
        return os.path.exists(self.keystore_props) and os.path.exists(
            os.path.join(self.keystore_dir, "release.keystore")
        )

    def verify_password(self, keystore_file: str, password: str) -> bool:
        cmd = [
            "keytool",
            "-list",
            "-keystore",
            keystore_file,
            "-storepass",
            password,
        ]
        result = run_command(cmd, capture_output=True)
        return result is not None and result.returncode == 0

    def create_or_update(self) -> Optional[Dict[str, str]]:
        print_step("Setting up release signing keystore")

        os.makedirs(self.keystore_dir, exist_ok=True)
        keystore_file = os.path.join(self.keystore_dir, "release.keystore")

        if os.path.exists(keystore_file):
            os.remove(keystore_file)

        print_info("Creating new keystore")
        alias = get_input("Key alias", default="release")

        print_info("Choose a password you'll remember")
        storepass = get_input(
            "Keystore password",
            validator=lambda x: len(x) >= 4,
            error_msg="Password must be at least 4 characters",
        )

        confirm = get_input("Confirm password")
        if confirm != storepass:
            print_error("Passwords don't match")
            return None

        use_same = get_input(
            "Use same password for key?",
            default="Y",
            validator=lambda x: x.lower() in ["y", "n"],
        )

        if use_same.lower() == "y":
            keypass = storepass
        else:
            keypass = get_input("Key password", validator=lambda x: len(x) >= 4)
            confirm_key = get_input("Confirm key password")
            if confirm_key != keypass:
                print_error("Key passwords don't match")
                return None

        print_info("Enter certificate details")
        name = get_input("First and Last Name", default="Developer")
        org_unit = get_input("Organizational Unit", default="Development")
        org = get_input("Organization", default="Personal")
        city = get_input("City", default="")
        state = get_input("State", default="")
        country = get_input(
            "Country Code (2 letters)", default="US", validator=lambda x: len(x) == 2
        )

        dn_parts = []
        if name:
            dn_parts.append(f"CN={name}")
        if org_unit:
            dn_parts.append(f"OU={org_unit}")
        if org:
            dn_parts.append(f"O={org}")
        if city:
            dn_parts.append(f"L={city}")
        if state:
            dn_parts.append(f"ST={state}")
        if country:
            dn_parts.append(f"C={country}")
        dname = ",".join(dn_parts)

        cmd = [
            "keytool",
            "-genkey",
            "-v",
            "-keystore",
            keystore_file,
            "-alias",
            alias,
            "-keyalg",
            "RSA",
            "-keysize",
            "2048",
            "-validity",
            "10000",
            "-dname",
            dname,
            "-storepass",
            storepass,
            "-keypass",
            keypass,
        ]
        if not run_command(cmd):
            print_error("Failed to create keystore")
            return None

        props = {
            "storeFile": keystore_file,
            "storePassword": storepass,
            "keyPassword": keypass,
            "keyAlias": alias,
        }

        with open(self.keystore_props, "w") as f:
            for key, value in props.items():
                f.write(f"{key}={value}\n")

        pass_file = os.path.join(self.keystore_dir, "passwords.txt")
        with open(pass_file, "w") as f:
            f.write(f"# KEEP THIS FILE SAFE\n")
            for key, value in props.items():
                f.write(f"{key}={value}\n")

        print_success(f"Keystore created at: {keystore_file}")
        print_warning(f"Passwords saved to: {pass_file}")

        return props

    def get_signing_config(self) -> Optional[Dict[str, str]]:
        if not os.path.exists(self.keystore_props):
            return None

        config = {}
        with open(self.keystore_props, "r") as f:
            for line in f:
                if "=" in line:
                    key, value = line.strip().split("=", 1)
                    config[key] = value
        return config if config else None


class ProjectCreator:
    def __init__(self, templates_dir: str):
        self.templates_dir = templates_dir

    def create(self, args) -> bool:
        print_step("Configure New Project")

        project_name = args.name or get_input(
            "Project Name", validator=lambda x: len(x) > 0
        )
        target_dir = os.path.abspath(project_name)

        if os.path.exists(target_dir):
            print_error(f"Directory '{project_name}' already exists.")
            return False

        default_pkg = f"com.example.{project_name.lower().replace('-', '_')}"
        package = get_input(
            "Android Package Name", default=default_pkg, validator=validate_package_name
        )
        min_sdk = get_input("Min Android SDK", default="24", validator=validate_int)
        target_sdk = get_input(
            "Target Android SDK",
            default=Config.ANDROID_SDK_VERSION,
            validator=validate_int,
        )
        compile_sdk = get_input(
            "Compile Android SDK",
            default=Config.ANDROID_SDK_VERSION,
            validator=validate_int,
        )

        print("\n" + Colors.BOLD + "Configuration Summary:" + Colors.ENDC)
        print(f"  Name:        {project_name}")
        print(f"  Package:     {package}")
        print(f"  Min SDK:     {min_sdk}")
        print(f"  Target SDK:  {target_sdk}")
        print(f"  Compile SDK: {compile_sdk}")

        if (
            not get_input(
                "\nCreate Project?",
                default="Y",
                validator=lambda x: x.lower() in ["y", "n"],
            )
            .lower()
            .startswith("y")
        ):
            print("Aborted.")
            return False

        return self._generate_project(
            target_dir, project_name, package, min_sdk, target_sdk, compile_sdk
        )

    def _generate_project(
        self,
        target_dir: str,
        name: str,
        package: str,
        min_sdk: str,
        target_sdk: str,
        compile_sdk: str,
    ) -> bool:
        print_step(f"Creating project {name}...")

        try:
            os.makedirs(target_dir)
            os.makedirs(os.path.join(target_dir, "src"))

            replacements = {
                "{{PROJECT_NAME}}": name,
                "{{PACKAGE_NAME}}": package,
                "{{MIN_SDK}}": min_sdk,
                "{{TARGET_SDK}}": target_sdk,
                "{{COMPILE_SDK}}": compile_sdk,
                "{{AROMA_ROOT}}": AROMA_ROOT,
            }

            self._copy_template(
                "app/main.c.tpl", os.path.join(target_dir, "src/main.c"), replacements
            )
         
            self._copy_template(
                "app/CMakeLists.txt.tpl",
                os.path.join(target_dir, "CMakeLists.txt"),
                replacements,
            )

            android_src = os.path.join(self.templates_dir, "android")
            android_dst = os.path.join(target_dir, "android")

            if os.path.exists(android_src):
                shutil.copytree(android_src, android_dst)
                self._process_android_templates(android_dst, replacements)
                self._setup_java_package(android_dst, package, replacements)
                self._create_local_properties(android_dst)

            print_success(f"Project '{name}' created successfully!")
            self._show_next_steps(name)
            return True

        except Exception as e:
            print_error(f"Failed to create project: {e}")
            return False

    def _copy_template(self, src_rel: str, dst: str, replacements: Dict) -> None:
        src = os.path.join(self.templates_dir, src_rel)
        if os.path.exists(src):
            with open(src, "r") as f:
                content = f.read()
            for k, v in replacements.items():
                content = content.replace(k, str(v))
            with open(dst, "w") as f:
                f.write(content)

    def _process_android_templates(self, android_dir: str, replacements: Dict) -> None:
        for root, _, files in os.walk(android_dir):
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

    def _setup_java_package(
        self, android_dir: str, package: str, replacements: Dict
    ) -> None:
        helper_tpl = os.path.join(android_dir, "app/src/main/java/AromaHelper.java.tpl")
        if not os.path.exists(helper_tpl):
            return

        package_path = package.replace(".", os.sep)
        java_dir = os.path.join(android_dir, "app/src/main/java")
        final_dir = os.path.join(java_dir, package_path)
        os.makedirs(final_dir, exist_ok=True)

        with open(helper_tpl, "r") as f:
            content = f.read()
        for k, v in replacements.items():
            content = content.replace(k, str(v))

        with open(os.path.join(final_dir, "AromaHelper.java"), "w") as f:
            f.write(content)

        os.remove(helper_tpl)

        self._remove_empty_dirs(os.path.join(java_dir, "com"))

    def _remove_empty_dirs(self, path: str) -> None:
        if not os.path.exists(path):
            return
        for root, dirs, files in os.walk(path, topdown=False):
            for dir in dirs:
                dir_path = os.path.join(root, dir)
                if not os.listdir(dir_path):
                    os.rmdir(dir_path)

    def _create_local_properties(self, android_dir: str) -> None:
        sdk = AndroidSDK()
        if sdk.find_existing():
            with open(os.path.join(android_dir, "local.properties"), "w") as f:
                f.write(f"sdk.dir={sdk.sdk_root}\n")
                ndk = sdk.get_ndk_path()
                if ndk:
                    f.write(f"ndk.dir={ndk}\n")

    def _show_next_steps(self, name: str) -> None:
        print(f"\n{Colors.BOLD}Next steps:{Colors.ENDC}")
        print(f"  cd {name}")
        print(f"  aroma run linux")
        print(f"  aroma run android")
        print(f"  aroma build android --release")
        print(f"  aroma build android --release --aab")
        print(f"  aroma sign")


class BuildSystem:
    def __init__(self, project_dir: str):
        self.project_dir = project_dir
        self.android_dir = os.path.join(project_dir, "android")

    def build_linux(self) -> bool:
        build_dir = os.path.join(self.project_dir, "build")
        os.makedirs(build_dir, exist_ok=True)

        if not run_command(["cmake", ".."], cwd=build_dir):
            print_error("CMake configuration failed")
            return False

        if not run_command(["make", "-j4"], cwd=build_dir):
            print_error("Build failed")
            return False

        print_success("Linux build successful!")
        return True

    def build_android(self, release: bool = False, aab: bool = False) -> bool:
        if not os.path.exists(self.android_dir):
            print_error("No 'android' directory found. Is this an Aroma project?")
            return False

        sdk = AndroidSDK()
        if not self._ensure_sdk(sdk):
            return False

        if release:
            keystore = KeystoreManager(self.project_dir)
            if not keystore.has_keystore():
                print_info("Release builds require a signing keystore")
                if (
                    not get_input(
                        "Set up signing now?",
                        default="Y",
                        validator=lambda x: x.lower() in ["y", "n"],
                    )
                    .lower()
                    .startswith("y")
                ):
                    print_warning("Cannot build release without signing")
                    return False
                if not keystore.create_or_update():
                    return False

        return self._run_gradle_build(release, aab)

    def _ensure_sdk(self, sdk: AndroidSDK) -> bool:
        sdk_path = sdk.find_existing()
        if not sdk_path:
            print_info("Android SDK not found.")
            if (
                not get_input(
                    "Install Android SDK?",
                    default="Y",
                    validator=lambda x: x.lower() in ["y", "n"],
                )
                .lower()
                .startswith("y")
            ):
                return False
            if not sdk.install():
                return False
            sdk_path = sdk.sdk_root

        os.environ["ANDROID_HOME"] = sdk_path

        local_prop = os.path.join(self.android_dir, "local.properties")
        with open(local_prop, "w") as f:
            f.write(f"sdk.dir={sdk_path}\n")
            ndk = sdk.get_ndk_path()
            if ndk:
                f.write(f"ndk.dir={ndk}\n")
                os.environ["ANDROID_NDK_HOME"] = ndk

        return True

    def _run_gradle_build(self, release: bool, aab: bool) -> bool:
        gradlew = os.path.join(self.android_dir, "gradlew")
        if os.path.exists(gradlew):
            os.chmod(gradlew, 0o755)
            cmd_prefix = [gradlew]
        else:
            print_info("gradlew not found, using system gradle")
            cmd_prefix = ["gradle"]

        if release:
            target = "bundleRelease" if aab else "assembleRelease"
            output_type = "AAB" if aab else "APK"
        else:
            target = "assembleDebug"
            output_type = "debug APK"

        print_info(f"Running: {' '.join(cmd_prefix + [target])}")

        if not run_command(cmd_prefix + [target], cwd=self.android_dir):
            print_error(f"Android build failed")
            return False

        self._show_output_path(release, aab)
        return True

    def _show_output_path(self, release: bool, aab: bool) -> None:
        base = os.path.join(self.android_dir, "app/build/outputs")

        if release:
            if aab:
                paths = [
                    os.path.join(base, "bundle/release/app-release.aab"),
                    os.path.join(base, "bundle/release/app.aab"),
                ]
                file_type = "AAB"
            else:
                paths = [
                    os.path.join(base, "apk/release/app-release.apk"),
                    os.path.join(base, "apk/release/app-release-unsigned.apk"),
                ]
                file_type = "APK"
        else:
            paths = [os.path.join(base, "apk/debug/app-debug.apk")]
            file_type = "debug APK"

        for path in paths:
            if os.path.exists(path):
                if "unsigned" in path:
                    print_warning(f"Unsigned {file_type} built at: {path}")
                    print_warning(
                        "This file is NOT signed and cannot be installed directly!"
                    )
                else:
                    print_success(f"{file_type} built at: {path}")
                return

        print_error("Build finished but output file not found.")


def cmd_doctor(args):
    print_step("Running Aroma Doctor...")

    print(f"OS: {platform.system()} {platform.release()}")

    tools = [
        ("CMake", ["cmake", "--version"]),
        ("Ninja", ["ninja", "--version"]),
        ("GCC", ["gcc", "--version"]),
        ("Java", ["java", "-version"]),
        ("keytool", ["keytool", "-help"]),
    ]

    for name, cmd in tools:
        result = run_command(cmd, capture_output=True)
        if result:
            if name == "Ninja":
                print_success(f"{name}: {result.stdout.strip()}")
            else:
                print_success(f"{name} installed")
        else:
            if name == "Ninja":
                print_info(f"{name} not found (recommended for fast builds)")
            else:
                print_warning(f"{name} not found")

    sdk = AndroidSDK()
    sdk_path = sdk.find_existing()

    if sdk_path:
        print_success(f"Android SDK: {sdk_path}")
        ndk = sdk.get_ndk_path()
        if ndk:
            print_success(f"Android NDK: {ndk}")
        else:
            print_warning("Android NDK not found")
            if (
                get_input(
                    "Install NDK?",
                    default="Y",
                    validator=lambda x: x.lower() in ["y", "n"],
                ).lower()
                == "y"
            ):
                sdk.install()
    else:
        print_warning("Android SDK not found")
        if (
            get_input(
                "Install Android SDK?",
                default="Y",
                validator=lambda x: x.lower() in ["y", "n"],
            ).lower()
            == "y"
        ):
            sdk.install()

    print_step("Doctor summary complete.")


def cmd_create(args):
    templates_dir = os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "templates"
    )
    creator = ProjectCreator(templates_dir)
    creator.create(args)


def cmd_build(args):
    print_step(f"Building for {args.platform}...")

    build_system = BuildSystem(os.getcwd())

    if args.platform == "linux":
        build_system.build_linux()
    elif args.platform == "android":
        build_system.build_android(args.release, args.aab)


def cmd_run(args):
    if args.platform == "android" and args.release:
        print_warning("Release builds are for distribution, not for running.")
        print_info("Use 'aroma run android' without --release for testing.")
        return

    build_system = BuildSystem(os.getcwd())
    if args.platform == "linux":
        if not build_system.build_linux():
            return
    elif args.platform == "android":
        if not build_system.build_android(False, False):
            return

    cwd = os.getcwd()

    if args.platform == "linux":
        _run_linux(cwd)
    elif args.platform == "android":
        _run_android(cwd, args.emu)


def _run_linux(cwd: str) -> None:
    project_name = os.path.basename(cwd)
    exe_path = os.path.join(cwd, "build", project_name)

    if not os.path.exists(exe_path):
        build_dir = os.path.join(cwd, "build")
        if os.path.exists(build_dir):
            files = [
                f
                for f in os.listdir(build_dir)
                if os.access(os.path.join(build_dir, f), os.X_OK)
                and not f.endswith(".so")
                and not os.path.isdir(os.path.join(build_dir, f))
                and f not in ["Makefile", "cmake_install.cmake"]
            ]
            if files:
                exe_path = os.path.join(build_dir, files[0])

    if os.path.exists(exe_path):
        print_step(f"Launching {exe_path}...")
        subprocess.run([exe_path])
    else:
        print_error("Executable not found. Build may have failed.")


def _run_android(cwd: str, use_emu: bool) -> None:
    if use_emu and not _start_emulator():
        return

    result = run_command(["adb", "devices"], capture_output=True)
    if (
        not result
        or "device" not in result.stdout.replace("List of devices attached", "").strip()
    ):
        print_error("No Android devices connected.")
        return

    apk_path = os.path.join(cwd, "android/app/build/outputs/apk/debug/app-debug.apk")
    if not os.path.exists(apk_path):
        print_error("APK not found. Build may have failed.")
        return

    print_step("Installing to device...")
    if not run_command(["adb", "install", "-r", apk_path]):
        print_error("Installation failed.")
        return

    print_success("Installed.")

    package_name = _find_package_name(cwd)
    if not package_name:
        print_info("Could not detect package name. Please launch manually.")
        return

    print_step(f"Launching {package_name}...")
    run_command(
        [
            "adb",
            "shell",
            "am",
            "start",
            "-n",
            f"{package_name}/android.app.NativeActivity",
        ]
    )


def _find_package_name(cwd: str) -> Optional[str]:
    build_gradle = os.path.join(cwd, "android/app/build.gradle")
    if not os.path.exists(build_gradle):
        return None

    with open(build_gradle, "r") as f:
        for line in f:
            if "namespace" in line or "applicationId" in line:
                parts = line.split()
                for p in parts:
                    clean = p.strip("'\"")
                    if "." in clean and not clean.startswith("com.android"):
                        return clean
    return None


def _start_emulator() -> bool:
    sdk = AndroidSDK()
    if not sdk.find_existing():
        print_error("Android SDK not found.")
        return False

    emulator = os.path.join(sdk.sdk_root, "emulator/emulator")
    if not os.path.exists(emulator):
        print_error("Emulator not installed.")
        return False

    avd_name = "aroma_emu"

    result = run_command(["adb", "devices"], capture_output=True)
    if result and "emulator-" in result.stdout:
        print_info("Emulator already running.")
        return True

    avdmanager = os.path.join(sdk.cmdline_tools, "bin/avdmanager")
    result = run_command([avdmanager, "list", "avd", "-c"], capture_output=True)

    if avd_name not in (result.stdout.strip().splitlines() if result else []):
        print_step(f"Creating AVD '{avd_name}'...")
        img = f"system-images;android-{Config.ANDROID_SDK_VERSION};google_apis;{platform.machine()}"
        cmd = [avdmanager, "create", "avd", "-n", avd_name, "-k", img, "--force"]
        p = subprocess.Popen(
            cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        p.communicate(input=b"no\n")
        if p.returncode != 0:
            print_error("Failed to create AVD")
            return False

    print_step(f"Starting emulator...")
    log = open("emulator.log", "w")
    subprocess.Popen(
        [emulator, "-avd", avd_name, "-no-snapshot-load"], stdout=log, stderr=log
    )

    print_info("Waiting for device...")
    run_command(["adb", "wait-for-device"])

    print_info("Waiting for boot...")
    while True:
        result = run_command(
            ["adb", "shell", "getprop", "sys.boot_completed"], capture_output=True
        )
        if result and result.stdout.strip() == "1":
            break
        time.sleep(2)

    print_success("Emulator ready.")
    return True


def _add_signing_config_to_gradle(project_dir: str) -> bool:
    build_gradle = os.path.join(project_dir, "android/app/build.gradle")
    
    if not os.path.exists(build_gradle):
        print_error("app/build.gradle not found")
        return False
    
    with open(build_gradle, 'r') as f:
        content = f.read()
    
    if 'signingConfigs {' in content and 'release {' in content:
        print_info("Signing configuration already exists in build.gradle")
        return True
    
    signing_config = '''
    signingConfigs {
        release {
            def keystorePropertiesFile = rootProject.file("keystore.properties")
            if (keystorePropertiesFile.exists()) {
                def keystoreProperties = new Properties()
                keystoreProperties.load(new FileInputStream(keystorePropertiesFile))

                storeFile file(keystoreProperties['storeFile'])
                storePassword keystoreProperties['storePassword']
                keyAlias keystoreProperties['keyAlias']
                keyPassword keystoreProperties['keyPassword']
            }
        }
    }
'''
    
    # Insert signingConfigs after android { line
    android_start_pattern = r'(android\s*{)'
    match = re.search(android_start_pattern, content)
    
    if match:
        insert_pos = match.end()
        new_content = content[:insert_pos] + signing_config + content[insert_pos:]
    else:
        print_error("Could not find android block in build.gradle")
        return False
    
    # Add release buildType
    release_block = '''
        release {
            signingConfig signingConfigs.release
            minifyEnabled false
            proguardFiles getDefaultProguardFile('proguard-android-optimize.txt'), 'proguard-rules.pro'
        }
'''
    
    # Check if buildTypes exists
    build_types_pattern = r'(buildTypes\s*{.*?})'
    build_types_match = re.search(build_types_pattern, new_content, re.DOTALL)
    
    if build_types_match:
        # buildTypes exists, add release before the closing }
        build_types_content = build_types_match.group(1)
        # Check if release already exists
        if 'release {' not in build_types_content:
            # Insert release before the last }
            modified_build_types = build_types_content.rstrip('}') + release_block + '    }'
            new_content = new_content.replace(build_types_content, modified_build_types)
    else:
        # buildTypes doesn't exist, create it after defaultConfig
        default_config_pattern = r'(defaultConfig\s*{.*?}\s*\n)'
        default_config_match = re.search(default_config_pattern, new_content, re.DOTALL)
        if default_config_match:
            insert_pos = default_config_match.end()
            build_types_section = f'''
    buildTypes {{
        debug {{
            debuggable true
        }}{release_block}
    }}
'''
            new_content = new_content[:insert_pos] + build_types_section + new_content[insert_pos:]
        else:
            print_error("Could not find defaultConfig block in build.gradle")
            return False
    
    try:
        with open(build_gradle, 'w') as f:
            f.write(new_content)
        print_success("Updated app/build.gradle with signing configuration")
        return True
    except Exception as e:
        print_error(f"Failed to write build.gradle: {e}")
        return False


def cmd_sign(args):
    keystore = KeystoreManager(os.getcwd())

    if keystore.create_or_update():
        if _add_signing_config_to_gradle(os.getcwd()):
            print_success("Signing configured successfully!")
            print_info("You can now build release APKs and AABs:")
            print_info("  aroma build android --release")
            print_info("  aroma build android --release --aab")
        else:
            print_warning("Signing keystore created but couldn't update build.gradle")
    else:
        print_error("Failed to setup signing.")


def main():
    parser = argparse.ArgumentParser(
        prog="aroma",
        description="AromaUI CLI Tool - Build and manage AromaUI projects",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  aroma doctor
  aroma create myapp
  aroma build linux
  aroma build android
  aroma build android --release
  aroma build android --release --aab
  aroma sign
  aroma run linux
  aroma run android
  aroma run android --emu
        """,
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("doctor", help="Check environment and dependencies")
    subparsers.add_parser("install-sdk", help="Install Android SDK & NDK")

    create_p = subparsers.add_parser("create", help="Create a new project")
    create_p.add_argument("name", nargs="?", help="Project name")

    build_p = subparsers.add_parser("build", help="Build the project")
    build_p.add_argument(
        "platform", choices=["linux", "android"], default="linux", nargs="?"
    )
    build_p.add_argument("--release", action="store_true", help="Build release version")
    build_p.add_argument("--aab", action="store_true", help="Build Android App Bundle")

    run_p = subparsers.add_parser("run", help="Run the project")
    run_p.add_argument(
        "platform", choices=["linux", "android"], default="linux", nargs="?"
    )
    run_p.add_argument("--emu", action="store_true", help="Run in emulator")
    run_p.add_argument("--release", action="store_true", help=argparse.SUPPRESS)

    sign_p = subparsers.add_parser("sign", help="Setup release signing keystore")

    if len(sys.argv) == 1:
        parser.print_help()
        sys.exit(1)

    args = parser.parse_args()

    commands = {
        "doctor": cmd_doctor,
        "install-sdk": lambda a: AndroidSDK().install(),
        "create": cmd_create,
        "build": cmd_build,
        "run": cmd_run,
        "sign": cmd_sign,
    }

    if args.command in commands:
        commands[args.command](args)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()