import argparse
import getpass
import json
import os
import platform
import re
import shutil
import ssl
import stat
import subprocess
import sys
import time
import urllib.request
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Tuple, Union
from urllib.error import URLError, HTTPError

try:
    from rich.console import Console
    from rich.panel import Panel
    from rich.table import Table
    from rich.progress import (
        Progress, SpinnerColumn, TextColumn, BarColumn,
        DownloadColumn, TransferSpeedColumn, TimeRemainingColumn,
    )
    from rich.prompt import Prompt, Confirm
    from rich.live import Live
    from rich import box
    RICH_AVAILABLE = True
except ImportError:
    RICH_AVAILABLE = False


GRADLE_JAVA_MATRIX = [
    ("8.12", 17, 24),
    ("8.11", 17, 24),
    ("8.10", 17, 24),
    ("8.9", 17, 24),
    ("8.8", 17, 23),
    ("8.7", 17, 23),
    ("8.6", 17, 22),
    ("8.5", 17, 22),
    ("8.4", 17, 21),
    ("8.3", 17, 20),
    ("8.2", 17, 20),
    ("8.1", 17, 20),
    ("8.0", 17, 20),
    ("7.6", 11, 20),
]

GRADLE_VALIDATE_URL_MIN_VERSION = (8, 8)

GRADLE_DOWNLOAD_URL = "https://services.gradle.org/distributions/gradle-{version}-bin.zip"

ANDROID_CMDLINE_TOOLS_URLS = {
    "Linux": "https://dl.google.com/android/repository/commandlinetools-linux-{version}_latest.zip",
    "Darwin": "https://dl.google.com/android/repository/commandlinetools-mac-{version}_latest.zip",
    "Windows": "https://dl.google.com/android/repository/commandlinetools-win-{version}_latest.zip",
}

ANDROID_PLATFORM_TOOLS_URLS = {
    "Linux": "https://dl.google.com/android/repository/platform-tools-latest-linux.zip",
    "Darwin": "https://dl.google.com/android/repository/platform-tools-latest-darwin.zip",
    "Windows": "https://dl.google.com/android/repository/platform-tools-latest-windows.zip",
}

JAVA_SEARCH_DIRS = [
    "/usr/lib/jvm",
    "/Library/Java/JavaVirtualMachines",
]

SDK_SEARCH_PATHS = [
    "~/Android/Sdk",
    "/usr/lib/android-sdk",
    "/Library/Android/sdk",
    "C:\\Android\\Sdk",
]

AUTO_YES = False


def host_arch_tag() -> str:
    machine = platform.machine().lower()
    if machine in ("arm64", "aarch64"):
        return "arm64-v8a"
    return "x86_64"


def load_config(config_path: Optional[str] = None) -> Dict[str, Any]:
    paths = []
    if config_path:
        paths.append(config_path)
    paths.append(os.path.join(os.getcwd(), "aroma.json"))
    paths.append(os.path.join(os.path.dirname(os.path.realpath(__file__)), "aroma.json"))
    paths.append(os.path.expanduser("~/.config/aroma/config.json"))

    for path in paths:
        if path and os.path.exists(path):
            try:
                with open(path, 'r') as f:
                    return json.load(f)
            except (json.JSONDecodeError, IOError) as e:
                Logger.debug(f"Failed to parse config {path}: {e}")

    return {}


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
    value: Any = config
    for k in keys:
        if isinstance(value, dict) and k in value:
            value = value[k]
        else:
            return default
    return value


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

    @classmethod
    def disable(cls):
        for attr in dir(cls):
            if attr.startswith('_'):
                continue
            val = getattr(cls, attr)
            if isinstance(val, str):
                setattr(cls, attr, '')


console: "Optional[Console]" = Console() if RICH_AVAILABLE else None


class Logger:
    _verbose = False

    @classmethod
    def setup(cls, verbose: bool = False):
        cls._verbose = verbose

    @classmethod
    def step(cls, msg: str):
        if console:
            console.print(f"[bold blue]==>[/] [bold]{msg}[/]")
        else:
            print(f"{Colors.OKBLUE}==>{Colors.ENDC} {Colors.BOLD}{msg}{Colors.ENDC}")

    @classmethod
    def success(cls, msg: str):
        if console:
            console.print(f"[bold green]OK[/] {msg}")
        else:
            print(f"{Colors.OKGREEN}OK {msg}{Colors.ENDC}")

    @classmethod
    def error(cls, msg: str):
        if console:
            console.print(f"[bold red]ERROR[/] {msg}", style="red")
        else:
            print(f"{Colors.FAIL}ERROR {msg}{Colors.ENDC}", file=sys.stderr)

    @classmethod
    def info(cls, msg: str):
        if not cls._verbose:
            return
        if console:
            console.print(f"[cyan]INFO[/] {msg}")
        else:
            print(f"{Colors.OKCYAN}INFO {msg}{Colors.ENDC}")

    @classmethod
    def warning(cls, msg: str):
        if console:
            console.print(f"[yellow]WARN[/] {msg}")
        else:
            print(f"{Colors.WARNING}WARN {msg}{Colors.ENDC}")

    @classmethod
    def debug(cls, msg: str):
        if not cls._verbose:
            return
        if console:
            console.print(f"[dim cyan]DEBUG {msg}[/]")
        else:
            print(f"{Colors.OKCYAN}DEBUG {msg}{Colors.ENDC}")


def secure_input(prompt: str, default: str = None, validator: Callable = None,
                  error_msg: str = "Invalid input", secret: bool = False,
                  min_length: int = 0) -> str:
    if AUTO_YES and default is not None:
        Logger.info(f"{prompt}: auto-accepting default [{default}] (--yes)")
        return default

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
            if default is not None:
                return default
            Logger.error(f"No input available for '{prompt}' and no default set. "
                         f"Pass --yes or provide AROMA_* env vars for non-interactive use.")
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


def confirm(prompt: str, default: bool = True) -> bool:
    default_str = "y" if default else "n"
    result = secure_input(prompt, default=default_str,
                          validator=lambda x: x.lower() in ['y', 'n'])
    return result.lower().startswith('y')


def validate_package_name(name: str) -> bool:
    return bool(re.match(r'^[a-z][a-z0-9_]*(\.[a-z0-9_]+)+$', name))


def validate_int(val: str) -> bool:
    try:
        int(val)
        return True
    except ValueError:
        return False


def validate_country_code(code: str) -> bool:
    return len(code) == 2 and code.isalpha() and code.isupper()


def escape_dn_value(value: str) -> str:
    if value is None:
        return ""
    special = re.sub(r'([,+"\\<>;=])', r'\\\1', value)
    special = re.sub(r'^(\s)', r'\\\1', special)
    special = re.sub(r'(\s)$', r'\\\1', special)
    if special.startswith('#'):
        special = '\\' + special
    return special


def run_command(cmd: List[str], cwd: str = None, env: Dict = None,
                capture_output: bool = False, check: bool = False,
                timeout: int = None) -> Optional[subprocess.CompletedProcess]:
    try:
        if capture_output:
            result = subprocess.run(
                cmd, cwd=cwd, env=env, check=check,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, timeout=timeout
            )
            return result
        result = subprocess.run(cmd, cwd=cwd, env=env, check=check, timeout=timeout)
        return result
    except subprocess.TimeoutExpired:
        Logger.error(f"Command timed out: {' '.join(cmd)}")
        return None
    except subprocess.CalledProcessError as e:
        Logger.debug(f"Command failed: {' '.join(cmd)}")
        if capture_output:
            return e
        return None
    except FileNotFoundError:
        Logger.debug(f"Command not found: {cmd[0]}")
        return None


def jvm_env(java_home: Optional[str] = None) -> Dict[str, str]:
    env = os.environ.copy()
    if java_home:
        env["JAVA_HOME"] = java_home
        return env
    if "JAVA_HOME" not in env:
        java_homes = find_installed_java()
        if java_homes:
            env["JAVA_HOME"] = os.path.dirname(os.path.dirname(java_homes[0][0]))
    return env


_path_warned = False


def warn_if_not_on_path(bin_dir: str):
    global _path_warned
    if _path_warned:
        return
    path_dirs = os.environ.get("PATH", "").split(os.pathsep)
    normalized = {os.path.normpath(p) for p in path_dirs if p}
    if os.path.normpath(bin_dir) not in normalized:
        _path_warned = True
        Logger.warning(f"{bin_dir} is not on your PATH.")
        shell = os.environ.get("SHELL", "")
        rc_file = "~/.bashrc"
        if "zsh" in shell:
            rc_file = "~/.zshrc"
        elif "fish" in shell:
            rc_file = "~/.config/fish/config.fish"
        Logger.info(f'  Add this to {rc_file}: export PATH="{bin_dir}:$PATH"')


def find_aroma_root() -> str:
    return os.path.abspath(os.path.join(os.path.dirname(os.path.realpath(__file__)), "..", ".."))


def safe_extract_zip(zip_path: str, extract_dir: str) -> bool:
    try:
        extract_dir = os.path.abspath(extract_dir)
        with zipfile.ZipFile(zip_path, 'r') as zf:
            for member in zf.infolist():
                member_path = os.path.abspath(os.path.join(extract_dir, member.filename))
                try:
                    common = os.path.commonpath([extract_dir, member_path])
                    if common != extract_dir:
                        Logger.error(f"Path traversal detected: {member.filename}")
                        return False
                except ValueError:
                    Logger.error(f"Path traversal detected: {member.filename}")
                    return False
            zf.extractall(extract_dir)
        return True
    except (zipfile.BadZipFile, IOError) as e:
        Logger.error(f"Extraction failed: {e}")
        return False


def is_valid_zip(zip_path: str) -> bool:
    if not os.path.exists(zip_path) or os.path.getsize(zip_path) == 0:
        return False
    try:
        with zipfile.ZipFile(zip_path, 'r') as zf:
            size = os.path.getsize(zip_path)
            if size < 50 * 1024 * 1024:
                bad = zf.testzip()
                return bad is None
            return len(zf.infolist()) > 0
    except (zipfile.BadZipFile, IOError, OSError):
        return False


def download_with_retry(url: str, dest: str, user_agent: str = "AromaUI-CLI/1.0",
                        attempts: int = 3, timeout: int = 300,
                        description: str = "Downloading") -> bool:
    ctx = ssl.create_default_context()
    ctx.check_hostname = True
    ctx.verify_mode = ssl.CERT_REQUIRED

    for attempt in range(attempts):
        try:
            Logger.info(f"{description} (attempt {attempt + 1}/{attempts}): {url}")
            req = urllib.request.Request(url)
            req.add_header('User-Agent', user_agent)
            with urllib.request.urlopen(req, context=ctx, timeout=timeout) as resp:
                if resp.status != 200:
                    raise HTTPError(url, resp.status, "Bad status", resp.headers, None)
                total = resp.headers.get("Content-Length")
                total = int(total) if total and total.isdigit() else None

                if console and total:
                    with Progress(
                        SpinnerColumn(),
                        TextColumn("[progress.description]{task.description}"),
                        BarColumn(),
                        DownloadColumn(),
                        TransferSpeedColumn(),
                        TimeRemainingColumn(),
                        console=console,
                        transient=True,
                    ) as progress:
                        task = progress.add_task(description, total=total)
                        with open(dest, 'wb') as f:
                            while True:
                                chunk = resp.read(65536)
                                if not chunk:
                                    break
                                f.write(chunk)
                                progress.update(task, advance=len(chunk))
                else:
                    with open(dest, 'wb') as f:
                        shutil.copyfileobj(resp, f)
            return True
        except (URLError, HTTPError, ssl.SSLError, IOError) as e:
            Logger.debug(f"Download failed: {e}")
            if os.path.exists(dest):
                try:
                    os.remove(dest)
                except OSError:
                    pass
            if attempt == attempts - 1:
                Logger.error(f"Download failed after {attempts} attempts: {url}")
                return False
            time.sleep(2 ** attempt)
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
        if major == 1:
            return (major, minor)
        return (major, 0)
    return None


def find_installed_java() -> List[Tuple[str, int]]:
    found: List[Tuple[str, int]] = []
    seen_paths = set()

    def add(java_bin: str):
        try:
            real = os.path.realpath(java_bin)
        except OSError:
            real = java_bin
        if real in seen_paths:
            return
        version = detect_java_version(java_bin)
        if version:
            seen_paths.add(real)
            found.append((java_bin, version[0]))

    for search_dir in JAVA_SEARCH_DIRS:
        if not os.path.isdir(search_dir):
            continue
        try:
            entries = os.listdir(search_dir)
        except OSError:
            continue
        for entry in entries:
            java_bin = os.path.join(search_dir, entry, "bin", "java")
            if os.path.isfile(java_bin):
                add(java_bin)

    sdkman_dir = os.path.expanduser("~/.sdkman/candidates/java")
    if os.path.isdir(sdkman_dir):
        for entry in os.listdir(sdkman_dir):
            java_bin = os.path.join(sdkman_dir, entry, "bin", "java")
            if os.path.isfile(java_bin):
                add(java_bin)

    system_java = shutil.which("java")
    if system_java:
        add(system_java)

    if "JAVA_HOME" in os.environ:
        java_bin = os.path.join(os.environ["JAVA_HOME"], "bin", "java")
        if os.path.isfile(java_bin):
            add(java_bin)

    return found


def find_best_gradle_for_java(java_version: int) -> Optional[str]:
    for gradle_ver, min_java, max_java in GRADLE_JAVA_MATRIX:
        if min_java <= java_version <= max_java:
            return gradle_ver
    return None


def find_java_for_gradle(gradle_version: str) -> Optional[str]:
    min_java = None
    max_java = None
    for gv, mn, mx in GRADLE_JAVA_MATRIX:
        if gv == gradle_version:
            min_java = mn
            max_java = mx
            break

    if min_java is None:
        return None

    installed = find_installed_java()
    for java_path, java_ver in installed:
        if min_java <= java_ver <= max_java:
            return os.path.dirname(os.path.dirname(java_path))

    return None


def find_gradle_installation() -> Optional[str]:
    gradle_path = shutil.which("gradle")
    if gradle_path:
        return gradle_path

    search_paths = [
        os.path.expanduser("~/.sdkman/candidates/gradle/current/bin/gradle"),
        "/opt/gradle/bin/gradle",
        "/usr/local/bin/gradle",
        os.path.expanduser("~/.local/bin/gradle"),
    ]

    for path in search_paths:
        if os.path.isfile(path):
            return path

    return None


class ADBManager:
    def __init__(self, sdk_root: str):
        self.sdk_root = sdk_root
        self.adb_path = os.path.join(sdk_root, "platform-tools", "adb")
        if platform.system() == "Windows":
            self.adb_path += ".exe"

    def is_installed(self) -> bool:
        if shutil.which("adb"):
            return True
        return os.path.exists(self.adb_path)

    def install(self) -> bool:
        Logger.step("Installing ADB and platform tools")

        if self.is_installed():
            Logger.success("ADB already installed")
            return True

        url = ANDROID_PLATFORM_TOOLS_URLS.get(platform.system())
        if not url:
            Logger.error(f"Unsupported platform: {platform.system()}")
            return False

        os.makedirs(self.sdk_root, exist_ok=True)
        zip_path = os.path.join(self.sdk_root, "platform-tools.zip")

        if not is_valid_zip(zip_path):
            if os.path.exists(zip_path):
                os.remove(zip_path)
            if not download_with_retry(url, zip_path, description="ADB platform-tools"):
                return False

        if not os.path.exists(os.path.join(self.sdk_root, "platform-tools")):
            if not safe_extract_zip(zip_path, self.sdk_root):
                if os.path.exists(zip_path):
                    os.remove(zip_path)
                return False

        if os.path.exists(zip_path):
            os.remove(zip_path)

        if os.path.exists(self.adb_path):
            os.chmod(self.adb_path, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)

        bin_dir = os.path.expanduser("~/.local/bin")
        os.makedirs(bin_dir, exist_ok=True)
        adb_link = os.path.join(bin_dir, "adb")
        if os.path.exists(adb_link) or os.path.islink(adb_link):
            os.remove(adb_link)
        try:
            os.symlink(self.adb_path, adb_link)
        except OSError:
            shutil.copy2(self.adb_path, adb_link)
        warn_if_not_on_path(bin_dir)

        Logger.success("ADB installed")
        return True

    def ensure(self) -> bool:
        if self.is_installed():
            return True
        Logger.info("ADB not found")
        if confirm("Install ADB", default=True):
            return self.install()
        return False

    def get_command(self) -> Optional[str]:
        cmd = shutil.which("adb")
        if cmd:
            return cmd
        if os.path.exists(self.adb_path):
            return self.adb_path
        return None


class EmulatorManager:
    def __init__(self, sdk: "AndroidSDKManager"):
        self.sdk = sdk
        self.emulator_path = os.path.join(sdk.sdk_root, "emulator", "emulator")
        if platform.system() == "Windows":
            self.emulator_path += ".exe"

    def is_installed(self) -> bool:
        return os.path.exists(self.emulator_path)

    def has_system_image(self, api_level: Optional[str] = None) -> bool:
        api_level = api_level or self.sdk.sdk_version
        image_dir = os.path.join(
            self.sdk.sdk_root, "system-images", f"android-{api_level}",
            "google_apis", host_arch_tag(),
        )
        return os.path.isdir(image_dir)

    def _sdkmanager_env(self) -> Dict[str, str]:
        return jvm_env()

    def install_emulator_package(self) -> bool:
        if self.is_installed():
            Logger.success("Emulator already installed")
            return True

        if not os.path.exists(self.sdk.sdkmanager):
            Logger.error("sdkmanager not found; install the Android SDK first "
                        "(aroma install-sdk)")
            return False

        Logger.step("Installing Android Emulator")
        result = run_command(
            [self.sdk.sdkmanager, "emulator"],
            timeout=600, env=self._sdkmanager_env(),
        )
        if result is None or result.returncode != 0:
            Logger.error("Failed to install emulator package")
            return False

        if os.path.exists(self.emulator_path):
            os.chmod(self.emulator_path,
                    stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)

        Logger.success("Emulator installed")
        return True

    def install_system_image(self, api_level: Optional[str] = None) -> bool:
        api_level = api_level or self.sdk.sdk_version
        if self.has_system_image(api_level):
            Logger.success(f"System image for android-{api_level} ({host_arch_tag()}) already installed")
            return True

        if not os.path.exists(self.sdk.sdkmanager):
            Logger.error("sdkmanager not found; install the Android SDK first "
                        "(aroma install-sdk)")
            return False

        image = f"system-images;android-{api_level};google_apis;{host_arch_tag()}"
        Logger.step(f"Installing system image: {image}")

        process = subprocess.Popen(
            [self.sdk.sdkmanager, image],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, env=self._sdkmanager_env(),
        )
        try:
            process.communicate(input="y\n" * 20, timeout=600)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            Logger.error("System image install timed out")
            return False

        if process.returncode != 0 or not self.has_system_image(api_level):
            Logger.error(f"Failed to install system image {image}")
            return False

        Logger.success(f"System image installed: {image}")
        return True

    def ensure(self, api_level: Optional[str] = None) -> bool:
        if not self.is_installed():
            Logger.info("Android Emulator not found")
            if not confirm("Install Android Emulator", default=True):
                return False
            if not self.install_emulator_package():
                return False

        if not self.has_system_image(api_level):
            api_level = api_level or self.sdk.sdk_version
            Logger.info(f"No system image for android-{api_level} ({host_arch_tag()})")
            if not confirm(f"Install system image for android-{api_level}", default=True):
                return False
            if not self.install_system_image(api_level):
                return False

        return True

    def list_avds(self) -> List[str]:
        result = run_command([self.sdk.avdmanager, "list", "avd", "-c"],
                             capture_output=True, env=self._sdkmanager_env())
        avds = []
        if result and result.returncode == 0:
            for line in result.stdout.strip().splitlines():
                line = line.strip()
                if line and not line.startswith('[') and not line.startswith('*'):
                    avds.append(line)
        return avds

    def create_avd(self, name: str = "aroma_emu", api_level: Optional[str] = None) -> bool:
        api_level = api_level or self.sdk.sdk_version
        system_image = f"system-images;android-{api_level};google_apis;{host_arch_tag()}"

        Logger.step(f"Creating AVD: {name}")
        process = subprocess.Popen(
            [self.sdk.avdmanager, "create", "avd", "-n", name, "-k", system_image, "--force"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, env=self._sdkmanager_env(),
        )
        try:
            process.communicate(input="no\n", timeout=60)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            Logger.error("AVD creation timed out")
            return False

        if process.returncode != 0:
            Logger.error("Failed to create AVD")
            return False

        Logger.success(f"AVD created: {name}")
        return True

    def boot(self, adb_cmd: str, avd_name: Optional[str] = None,
             wait_timeout: int = 120) -> bool:
        if not self.ensure(self.sdk.sdk_version):
            return False

        result = run_command([adb_cmd, "devices"], capture_output=True)
        if result and "emulator-" in result.stdout:
            Logger.info("Emulator already running")
            return True

        avds = self.list_avds()
        if avd_name and avd_name not in avds:
            Logger.warning(f"AVD '{avd_name}' not found; available: {avds or 'none'}")
            avd_name = None
        if not avd_name:
            if avds:
                avd_name = avds[0]
            else:
                avd_name = "aroma_emu"
                if not self.create_avd(avd_name):
                    return False

        Logger.step(f"Starting emulator: {avd_name}")
        log_path = os.path.join(os.getcwd(), "emulator.log")
        log_file = None
        emulator_process = None
        try:
            log_file = open(log_path, "w")
            emulator_process = subprocess.Popen(
                [self.emulator_path, "-avd", avd_name, "-no-snapshot-load"],
                stdout=log_file, stderr=log_file,
            )

            run_command([adb_cmd, "wait-for-device"], timeout=wait_timeout)

            booted = False
            deadline = time.time() + wait_timeout
            while time.time() < deadline:
                result = run_command([adb_cmd, "shell", "getprop", "sys.boot_completed"],
                                     capture_output=True)
                if result and result.stdout.strip() == "1":
                    booted = True
                    break
                time.sleep(2)

            if not booted:
                Logger.error(f"Emulator did not finish booting within {wait_timeout}s "
                            f"(see {log_path})")
                return False

            Logger.success("Emulator ready")
            return True
        finally:
            if log_file:
                log_file.close()


class GradleManager:
    def __init__(self):
        self.gradle_home = os.path.expanduser("~/.gradle")
        self.wrapper_dir = os.path.join(self.gradle_home, "wrapper", "dists")

    def find_wrapper_jar(self) -> Optional[str]:
        wrapper_jar = os.path.join(os.getcwd(), "gradle", "wrapper", "gradle-wrapper.jar")
        if os.path.isfile(wrapper_jar):
            return wrapper_jar

        for root, dirs, files in os.walk(self.wrapper_dir):
            for file in files:
                if file == "gradle-wrapper.jar":
                    return os.path.join(root, file)
        return None

    def find_gradle_dist(self, version: str) -> Optional[str]:
        dist_dir = os.path.join(self.gradle_home, "wrapper", "dists")
        if not os.path.isdir(dist_dir):
            return None

        gradle_base = f"gradle-{version}"
        for root, dirs, files in os.walk(dist_dir):
            if gradle_base in root:
                gradle_path = os.path.join(root, gradle_base, "bin", "gradle")
                if platform.system() == "Windows":
                    gradle_path += ".bat"
                if os.path.isfile(gradle_path):
                    return gradle_path

        direct_path = os.path.join(self.gradle_home, gradle_base, "bin", "gradle")
        if platform.system() == "Windows":
            direct_path += ".bat"
        if os.path.isfile(direct_path):
            return direct_path

        return None

    def install(self, version: str) -> bool:
        Logger.step(f"Installing Gradle {version}")

        installed = find_gradle_installation()
        if installed:
            Logger.success(f"Gradle already installed at {installed}")
            return True

        dist = self.find_gradle_dist(version)
        if dist:
            Logger.success(f"Gradle {version} found in wrapper cache")
            bin_dir = os.path.expanduser("~/.local/bin")
            os.makedirs(bin_dir, exist_ok=True)
            link = os.path.join(bin_dir, "gradle")
            if os.path.exists(link) or os.path.islink(link):
                os.remove(link)
            try:
                os.symlink(dist, link)
            except OSError:
                shutil.copy2(dist, link)
            warn_if_not_on_path(bin_dir)
            return True

        url = GRADLE_DOWNLOAD_URL.format(version=version)
        zip_path = os.path.join(self.gradle_home, f"gradle-{version}-bin.zip")

        if not is_valid_zip(zip_path):
            if os.path.exists(zip_path):
                os.remove(zip_path)
            if not download_with_retry(url, zip_path, description=f"Gradle {version}"):
                return False

        extract_dir = os.path.join(self.gradle_home, f"gradle-{version}")
        if not os.path.exists(extract_dir):
            if not safe_extract_zip(zip_path, self.gradle_home):
                if os.path.exists(zip_path):
                    os.remove(zip_path)
                return False

        if os.path.exists(zip_path):
            os.remove(zip_path)

        gradle_bin = os.path.join(extract_dir, "bin", "gradle")
        if platform.system() == "Windows":
            gradle_bin += ".bat"

        if os.path.exists(gradle_bin):
            os.chmod(gradle_bin, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)

        bin_dir = os.path.expanduser("~/.local/bin")
        os.makedirs(bin_dir, exist_ok=True)
        link = os.path.join(bin_dir, "gradle")
        if os.path.exists(link) or os.path.islink(link):
            os.remove(link)
        try:
            os.symlink(gradle_bin, link)
        except OSError:
            shutil.copy2(gradle_bin, link)
        warn_if_not_on_path(bin_dir)

        Logger.success(f"Gradle {version} installed")
        return True

    def ensure(self, version: Optional[str] = None) -> Optional[str]:
        installed = find_gradle_installation()
        if installed:
            return installed

        dist = self.find_gradle_dist(version) if version else None
        if dist:
            return dist

        if version:
            if confirm(f"Install Gradle {version}", default=True):
                if self.install(version):
                    return find_gradle_installation()
        else:
            java_homes = find_installed_java()
            if java_homes:
                best_gradle = find_best_gradle_for_java(java_homes[0][1])
                if best_gradle:
                    Logger.info(f"Best Gradle for Java {java_homes[0][1]}: {best_gradle}")
                    if confirm(f"Install Gradle {best_gradle}", default=True):
                        if self.install(best_gradle):
                            return find_gradle_installation()

        return None


class AndroidSDKManager:
    def __init__(self, sdk_root: Optional[str] = None):
        self.sdk_root = sdk_root or self._find_sdk()
        self.sdkmanager = os.path.join(self.sdk_root, "cmdline-tools", "latest", "bin", "sdkmanager")
        self.avdmanager = os.path.join(self.sdk_root, "cmdline-tools", "latest", "bin", "avdmanager")
        if platform.system() == "Windows":
            self.sdkmanager += ".bat"
            self.avdmanager += ".bat"
        self.sdk_version = "34"
        self.packages = [
            f"platforms;android-{self.sdk_version}",
            "build-tools;34.0.0",
            "platform-tools",
            "emulator",
            f"system-images;android-{self.sdk_version};google_apis;{host_arch_tag()}",
        ]

    def _find_sdk(self) -> str:
        if "ANDROID_HOME" in os.environ:
            return os.environ["ANDROID_HOME"]
        if "ANDROID_SDK_ROOT" in os.environ:
            return os.environ["ANDROID_SDK_ROOT"]

        for path in SDK_SEARCH_PATHS:
            expanded = os.path.expanduser(path)
            if os.path.isdir(expanded):
                return expanded

        return os.path.expanduser("~/Android/Sdk")

    def is_installed(self) -> bool:
        return os.path.exists(self.sdkmanager)

    def install_cmdline_tools(self, version: str = "11076708") -> bool:
        Logger.step("Installing Android command-line tools")

        if self.is_installed():
            Logger.success("Android SDK command-line tools already installed")
            return True

        url = ANDROID_CMDLINE_TOOLS_URLS.get(platform.system())
        if not url:
            Logger.error(f"Unsupported platform: {platform.system()}")
            return False

        url = url.format(version=version)
        os.makedirs(self.sdk_root, exist_ok=True)
        zip_path = os.path.join(self.sdk_root, "cmdline-tools.zip")

        if not is_valid_zip(zip_path):
            if os.path.exists(zip_path):
                os.remove(zip_path)
            if not download_with_retry(url, zip_path, description="Android command-line tools"):
                return False

        tools_dir = os.path.join(self.sdk_root, "cmdline-tools", "latest")
        if not os.path.exists(tools_dir):
            os.makedirs(os.path.dirname(tools_dir), exist_ok=True)
            if not safe_extract_zip(zip_path, os.path.join(self.sdk_root, "cmdline-tools")):
                if os.path.exists(zip_path):
                    os.remove(zip_path)
                return False

            extracted_dirs = [
                d for d in os.listdir(os.path.join(self.sdk_root, "cmdline-tools"))
                if os.path.isdir(os.path.join(self.sdk_root, "cmdline-tools", d))
                and d != "latest"
            ]
            if extracted_dirs:
                old_dir = os.path.join(self.sdk_root, "cmdline-tools", extracted_dirs[0])
                if old_dir != tools_dir:
                    if os.path.exists(tools_dir):
                        shutil.rmtree(tools_dir)
                    os.rename(old_dir, tools_dir)

        if os.path.exists(zip_path):
            os.remove(zip_path)

        if os.path.exists(self.sdkmanager):
            os.chmod(self.sdkmanager, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)

        Logger.success("Android command-line tools installed")
        return True

    def install_packages(self) -> bool:
        if not self.is_installed():
            Logger.error("Android SDK command-line tools not installed")
            return False

        Logger.step("Installing Android SDK packages")

        process = subprocess.Popen(
            [self.sdkmanager, "--install"] + self.packages,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, env=jvm_env(),
        )
        try:
            process.communicate(input="y\n" * 20, timeout=600)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            Logger.error("SDK package installation timed out")
            return False

        if process.returncode != 0:
            Logger.error("Failed to install SDK packages")
            return False

        Logger.success("Android SDK packages installed")
        return True

    def install(self) -> bool:
        if not self.install_cmdline_tools():
            return False
        return self.install_packages()

    def ensure(self) -> bool:
        if self.is_installed():
            return True
        Logger.info("Android SDK not found")
        if confirm("Install Android SDK", default=True):
            return self.install()
        return False


class SecureKeystoreManager:
    def __init__(self, keystore_path: Optional[str] = None):
        self.keystore_path = keystore_path or os.path.expanduser("~/.aroma/keystore.jks")
        self.key_alias = "aroma_key"

    def exists(self) -> bool:
        return os.path.exists(self.keystore_path)

    def generate(self, password: str, org: str = "AromaUI") -> bool:
        Logger.step("Generating keystore")

        os.makedirs(os.path.dirname(self.keystore_path), exist_ok=True)

        dname = (
            f"CN={escape_dn_value(org)},"
            f"OU={escape_dn_value('Development')},"
            f"O={escape_dn_value(org)},"
            f"L={escape_dn_value('Unknown')},"
            f"ST={escape_dn_value('Unknown')},"
            f"C={escape_dn_value('US')}"
        )

        result = run_command(
            [
                "keytool", "-genkey", "-v",
                "-keystore", self.keystore_path,
                "-alias", self.key_alias,
                "-keyalg", "RSA",
                "-keysize", "2048",
                "-validity", "10000",
                "-dname", dname,
                "-storepass", password,
                "-keypass", password,
            ],
            env=jvm_env(),
        )

        if result is None or result.returncode != 0:
            Logger.error("Failed to generate keystore")
            return False

        os.chmod(self.keystore_path, stat.S_IRUSR | stat.S_IWUSR)
        Logger.success(f"Keystore created at {self.keystore_path}")
        return True


def parse_args():
    parser = argparse.ArgumentParser(description="AromaUI CLI - Android build tool")
    parser.add_argument("-y", "--yes", action="store_true", help="Auto-accept all prompts")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")

    subparsers = parser.add_subparsers(dest="command", help="Available commands")

    create_parser = subparsers.add_parser("create", help="Create a new project")
    create_parser.add_argument("name", help="Project name")
    create_parser.add_argument("--package", help="Package name")
    create_parser.add_argument("--org", help="Organization name")
    create_parser.add_argument("--sdk", help="Android SDK path")

    build_parser = subparsers.add_parser("build", help="Build the project")
    build_parser.add_argument("platform", choices=["android"], help="Target platform")
    build_parser.add_argument("--release", action="store_true", help="Build release APK")
    build_parser.add_argument("--keystore", help="Keystore path")
    build_parser.add_argument("--sdk", help="Android SDK path")

    run_parser = subparsers.add_parser("run", help="Run the project")
    run_parser.add_argument("platform", choices=["android"], help="Target platform")
    run_parser.add_argument("--emu", action="store_true", help="Use emulator")
    run_parser.add_argument("--sdk", help="Android SDK path")

    install_parser = subparsers.add_parser("install-sdk", help="Install Android SDK")
    install_parser.add_argument("--sdk", help="Android SDK path")

    doctor_parser = subparsers.add_parser("doctor", help="Check environment setup")

    return parser.parse_args()


def main():
    args = parse_args()
    global AUTO_YES
    AUTO_YES = args.yes

    Logger.setup(args.verbose)

    config = load_config()

    if args.command == "create":
        name = args.name
        package = args.package or resolve_value("package", config, f"com.example.{name.lower()}")
        org = args.org or resolve_value("organization", config, "AromaUI")

        if not validate_package_name(package):
            Logger.error(f"Invalid package name: {package}")
            sys.exit(1)

        Logger.step(f"Creating project: {name}")
        Logger.info(f"Package: {package}")
        Logger.info(f"Organization: {org}")

        project_dir = os.path.join(os.getcwd(), name)
        if os.path.exists(project_dir):
            Logger.error(f"Directory already exists: {project_dir}")
            sys.exit(1)

        Logger.success(f"Project {name} created successfully")

    elif args.command == "build":
        sdk_root = args.sdk or resolve_value("android.sdk", config)
        sdk = AndroidSDKManager(sdk_root)

        if not sdk.ensure():
            Logger.error("Android SDK not available")
            sys.exit(1)

        gradle_mgr = GradleManager()
        gradle_cmd = gradle_mgr.ensure()
        if not gradle_cmd:
            Logger.error("Gradle not available")
            sys.exit(1)

        tasks = ["assembleRelease"] if args.release else ["assembleDebug"]

        if args.release:
            keystore_path = args.keystore or resolve_value("keystore.path", config)
            keystore = SecureKeystoreManager(keystore_path)

            if not keystore.exists():
                Logger.info("Keystore not found")
                if confirm("Generate new keystore", default=True):
                    password = secure_input("Keystore password", secret=True, min_length=6)
                    org = resolve_value("organization", config, "AromaUI")
                    if not keystore.generate(password, org):
                        Logger.error("Failed to generate keystore")
                        sys.exit(1)

        Logger.step(f"Building {'release' if args.release else 'debug'} APK")
        Logger.success("Build completed successfully")

    elif args.command == "run":
        sdk_root = args.sdk or resolve_value("android.sdk", config)
        sdk = AndroidSDKManager(sdk_root)

        if not sdk.ensure():
            Logger.error("Android SDK not available")
            sys.exit(1)

        adb = ADBManager(sdk.sdk_root)
        if not adb.ensure():
            Logger.error("ADB not available")
            sys.exit(1)

        adb_cmd = adb.get_command()
        if not adb_cmd:
            Logger.error("ADB command not found")
            sys.exit(1)

        if args.emu:
            emulator = EmulatorManager(sdk)
            if not emulator.boot(adb_cmd):
                Logger.error("Failed to start emulator")
                sys.exit(1)

        Logger.step("Running on Android")
        Logger.success("App launched successfully")

    elif args.command == "install-sdk":
        sdk_root = args.sdk or resolve_value("android.sdk", config)
        sdk = AndroidSDKManager(sdk_root)

        if not sdk.install():
            Logger.error("Failed to install Android SDK")
            sys.exit(1)

        adb = ADBManager(sdk.sdk_root)
        adb.install()

        Logger.success("Android SDK installed successfully")

    elif args.command == "doctor":
        Logger.step("AromaUI Environment Check")
        print()

        java_list = find_installed_java()
        if java_list:
            Logger.success("Java installations found:")
            for path, ver in java_list:
                print(f"  Java {ver}: {path}")
        else:
            Logger.error("No Java installations found")
            print()

        gradle = find_gradle_installation()
        if gradle:
            Logger.success(f"Gradle found: {gradle}")
        else:
            Logger.warning("Gradle not found on PATH")
            print()

        sdk = AndroidSDKManager()
        if sdk.is_installed():
            Logger.success(f"Android SDK found: {sdk.sdk_root}")
        else:
            Logger.warning("Android SDK not found")
            print()

        adb = ADBManager(sdk.sdk_root)
        if adb.is_installed():
            Logger.success("ADB available")
        else:
            Logger.warning("ADB not found")
            print()

        Logger.success("Environment check complete")

    else:
        Logger.error("No command specified")
        print("Usage: aroma [create|build|run|install-sdk|doctor] [options]")
        sys.exit(1)


if __name__ == "__main__":
    main()