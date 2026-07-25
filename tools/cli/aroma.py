#!/usr/bin/env python3

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
from typing import Any, Callable, Dict, List, Optional, Tuple
from urllib.error import URLError, HTTPError


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

# Packages matching these prefixes are large (several hundred MB to 1GB+)
# and need substantially more time than the default package timeout.
# System images in particular routinely exceed 600s on anything but a very
# fast connection, which is what caused the original timeout failures.
LARGE_PACKAGE_PREFIXES = (
    "system-images;",
)

DEFAULT_PACKAGE_TIMEOUT = 600
LARGE_PACKAGE_TIMEOUT = 1800


class DownloadProgress:
    def __init__(self, desc: str, total: int = 0):
        self.desc = desc
        self.total = total
        self.downloaded = 0
        self.last_report = 0
        self.start_time = time.time()
        self.width = 40
    
    def update(self, count: int):
        self.downloaded += count
        now = time.time()
        if now - self.last_report < 0.2 and self.downloaded < self.total:
            return
        self.last_report = now
        
        if self.total > 0:
            pct = self.downloaded / self.total
            filled = int(self.width * pct)
            bar = '█' * filled + '░' * (self.width - filled)
            elapsed = now - self.start_time
            speed = self.downloaded / elapsed if elapsed > 0 else 0
            speed_str = self._format_size(speed) + "/s"
            downloaded_str = self._format_size(self.downloaded)
            total_str = self._format_size(self.total)
            sys.stdout.write(f"\r{self.desc}: |{bar}| {pct*100:.1f}% {downloaded_str}/{total_str} {speed_str}  ")
        else:
            downloaded_str = self._format_size(self.downloaded)
            sys.stdout.write(f"\r{self.desc}: {downloaded_str} downloaded  ")
        sys.stdout.flush()
    
    def finish(self):
        if self.total > 0:
            elapsed = time.time() - self.start_time
            speed = self.total / elapsed if elapsed > 0 else 0
            speed_str = self._format_size(speed) + "/s"
            total_str = self._format_size(self.total)
            sys.stdout.write(f"\r{self.desc}: |{'█' * self.width}| 100.0% {total_str}/{total_str} {speed_str}  \n")
        else:
            total_str = self._format_size(self.downloaded)
            sys.stdout.write(f"\r{self.desc}: {total_str} downloaded  \n")
        sys.stdout.flush()
    
    def _format_size(self, size: float) -> str:
        for unit in ['B', 'KB', 'MB', 'GB']:
            if size < 1024:
                return f"{size:.1f} {unit}"
            size /= 1024
        return f"{size:.1f} TB"


def download_file(url: str, dest: str, desc: str = "Downloading") -> bool:
    ctx = ssl.create_default_context()
    ctx.check_hostname = True
    ctx.verify_mode = ssl.CERT_REQUIRED
    
    part_path = dest + ".part"
    
    for attempt in range(3):
        try:
            Logger.info(f"{desc} (attempt {attempt + 1}/3)")
            req = urllib.request.Request(url)
            req.add_header('User-Agent', 'AromaUI-CLI/1.0')
            with urllib.request.urlopen(req, context=ctx, timeout=300) as resp:
                if resp.status != 200:
                    raise HTTPError(url, resp.status, "Bad status", resp.headers, None)
                total = int(resp.headers.get('content-length', 0))
                progress = DownloadProgress(desc, total)
                with open(part_path, 'wb') as f:
                    while True:
                        chunk = resp.read(8192)
                        if not chunk:
                            break
                        f.write(chunk)
                        progress.update(len(chunk))
                progress.finish()
            os.replace(part_path, dest)
            return True
        except (URLError, HTTPError, ssl.SSLError, IOError) as e:
            Logger.debug(f"Download failed: {e}")
            if os.path.exists(part_path):
                try:
                    os.remove(part_path)
                except OSError:
                    pass
            if attempt == 2:
                Logger.error(f"{desc} failed after 3 attempts")
                return False
            time.sleep(2 ** attempt)
    return False


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
            except (json.JSONDecodeError, IOError):
                pass
    
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
    value = config
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
    def step(cls, msg: str):
        print(f"{Colors.OKBLUE}==>{Colors.ENDC} {Colors.BOLD}{msg}{Colors.ENDC}")
    
    @classmethod
    def success(cls, msg: str):
        print(f"{Colors.OKGREEN}OK {msg}{Colors.ENDC}")
    
    @classmethod
    def error(cls, msg: str):
        print(f"{Colors.FAIL}ERROR {msg}{Colors.ENDC}", file=sys.stderr)
    
    @classmethod
    def info(cls, msg: str):
        if cls._verbose:
            print(f"{Colors.OKCYAN}INFO {msg}{Colors.ENDC}")
    
    @classmethod
    def warning(cls, msg: str):
        print(f"{Colors.WARNING}WARN {msg}{Colors.ENDC}")
    
    @classmethod
    def debug(cls, msg: str):
        if cls._verbose:
            print(f"{Colors.OKCYAN}DEBUG {msg}{Colors.ENDC}")


def secure_input(prompt: str, default: str = None, validator: Callable = None,
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
            Logger.error("No input available (stdin closed) and no default provided")
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


# JLS-reserved words + boolean/null literals. Contextual keywords (var,
# record, yield, sealed, ...) are deliberately excluded: they're legal
# identifiers outside their special syntactic context, and a package
# segment is such a context.
_JAVA_RESERVED_WORDS = frozenset({
    "abstract", "assert", "boolean", "break", "byte", "case", "catch",
    "char", "class", "const", "continue", "default", "do", "double",
    "else", "enum", "extends", "final", "finally", "float", "for",
    "goto", "if", "implements", "import", "instanceof", "int",
    "interface", "long", "native", "new", "package", "private",
    "protected", "public", "return", "short", "static", "strictfp",
    "super", "switch", "synchronized", "this", "throw", "throws",
    "transient", "try", "void", "volatile", "while",
    "true", "false", "null",
})

_PACKAGE_SEGMENT_RE = re.compile(r'^[a-z][a-z0-9_]*$')


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


def validate_country_code(code: str) -> bool:
    return len(code) == 2 and code.isalpha() and code.isupper()


_DN_FORBIDDEN_CHARS = set(',+"\\<>;=')


def validate_dn_field(val: str) -> bool:
    return not any(ch in _DN_FORBIDDEN_CHARS for ch in val)


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


def package_timeout(package: str) -> int:
    """Return the timeout (seconds) to use for a given sdkmanager package.

    Large packages (currently: system images) get a longer timeout since
    they routinely run 1GB+ and can legitimately take longer than 600s on
    anything but a fast connection.
    """
    if package.startswith(LARGE_PACKAGE_PREFIXES):
        return LARGE_PACKAGE_TIMEOUT
    return DEFAULT_PACKAGE_TIMEOUT


def get_installed_sdk_packages(sdkmanager: str, env: Dict) -> Optional[set]:
    """Return the set of currently installed package paths, or None if the
    query itself failed (in which case callers should fall back to
    attempting the install rather than assuming nothing is installed).
    """
    result = run_command([sdkmanager, "--list_installed"], capture_output=True, timeout=60, env=env)
    if result is None or result.returncode != 0:
        return None
    
    installed = set()
    output = result.stdout or ""
    for line in output.splitlines():
        line = line.strip()
        if not line or line.lower().startswith(("installed packages", "path", "---", "available")):
            continue
        # `sdkmanager --list_installed` lines look like:
        #   platforms;android-34 | 2 | Android SDK Platform 34 | platforms/android-34
        first_col = line.split('|')[0].strip()
        if first_col:
            installed.add(first_col)
    return installed


def find_aroma_root() -> str:
    return os.path.abspath(os.path.join(os.path.dirname(os.path.realpath(__file__)), "..", ".."))


def safe_extract_zip(zip_path: str, extract_dir: str) -> bool:
    try:
        with zipfile.ZipFile(zip_path, 'r') as zf:
            for member in zf.infolist():
                target = os.path.abspath(os.path.join(extract_dir, member.filename))
                if not target.startswith(os.path.abspath(extract_dir) + os.sep):
                    Logger.error(f"Path traversal detected: {member.filename}")
                    return False
            total = len(zf.infolist())
            for i, member in enumerate(zf.infolist()):
                zf.extract(member, extract_dir)
                pct = (i + 1) / total * 100
                sys.stdout.write(f"\rExtracting: {pct:.0f}% ({i+1}/{total})  ")
                sys.stdout.flush()
            sys.stdout.write("\rExtracting: 100% complete  \n")
            sys.stdout.flush()
        return True
    except (zipfile.BadZipFile, IOError) as e:
        Logger.error(f"Extraction failed: {e}")
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
        
        if not os.path.exists(zip_path):
            if not download_file(url, zip_path, "Platform Tools"):
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
        if os.path.exists(adb_link):
            os.remove(adb_link)
        try:
            os.symlink(self.adb_path, adb_link)
        except OSError:
            shutil.copy2(self.adb_path, adb_link)
        
        Logger.success("ADB installed")
        return True
    
    def ensure(self) -> bool:
        if self.is_installed():
            return True
        Logger.info("ADB not found")
        if secure_input("Install ADB", default="y",
                       validator=lambda x: x.lower() in ['y', 'n']).lower().startswith('y'):
            return self.install()
        return False
    
    def get_command(self) -> Optional[str]:
        cmd = shutil.which("adb")
        if cmd:
            return cmd
        if os.path.exists(self.adb_path):
            return self.adb_path
        return None


class GradleManager:
    def __init__(self, config: Dict[str, Any]):
        self.config = config
        self.cache_dir = os.path.expanduser("~/.cache/aroma/gradle")
    
    def resolve_version(self) -> str:
        configured = resolve_value("android.gradle_version", self.config, None)
        if configured:
            return configured
        
        java_versions = find_installed_java()
        if java_versions:
            java_ver = max(v[1] for v in java_versions)
            best = find_best_gradle_for_java(java_ver)
            if best:
                return best
        
        return GRADLE_JAVA_MATRIX[0][0]
    
    def find_project_gradlew(self, project_dir: str) -> Optional[str]:
        gradlew = os.path.join(project_dir, "android", "gradlew")
        if platform.system() == "Windows":
            gradlew += ".bat"
        if os.path.isfile(gradlew):
            return gradlew
        return None
    
    def get_cached_path(self, version: str) -> Optional[str]:
        gradle_bin = os.path.join(self.cache_dir, f"gradle-{version}", "bin", "gradle")
        if platform.system() == "Windows":
            gradle_bin += ".bat"
        if os.path.isfile(gradle_bin):
            return gradle_bin
        return None
    
    def download(self, version: str) -> Optional[str]:
        Logger.step(f"Installing Gradle {version}")
        
        cached = self.get_cached_path(version)
        if cached:
            Logger.success(f"Gradle {version} already installed")
            return cached
        
        java_home = find_java_for_gradle(version)
        if not java_home:
            Logger.error(f"No compatible Java found for Gradle {version}")
            return None
        
        os.environ["JAVA_HOME"] = java_home
        os.makedirs(self.cache_dir, exist_ok=True)
        
        url = GRADLE_DOWNLOAD_URL.format(version=version)
        zip_path = os.path.join(self.cache_dir, f"gradle-{version}.zip")
        
        if not os.path.exists(zip_path):
            if not download_file(url, zip_path, f"Gradle {version}"):
                return None
        
        if not os.path.exists(os.path.join(self.cache_dir, f"gradle-{version}")):
            if not safe_extract_zip(zip_path, self.cache_dir):
                return None
        
        gradle_bin = self.get_cached_path(version)
        if gradle_bin:
            os.chmod(gradle_bin, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
            
            bin_dir = os.path.expanduser("~/.local/bin")
            os.makedirs(bin_dir, exist_ok=True)
            link = os.path.join(bin_dir, "gradle")
            if os.path.exists(link):
                os.remove(link)
            try:
                os.symlink(gradle_bin, link)
            except OSError:
                shutil.copy2(gradle_bin, link)
            
            Logger.success(f"Gradle {version} installed")
            return gradle_bin
        
        return None
    
    def ensure(self, project_dir: str) -> Optional[str]:
        gradlew = self.find_project_gradlew(project_dir)
        if gradlew:
            return gradlew
        
        version = self.resolve_version()
        Logger.info(f"Gradle not found, downloading version {version}")
        return self.download(version)
    
    def write_wrapper_properties(self, android_dir: str, version: str):
        wrapper_dir = os.path.join(android_dir, "gradle", "wrapper")
        os.makedirs(wrapper_dir, exist_ok=True)
        
        wrapper_props = os.path.join(wrapper_dir, "gradle-wrapper.properties")
        expected_url = f"https\\://services.gradle.org/distributions/gradle-{version}-bin.zip"
        
        content = f"""distributionBase=GRADLE_USER_HOME
distributionPath=wrapper/dists
distributionUrl={expected_url}
networkTimeout=10000
validateDistributionUrl=true
zipStoreBase=GRADLE_USER_HOME
zipStorePath=wrapper/dists
"""
        with open(wrapper_props, 'w') as f:
            f.write(content)
    
    def remove_bad_wrapper_cache(self):
        gradle_dists = os.path.join(os.path.expanduser("~/.gradle"), "wrapper", "dists")
        if not os.path.isdir(gradle_dists):
            return
        
        for dist in os.listdir(gradle_dists):
            dist_path = os.path.join(gradle_dists, dist)
            if os.path.isdir(dist_path):
                if "gradle-9." in dist.lower() or "gradle-9" in dist.lower():
                    Logger.info(f"Removing cached Gradle 9.x: {dist}")
                    shutil.rmtree(dist_path, ignore_errors=True)
    
    def generate_wrapper(self, project_dir: str) -> bool:
        android_dir = os.path.join(project_dir, "android")
        version = self.resolve_version()
        
        os.makedirs(android_dir, exist_ok=True)
        
        self.remove_bad_wrapper_cache()
        self.write_wrapper_properties(android_dir, version)
        
        gradle_cmd = self.download(version)
        if not gradle_cmd:
            Logger.error("Cannot generate Gradle wrapper, Gradle download failed")
            return False
        
        env = os.environ.copy()
        if "JAVA_HOME" not in env:
            java_home = find_java_for_gradle(version)
            if java_home:
                env["JAVA_HOME"] = java_home
            else:
                java_homes = find_installed_java()
                if java_homes:
                    env["JAVA_HOME"] = os.path.dirname(os.path.dirname(java_homes[0][0]))
        
        Logger.info(f"Generating Gradle wrapper for version {version}")
        result = run_command(
            [gradle_cmd, "wrapper", "--gradle-version", version],
            cwd=android_dir,
            timeout=120,
            env=env
        )
        
        if result is None or result.returncode != 0:
            return self._create_manual_wrapper(android_dir, version)
        
        gradlew_path = os.path.join(android_dir, "gradlew")
        gradlew_bat = os.path.join(android_dir, "gradlew.bat")
        
        self.write_wrapper_properties(android_dir, version)
        
        if os.path.exists(gradlew_path):
            os.chmod(gradlew_path, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
            if os.path.exists(gradlew_bat):
                os.chmod(gradlew_bat, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
            Logger.success(f"Gradle wrapper {version} ready")
            return True
        
        return False
    
    def _create_manual_wrapper(self, android_dir: str, version: str) -> bool:
        gradlew_path = os.path.join(android_dir, "gradlew")
        gradlew_bat = os.path.join(android_dir, "gradlew.bat")
        
        wrapper_jar_src = os.path.join(self.cache_dir, f"gradle-{version}", "lib", f"gradle-wrapper-{version}.jar")
        wrapper_jar_dst = os.path.join(android_dir, "gradle", "wrapper", "gradle-wrapper.jar")
        
        if os.path.exists(wrapper_jar_src):
            os.makedirs(os.path.dirname(wrapper_jar_dst), exist_ok=True)
            shutil.copy2(wrapper_jar_src, wrapper_jar_dst)
        else:
            gradle_wrapper_url = f"https://raw.githubusercontent.com/gradle/gradle/v{version}/gradle/wrapper/gradle-wrapper.jar"
            if not download_file(gradle_wrapper_url, wrapper_jar_dst, "gradle-wrapper.jar"):
                Logger.warning("Failed to download wrapper jar")
        
        self.write_wrapper_properties(android_dir, version)
        
        gradlew_content = """#!/bin/sh
set -e
APP_HOME="$(cd "$(dirname "$0")" && pwd)"
CLASSPATH=$APP_HOME/gradle/wrapper/gradle-wrapper.jar
if [ -n "$JAVA_HOME" ]; then
    JAVACMD="$JAVA_HOME/bin/java"
else
    JAVACMD="java"
fi
exec "$JAVACMD" -cp "$CLASSPATH" org.gradle.wrapper.GradleWrapperMain "$@"
"""
        with open(gradlew_path, 'w') as f:
            f.write(gradlew_content)
        os.chmod(gradlew_path, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
        
        gradlew_bat_content = """@rem
@if "%DEBUG%"=="" @echo off
@rem
set DIRNAME=%~dp0
if "%DIRNAME%"=="" set DIRNAME=.
set APP_HOME=%DIRNAME%
set CLASSPATH=%APP_HOME%\\gradle\\wrapper\\gradle-wrapper.jar
if defined JAVA_HOME goto findJavaFromJavaHome
set JAVA_EXE=java.exe
%JAVA_EXE% -version >NUL 2>&1
if "%ERRORLEVEL%"=="0" goto execute
echo ERROR: JAVA_HOME is not set and no java command could be found in your PATH.
goto fail
:findJavaFromJavaHome
set JAVA_HOME=%JAVA_HOME:"=%
set JAVA_EXE=%JAVA_HOME%/bin/java.exe
if exist "%JAVA_EXE%" goto execute
echo ERROR: JAVA_HOME is set to an invalid directory: %JAVA_HOME%
goto fail
:execute
@rem Setup the command line
set WRAPPER_LAUNCHER=org.gradle.wrapper.GradleWrapperMain
"%JAVA_EXE%" -cp "%CLASSPATH%" %WRAPPER_LAUNCHER% %*
:end
@rem End local scope for the variables with windows NT shell
if "%OS%"=="Windows_NT" endlocal
:omega
"""
        with open(gradlew_bat, 'w') as f:
            f.write(gradlew_bat_content)
        
        if os.path.exists(wrapper_jar_dst):
            Logger.success(f"Gradle wrapper {version} manually created")
            return True
        
        Logger.warning("Gradle wrapper created but wrapper jar is missing")
        return os.path.exists(gradlew_path)


class SecureKeystoreManager:
    def __init__(self, project_dir: str, config: Dict[str, Any]):
        self.project_dir = project_dir
        self.config = config
        self.signing = {
            "key_size": int(resolve_value("signing.key_size", config, 2048)),
            "validity_days": int(resolve_value("signing.validity_days", config, 10000)),
            "algorithm": resolve_value("signing.algorithm", config, "RSA"),
        }
        if self.signing["key_size"] < 2048:
            self.signing["key_size"] = 2048
        
        self.android_dir = os.path.join(project_dir, "android")
        self.keystore_dir = os.path.join(self.android_dir, "keystore")
        self.keystore_file = os.path.join(self.keystore_dir, "release.keystore")
        self.props_file = os.path.join(self.android_dir, "keystore.properties")
    
    def exists(self) -> bool:
        return os.path.exists(self.props_file) and os.path.exists(self.keystore_file)
    
    def verify(self, password: str) -> bool:
        env = os.environ.copy()
        env["_AROMA_STOREPASS"] = password
        result = run_command(
            ["keytool", "-list", "-keystore", self.keystore_file,
             "-storepass:env", "_AROMA_STOREPASS"],
            capture_output=True, timeout=30, env=env
        )
        return result is not None and result.returncode == 0
    
    def create(self, alias: str, storepass: str, keypass: str,
               cn: str, ou: str, o: str, l: str, st: str, c: str) -> bool:
        os.makedirs(self.keystore_dir, exist_ok=True)
        
        if os.path.exists(self.keystore_file):
            os.remove(self.keystore_file)
        
        dn_parts = []
        if cn:
            dn_parts.append(f"CN={cn}")
        if ou:
            dn_parts.append(f"OU={ou}")
        if o:
            dn_parts.append(f"O={o}")
        if l:
            dn_parts.append(f"L={l}")
        if st:
            dn_parts.append(f"ST={st}")
        if c:
            dn_parts.append(f"C={c}")
        dname = ",".join(dn_parts)
        
        env = os.environ.copy()
        env["_AROMA_STOREPASS"] = storepass
        env["_AROMA_KEYPASS"] = keypass
        
        cmd = [
            "keytool", "-genkey", "-v",
            "-keystore", self.keystore_file,
            "-alias", alias,
            "-keyalg", self.signing["algorithm"],
            "-keysize", str(self.signing["key_size"]),
            "-validity", str(self.signing["validity_days"]),
            "-dname", dname,
            "-storepass:env", "_AROMA_STOREPASS",
            "-keypass:env", "_AROMA_KEYPASS",
            "-storetype", "PKCS12"
        ]
        
        result = run_command(cmd, timeout=60, env=env)
        if result is None or result.returncode != 0:
            return False
        
        os.chmod(self.keystore_file, stat.S_IRUSR | stat.S_IWUSR)
        return True
    
    def save_props(self, alias: str, storepass: str, keypass: str) -> bool:
        try:
            with open(self.props_file, 'w') as f:
                f.write(f"storeFile={self.keystore_file}\n")
                f.write(f"storePassword={storepass}\n")
                f.write(f"keyPassword={keypass}\n")
                f.write(f"keyAlias={alias}\n")
            os.chmod(self.props_file, stat.S_IRUSR | stat.S_IWUSR)
            return True
        except IOError:
            return False
    
    def setup(self) -> bool:
        Logger.step("Setting up release signing keystore")
        
        alias = secure_input("Key alias", default="release")
        
        storepass = secure_input(
            "Keystore password",
            validator=lambda x: len(x) >= 8,
            error_msg="Minimum 8 characters",
            secret=True,
            min_length=8
        )
        
        if secure_input("Confirm keystore password", secret=True) != storepass:
            Logger.error("Passwords do not match")
            return False
        
        if secure_input("Same password for key", default="y",
                       validator=lambda x: x.lower() in ['y', 'n']).lower().startswith('y'):
            keypass = storepass
        else:
            keypass = secure_input("Key password", validator=lambda x: len(x) >= 8,
                                   error_msg="Minimum 8 characters", secret=True, min_length=8)
            if secure_input("Confirm key password", secret=True) != keypass:
                Logger.error("Key passwords do not match")
                return False
        
        dn_error = "Cannot contain , + \" \\ < > ; ="
        name = secure_input("Name", default="Developer",
                           validator=validate_dn_field, error_msg=dn_error)
        org_unit = secure_input("Organizational Unit", default="Development",
                               validator=validate_dn_field, error_msg=dn_error)
        org = secure_input("Organization", default="Personal",
                          validator=validate_dn_field, error_msg=dn_error)
        city = secure_input("City", default="",
                           validator=validate_dn_field, error_msg=dn_error)
        state = secure_input("State", default="",
                            validator=validate_dn_field, error_msg=dn_error)
        country = secure_input("Country Code", default="US",
                              validator=validate_country_code,
                              error_msg="2 uppercase letters required")
        
        if not self.create(alias, storepass, keypass, name, org_unit, org, city, state, country):
            Logger.error("Failed to create keystore")
            return False
        
        if not self.save_props(alias, storepass, keypass):
            Logger.error("Failed to save properties")
            return False
        
        Logger.success(f"Keystore created: {self.keystore_file}")
        return True


class AndroidSDKManager:
    def __init__(self, config: Dict[str, Any]):
        self.config = config
        self.sdk_version = resolve_value("android.sdk_version", config, "34")
        self.ndk_version = resolve_value("android.ndk_version", config, "25.1.8937393")
        self.cmake_version = resolve_value("android.cmake_version", config, "3.22.1")
        self.cmdline_version = resolve_value("android.cmdline_tools_version", config, "11076708")
        
        self.sdk_root = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT") or os.path.expanduser("~/Android/Sdk")
        
        self.cmdline_dir = os.path.join(self.sdk_root, "cmdline-tools", "latest")
        self.sdkmanager = os.path.join(self.cmdline_dir, "bin", "sdkmanager")
        self.avdmanager = os.path.join(self.cmdline_dir, "bin", "avdmanager")
        if platform.system() == "Windows":
            self.sdkmanager += ".bat"
            self.avdmanager += ".bat"
        
        self.adb = ADBManager(self.sdk_root)
        self.gradle = GradleManager(config)
        
        self.packages = resolve_value("android.sdk_packages", config, [
            "platform-tools",
            f"platforms;android-{self.sdk_version}",
            f"build-tools;{self.sdk_version}.0.0",
            f"ndk;{self.ndk_version}",
            f"cmake;{self.cmake_version}",
            "emulator",
            f"system-images;android-{self.sdk_version};google_apis;x86_64",
            f"system-images;android-{self.sdk_version};google_apis;arm64-v8a",
        ])
    
    def find(self) -> Optional[str]:
        paths = [self.sdk_root]
        for p in SDK_SEARCH_PATHS:
            paths.append(os.path.expanduser(p))
        
        for path in paths:
            sm = os.path.join(path, "cmdline-tools", "latest", "bin", "sdkmanager")
            if platform.system() == "Windows":
                sm += ".bat"
            if os.path.exists(sm):
                self.sdk_root = path
                self.cmdline_dir = os.path.dirname(os.path.dirname(sm))
                self.sdkmanager = sm
                self.avdmanager = os.path.join(self.cmdline_dir, "bin", "avdmanager")
                if platform.system() == "Windows":
                    self.avdmanager += ".bat"
                self.adb = ADBManager(path)
                return path
        
        return None
    
    def licenses_accepted(self) -> bool:
        lic_dir = os.path.join(self.sdk_root, "licenses")
        if not os.path.isdir(lic_dir):
            return False
        return len(os.listdir(lic_dir)) > 0
    
    def accept_licenses(self) -> bool:
        if not os.path.exists(self.sdkmanager):
            return False
        
        if self.licenses_accepted():
            Logger.success("All licenses accepted")
            return True
        
        Logger.step("Android SDK License Agreement")
        Logger.info("https://developer.android.com/studio/terms")
        
        if not secure_input("Accept all licenses", default="y",
                           validator=lambda x: x.lower() in ['y', 'n']).lower().startswith('y'):
            Logger.warning("Licenses required to use Android SDK")
            return False
        
        try:
            env = os.environ.copy()
            if "JAVA_HOME" not in env:
                java_homes = find_installed_java()
                if java_homes:
                    env["JAVA_HOME"] = os.path.dirname(os.path.dirname(java_homes[0][0]))
            
            process = subprocess.Popen(
                [self.sdkmanager, "--licenses"],
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, env=env
            )
            
            try:
                stdout, stderr = process.communicate(input="y\n" * 100, timeout=120)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()
                Logger.error("License acceptance timed out")
                return False
            
            if self.licenses_accepted():
                Logger.success("Licenses accepted")
                return True
            
            Logger.warning("Some licenses may remain unaccepted")
            return self.licenses_accepted()
        except Exception as e:
            Logger.error(f"License acceptance failed: {e}")
            return False
    
    def install_packages(self) -> bool:
        env = os.environ.copy()
        if "JAVA_HOME" not in env:
            java_homes = find_installed_java()
            if java_homes:
                env["JAVA_HOME"] = os.path.dirname(os.path.dirname(java_homes[0][0]))
        
        # Query what's already installed so a re-run after a partial failure
        # (e.g. a previous timeout on one package) doesn't redo work that
        # already succeeded, and doesn't risk hitting the same timeout on a
        # package a user may have installed manually in the meantime.
        # If the query itself fails, installed stays None and every package
        # falls through to being attempted, matching the old behavior.
        installed = get_installed_sdk_packages(self.sdkmanager, env)
        
        total = len(self.packages)
        for i, package in enumerate(self.packages):
            if installed is not None and package in installed:
                Logger.success(f"Already installed ({i+1}/{total}): {package}")
                continue
            
            Logger.step(f"Installing SDK package ({i+1}/{total})")
            Logger.info(f"  {package}")
            timeout = package_timeout(package)
            if timeout != DEFAULT_PACKAGE_TIMEOUT:
                Logger.info(f"  (using extended timeout: {timeout}s)")
            result = run_command([self.sdkmanager, package], timeout=timeout, env=env)
            if result is None or result.returncode != 0:
                Logger.error(f"Failed: {package}")
                return False
            Logger.success(f"Installed: {package}")
        
        Logger.success("All SDK packages installed")
        return True
    def install_cmdline(self) -> bool:
        if os.path.exists(self.sdkmanager):
            return True
        
        Logger.step("Downloading Android SDK command-line tools")
        
        url_template = ANDROID_CMDLINE_TOOLS_URLS.get(platform.system())
        if not url_template:
            Logger.error(f"Unsupported platform: {platform.system()}")
            return False
        
        url = url_template.format(version=self.cmdline_version)
        os.makedirs(os.path.dirname(self.cmdline_dir), exist_ok=True)
        zip_path = os.path.join(self.sdk_root, "cmdline-tools.zip")
        
        if not os.path.exists(zip_path):
            if not download_file(url, zip_path, "Android SDK Command-line Tools"):
                return False
        
        extracted = os.path.join(os.path.dirname(self.cmdline_dir), "cmdline-tools")
        if not os.path.exists(self.cmdline_dir) and not os.path.exists(extracted):
            Logger.step("Extracting Android SDK command-line tools")
            if not safe_extract_zip(zip_path, os.path.dirname(self.cmdline_dir)):
                if os.path.exists(zip_path):
                    os.remove(zip_path)
                return False
        
        if os.path.exists(extracted) and extracted != self.cmdline_dir:
            if os.path.exists(self.cmdline_dir):
                shutil.rmtree(self.cmdline_dir)
            shutil.move(extracted, self.cmdline_dir)
        
        if os.path.exists(zip_path):
            os.remove(zip_path)
        
        # Set permissions for BOTH tools
        os.chmod(self.sdkmanager, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
        
        # ADD THIS LINE:
        if os.path.exists(self.avdmanager):
            os.chmod(self.avdmanager, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
        
        Logger.success("Android SDK command-line tools installed")
        return True
    def install_emulator(self) -> bool:
        emulator_path = os.path.join(self.sdk_root, "emulator", "emulator")
        if platform.system() == "Windows":
            emulator_path += ".exe"
        
        if os.path.exists(emulator_path):
            Logger.success("Emulator already installed")
            return True
        
        Logger.step("Installing Android Emulator")
        env = os.environ.copy()
        if "JAVA_HOME" not in env:
            java_homes = find_installed_java()
            if java_homes:
                env["JAVA_HOME"] = os.path.dirname(os.path.dirname(java_homes[0][0]))
        
        result = run_command([self.sdkmanager, "emulator"], timeout=DEFAULT_PACKAGE_TIMEOUT, env=env)
        if result is None or result.returncode != 0:
            Logger.error("Failed to install emulator")
            return False
        
        # Check what's already there before attempting a system image, so a
        # retry after a prior timeout (or a manual install in the meantime)
        # doesn't re-attempt a package that's already present.
        installed = get_installed_sdk_packages(self.sdkmanager, env)
        
        x86_64_image = f"system-images;android-{self.sdk_version};google_apis;x86_64"
        arm64_image = f"system-images;android-{self.sdk_version};google_apis;arm64-v8a"
        
        if installed is not None and x86_64_image in installed:
            Logger.success(f"Already installed: {x86_64_image}")
            Logger.success("Emulator and system image installed")
            return True
        
        Logger.info("Installing system image")
        result = run_command(
            [self.sdkmanager, x86_64_image],
            timeout=package_timeout(x86_64_image), env=env
        )
        if result is None or result.returncode != 0:
            if installed is not None and arm64_image in installed:
                Logger.success(f"Already installed: {arm64_image}")
                Logger.success("Emulator and system image installed")
                return True
            
            Logger.warning("Failed to install x86_64 system image, trying arm64")
            result = run_command(
                [self.sdkmanager, arm64_image],
                timeout=package_timeout(arm64_image), env=env
            )
            if result is None or result.returncode != 0:
                Logger.error("Failed to install system image")
                return False
        
        Logger.success("Emulator and system image installed")
        return True
    
    def install(self) -> bool:
        Logger.step("Setting up Android SDK")
        
        if not self.install_cmdline():
            return False
        
        if not find_installed_java():
            Logger.error("Java required but not found")
            return False
        
        if not self.install_packages():
            return False
        
        if not self.accept_licenses():
            Logger.error("Licenses must be accepted")
            return False
        
        if not self.install_emulator():
            Logger.warning("Emulator installation skipped")
        
        self.adb.ensure()
        Logger.success("Android SDK setup complete")
        return True
    
    def get_ndk_path(self) -> Optional[str]:
        ndk_root = os.path.join(self.sdk_root, "ndk")
        if not os.path.isdir(ndk_root):
            return None
        
        versions = [d for d in os.listdir(ndk_root)
                   if os.path.isdir(os.path.join(ndk_root, d)) and d != "sources"]
        if not versions:
            return None
        
        versions.sort()
        return os.path.join(ndk_root, versions[-1])


class BuildSystem:
    def __init__(self, project_dir: str, config: Dict[str, Any]):
        self.project_dir = project_dir
        self.config = config
        self.android_dir = os.path.join(project_dir, "android")
        self.build_dir = os.path.join(project_dir, "build")
        self.web_build_dir = os.path.join(project_dir, "build-web")
        self.gradle = GradleManager(config)
    
    def ensure_sdk_and_gradle(self) -> Optional[AndroidSDKManager]:
        sdk = AndroidSDKManager(self.config)
        
        if sdk.find():
            os.environ["ANDROID_HOME"] = sdk.sdk_root
            
            if not sdk.licenses_accepted():
                if not sdk.accept_licenses():
                    Logger.error("Licenses must be accepted")
                    return None
            
            sdk.adb.ensure()
            
            if not os.path.exists(os.path.join(sdk.sdk_root, "emulator", "emulator")):
                sdk.install_emulator()
            
            local_props = os.path.join(self.android_dir, "local.properties")
            try:
                with open(local_props, 'w') as f:
                    f.write(f"sdk.dir={sdk.sdk_root}\n")
                    ndk = sdk.get_ndk_path()
                    if ndk:
                        f.write(f"ndk.dir={ndk}\n")
                        os.environ["ANDROID_NDK_HOME"] = ndk
            except IOError:
                pass
            
            self.gradle.generate_wrapper(self.project_dir)
            return sdk
        
        Logger.info("Android SDK not found")
        if secure_input("Install Android SDK", default="y",
                       validator=lambda x: x.lower() in ['y', 'n']).lower().startswith('y'):
            if sdk.install():
                os.environ["ANDROID_HOME"] = sdk.sdk_root
                local_props = os.path.join(self.android_dir, "local.properties")
                try:
                    with open(local_props, 'w') as f:
                        f.write(f"sdk.dir={sdk.sdk_root}\n")
                        ndk = sdk.get_ndk_path()
                        if ndk:
                            f.write(f"ndk.dir={ndk}\n")
                            os.environ["ANDROID_NDK_HOME"] = ndk
                except IOError:
                    pass
                
                self.gradle.generate_wrapper(self.project_dir)
                return sdk
        
        return None
    
    def build_linux(self) -> bool:
        os.makedirs(self.build_dir, exist_ok=True)
        
        result = run_command(["cmake", ".."], cwd=self.build_dir)
        if result is None or result.returncode != 0:
            Logger.error("CMake configuration failed")
            return False
        
        result = run_command(["make", f"-j{os.cpu_count() or 4}"], cwd=self.build_dir)
        if result is None or result.returncode != 0:
            Logger.error("Build failed")
            return False
        
        Logger.success("Linux build successful")
        return True
    
    def build_android(self, release: bool = False, aab: bool = False) -> bool:
        if not os.path.exists(self.android_dir):
            Logger.error("Not an Aroma project")
            return False
        
        sdk = self.ensure_sdk_and_gradle()
        if not sdk:
            return False
        
        self.gradle.remove_bad_wrapper_cache()
        version = self.gradle.resolve_version()
        self.gradle.write_wrapper_properties(self.android_dir, version)
        
        if release:
            keystore = SecureKeystoreManager(self.project_dir, self.config)
            if not keystore.exists():
                Logger.info("Release builds require signing")
                if secure_input("Setup signing", default="y",
                               validator=lambda x: x.lower() in ['y', 'n']).lower().startswith('y'):
                    if not keystore.setup():
                        return False
                else:
                    Logger.warning("Cannot build release without signing")
                    return False
        
        return self._run_gradle(release, aab)
    
    def build_web(self) -> bool:
        Logger.step("Configuring web build")
        
        if not shutil.which("emcmake") or not shutil.which("emmake"):
            Logger.error("Emscripten tools not found")
            return False
        
        os.makedirs(self.web_build_dir, exist_ok=True)
        
        result = run_command(["emcmake", "cmake", ".."], cwd=self.web_build_dir)
        if result is None or result.returncode != 0:
            Logger.error("Web CMake configuration failed")
            return False
        
        result = run_command(["emmake", "make", f"-j{os.cpu_count() or 4}"], cwd=self.web_build_dir)
        if result is None or result.returncode != 0:
            Logger.error("Web build failed")
            return False
        
        self._generate_web_index()
        Logger.success("Web build successful")
        return True
    
    def _run_gradle(self, release: bool, aab: bool) -> bool:
        self.gradle.remove_bad_wrapper_cache()
        version = self.gradle.resolve_version()
        self.gradle.write_wrapper_properties(self.android_dir, version)
        
        gradle_cmd = self.gradle.find_project_gradlew(self.project_dir)
        if not gradle_cmd:
            gradle_cmd = self.gradle.ensure(self.project_dir)
        
        if not gradle_cmd:
            Logger.error("Gradle not available")
            return False
        
        if release and aab:
            target = "bundleRelease"
        elif release:
            target = "assembleRelease"
        else:
            target = "assembleDebug"
        
        env = os.environ.copy()
        if "JAVA_HOME" not in env:
            java_home = find_java_for_gradle(version)
            if java_home:
                env["JAVA_HOME"] = java_home
            else:
                java_homes = find_installed_java()
                if java_homes:
                    env["JAVA_HOME"] = os.path.dirname(os.path.dirname(java_homes[0][0]))
        
        Logger.info(f"Running: {gradle_cmd} {target}")
        result = run_command([gradle_cmd, target], cwd=self.android_dir, timeout=600, env=env)
        
        if result is None or result.returncode != 0:
            Logger.error("Android build failed")
            return False
        
        self._show_output(release, aab)
        return True
    
    def _show_output(self, release: bool, aab: bool):
        base = os.path.join(self.android_dir, "app", "build", "outputs")
        paths = []
        
        if release and aab:
            paths = [
                os.path.join(base, "bundle", "release", "app-release.aab"),
                os.path.join(base, "bundle", "release", "app.aab"),
            ]
        elif release:
            paths = [
                os.path.join(base, "apk", "release", "app-release.apk"),
                os.path.join(base, "apk", "release", "app-release-unsigned.apk"),
            ]
        else:
            paths = [
                os.path.join(base, "apk", "debug", "app-debug.apk"),
                os.path.join(base, "apk", "debug", "app-debug-unsigned.apk"),
            ]
        
        for path in paths:
            if os.path.exists(path):
                Logger.success(f"Output: {path}")
                return
        
        Logger.error("Build output not found")
    
    def _generate_web_index(self):
        js_files = []
        for root, _, files in os.walk(self.web_build_dir):
            for f in files:
                if f.endswith('.js') and not f.endswith('.worker.js') and not f.endswith('.wasm.js'):
                    js_files.append(os.path.join(root, f))
        
        if not js_files:
            return
        
        js_path = sorted(js_files)[0]
        js_name = os.path.basename(js_path)
        project_name = os.path.basename(self.project_dir)
        
        html = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>{project_name}</title>
    <style>
        html, body {{ margin: 0; padding: 0; height: 100%; background: #111; }}
        canvas {{ display: block; width: 100%; height: 100%; }}
    </style>
</head>
<body>
    <canvas id="canvas"></canvas>
    <script>
        var Module = {{
            canvas: document.getElementById('canvas'),
            onRuntimeInitialized: function() {{
                var canvas = Module.canvas;
                function getCoords(e) {{
                    var rect = canvas.getBoundingClientRect();
                    return {{
                        x: (e.clientX - rect.left) * (canvas.width / rect.width),
                        y: (e.clientY - rect.top) * (canvas.height / rect.height)
                    }};
                }}
                window.addEventListener('mousemove', function(e) {{
                    var p = getCoords(e);
                    if (Module._aroma_emscripten_dispatch_mouse) {{
                        Module._aroma_emscripten_dispatch_mouse(0, p.x, p.y, 0);
                    }}
                }});
            }}
        }};
    </script>
    <script src="{js_name}"></script>
</body>
</html>"""
        
        with open(os.path.join(self.web_build_dir, "index.html"), 'w') as f:
            f.write(html)


class ProjectCreator:
    def __init__(self, templates_dir: str, config: Dict[str, Any]):
        self.templates_dir = templates_dir
        self.config = config
        self.gradle_mgr = GradleManager(config)
    
    def create(self, name: str = None) -> bool:
        Logger.step("Create New Project")
        
        project_name = name or secure_input("Project Name", min_length=1)
        target_dir = os.path.abspath(project_name)
        
        if os.path.exists(target_dir):
            Logger.error(f"Directory exists: {project_name}")
            return False
        
        default_pkg = f"com.example.{project_name.lower().replace('-', '')}"
        package = secure_input("Package Name", default=default_pkg,
                              validator=validate_package_name,
                              error_msg="Invalid package name")
        
        sdk_ver = resolve_value("android.sdk_version", self.config, "34")
        
        min_sdk = int(secure_input("Min SDK", default="24", validator=validate_int))
        target_sdk = int(secure_input("Target SDK", default=sdk_ver, validator=validate_int))
        compile_sdk = int(secure_input("Compile SDK", default=sdk_ver, validator=validate_int))
        
        if min_sdk > target_sdk:
            Logger.warning(f"Min SDK ({min_sdk}) > Target SDK ({target_sdk})")
            if not secure_input("Continue", default="n",
                               validator=lambda x: x.lower() in ['y', 'n']).lower().startswith('y'):
                return False
        
        if compile_sdk < target_sdk:
            Logger.warning(f"Compile SDK ({compile_sdk}) < Target SDK ({target_sdk})")
        
        print(f"\n{Colors.BOLD}Configuration:{Colors.ENDC}")
        print(f"  Name:        {project_name}")
        print(f"  Package:     {package}")
        print(f"  Min SDK:     {min_sdk}")
        print(f"  Target SDK:  {target_sdk}")
        print(f"  Compile SDK: {compile_sdk}")
        
        if not secure_input("\nCreate Project", default="y",
                           validator=lambda x: x.lower() in ['y', 'n']).lower().startswith('y'):
            return False
        
        return self._generate(target_dir, project_name, package,
                             str(min_sdk), str(target_sdk), str(compile_sdk))
    
    def _generate(self, target_dir: str, name: str, package: str,
                  min_sdk: str, target_sdk: str, compile_sdk: str) -> bool:
        Logger.step(f"Creating {name}")
        
        try:
            os.makedirs(target_dir, exist_ok=True)
            os.makedirs(os.path.join(target_dir, "src"), exist_ok=True)
            
            replacements = {
                "{{PROJECT_NAME}}": name,
                "{{PACKAGE_NAME}}": package,
                "{{MIN_SDK}}": min_sdk,
                "{{TARGET_SDK}}": target_sdk,
                "{{COMPILE_SDK}}": compile_sdk,
                "{{AROMA_ROOT}}": find_aroma_root(),
            }
            
            self._copy_tpl("app/main.c.tpl", os.path.join(target_dir, "src", "main.c"), replacements)
            self._copy_tpl("app/CMakeLists.txt.tpl", os.path.join(target_dir, "CMakeLists.txt"), replacements)
            
            android_src = os.path.join(self.templates_dir, "android")
            android_dst = os.path.join(target_dir, "android")
            
            if os.path.exists(android_src):
                if os.path.exists(android_dst):
                    shutil.rmtree(android_dst)
                shutil.copytree(android_src, android_dst)
                self._process_android_files(android_dst, replacements)
                self._setup_java_pkg(android_dst, package, replacements)
                self._create_local_props(android_dst)
            
            Logger.success(f"Project created: {name}")
            self._show_next(name)
            return True
        except Exception as e:
            Logger.error(f"Failed: {e}")
            return False
    
    def _copy_tpl(self, src_rel: str, dst: str, replacements: Dict[str, str]):
        src = os.path.join(self.templates_dir, src_rel)
        if not os.path.exists(src):
            return
        
        with open(src, 'r') as f:
            content = f.read()
        for k, v in replacements.items():
            content = content.replace(k, str(v))
        
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, 'w') as f:
            f.write(content)
    
    def _process_android_files(self, android_dir: str, replacements: Dict[str, str]):
        process_extensions = ['.gradle', '.gradle.kts', '.xml', '.properties', '.kt', '.java']
        
        for root, _, files in os.walk(android_dir):
            for file in files:
                path = os.path.join(root, file)
                
                if file.endswith('.tpl'):
                    new_path = path[:-4]
                    with open(path, 'r') as f:
                        content = f.read()
                    for k, v in replacements.items():
                        content = content.replace(k, str(v))
                    with open(new_path, 'w') as f:
                        f.write(content)
                    os.remove(path)
                else:
                    ext = os.path.splitext(file)[1]
                    if ext in process_extensions or file in ['build.gradle', 'build.gradle.kts',
                                                              'settings.gradle', 'settings.gradle.kts',
                                                              'AndroidManifest.xml', 'gradle.properties',
                                                              'local.properties']:
                        try:
                            with open(path, 'r') as f:
                                content = f.read()
                            
                            changed = False
                            for k, v in replacements.items():
                                if k in content:
                                    content = content.replace(k, str(v))
                                    changed = True
                            
                            if changed:
                                with open(path, 'w') as f:
                                    f.write(content)
                        except (IOError, UnicodeDecodeError):
                            pass
    
    def _setup_java_pkg(self, android_dir: str, package: str, replacements: Dict[str, str]):
        tpl = os.path.join(android_dir, "app", "src", "main", "java", "AromaHelper.java.tpl")
        if not os.path.exists(tpl):
            return
        
        pkg_path = package.replace('.', os.sep)
        java_dir = os.path.join(android_dir, "app", "src", "main", "java")
        final_dir = os.path.join(java_dir, pkg_path)
        os.makedirs(final_dir, exist_ok=True)
        
        with open(tpl, 'r') as f:
            content = f.read()
        for k, v in replacements.items():
            content = content.replace(k, str(v))
        
        with open(os.path.join(final_dir, "AromaHelper.java"), 'w') as f:
            f.write(content)
        os.remove(tpl)
        
        for root, dirs, _ in os.walk(java_dir, topdown=False):
            for d in dirs:
                dir_path = os.path.join(root, d)
                try:
                    if not os.listdir(dir_path):
                        os.rmdir(dir_path)
                except OSError:
                    pass
    
    def _create_local_props(self, android_dir: str):
        sdk = AndroidSDKManager(self.config)
        if sdk.find():
            with open(os.path.join(android_dir, "local.properties"), 'w') as f:
                f.write(f"sdk.dir={sdk.sdk_root}\n")
                ndk = sdk.get_ndk_path()
                if ndk:
                    f.write(f"ndk.dir={ndk}\n")
    
    def _show_next(self, name: str):
        print(f"\n{Colors.BOLD}Next steps:{Colors.ENDC}")
        print(f"  cd {name}")
        print("  aroma run linux")
        print("  aroma run android")
        print("  aroma build android --release")
        print("  aroma sign")


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


def start_emulator(sdk: AndroidSDKManager, adb_cmd: str) -> bool:
    emulator_path = os.path.join(sdk.sdk_root, "emulator", "emulator")
    if platform.system() == "Windows":
        emulator_path += ".exe"
    
    if not os.path.exists(emulator_path):
        Logger.error("Emulator not installed, installing...")
        if not sdk.install_emulator():
            return False
    
    result = run_command([adb_cmd, "devices"], capture_output=True)
    if result and "emulator-" in result.stdout:
        Logger.info("Emulator already running")
        return True
    
    result = run_command([sdk.avdmanager, "list", "avd", "-c"], capture_output=True)
    avds = []
    if result and result.returncode == 0:
        for line in result.stdout.strip().splitlines():
            line = line.strip()
            if line and not line.startswith('[') and not line.startswith('*'):
                avds.append(line)
    
    if avds:
        avd_name = avds[0]
        Logger.info(f"Using existing AVD: {avd_name}")
    else:
        avd_name = "aroma_emu"
        Logger.step(f"Creating AVD: {avd_name}")
        
        system_image = f"system-images;android-{sdk.sdk_version};google_apis;x86_64"
        result = run_command([sdk.sdkmanager, "--list"], capture_output=True)
        if result and "system-images" in result.stdout:
            if f"system-images;android-{sdk.sdk_version};google_apis;x86_64" not in result.stdout:
                Logger.info("x86_64 system image not available, trying arm64")
                system_image = f"system-images;android-{sdk.sdk_version};google_apis;arm64-v8a"
                if f"system-images;android-{sdk.sdk_version};google_apis;arm64-v8a" not in result.stdout:
                    Logger.error("No compatible system image found")
                    return False
        
        process = subprocess.Popen(
            [sdk.avdmanager, "create", "avd", "-n", avd_name, "-k", system_image, "--force"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        try:
            stdout, stderr = process.communicate(input="no\n", timeout=120)
            if process.returncode != 0:
                Logger.error(f"Failed to create AVD: {stderr}")
                return False
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            Logger.error("AVD creation timed out")
            return False
    
    Logger.step("Starting emulator")
    log_path = os.path.join(sdk.sdk_root, "emulator.log")
    log = open(log_path, "w")
    
    env = os.environ.copy()
    if "ANDROID_HOME" not in env:
        env["ANDROID_HOME"] = sdk.sdk_root
    if "ANDROID_SDK_ROOT" not in env:
        env["ANDROID_SDK_ROOT"] = sdk.sdk_root
    
    subprocess.Popen(
        [emulator_path, "-avd", avd_name, "-no-snapshot-load", "-no-boot-anim"],
        stdout=log, stderr=log, env=env
    )
    
    Logger.info("Waiting for device to be ready...")
    run_command([adb_cmd, "wait-for-device"], timeout=120)
    
    for i in range(120):
        result = run_command([adb_cmd, "shell", "getprop", "sys.boot_completed"], capture_output=True)
        if result and result.stdout.strip() == "1":
            break
        time.sleep(2)
    
    run_command([adb_cmd, "shell", "wm", "dismiss-keyguard"])
    
    Logger.success("Emulator ready")
    return True


def cmd_doctor(args):
    Logger.step("System Check")
    print(f"OS: {platform.system()} {platform.release()}")
    print(f"Arch: {platform.machine()}")
    print(f"Python: {sys.version}")
    
    config = load_config(getattr(args, 'config', None))
    
    java_list = find_installed_java()
    if java_list:
        for path, ver in java_list:
            Logger.success(f"Java {ver}: {path}")
        best_gradle = find_best_gradle_for_java(max(v[1] for v in java_list))
        if best_gradle:
            Logger.success(f"Compatible Gradle: {best_gradle} (will be downloaded on build)")
    else:
        Logger.warning("Java: not found")
    
    tools = ["cmake", "ninja", "gcc", "keytool"]
    for tool in tools:
        if shutil.which(tool):
            Logger.success(f"{tool}: available")
        else:
            Logger.warning(f"{tool}: not found")
    
    sdk = AndroidSDKManager(config)
    if sdk.find():
        Logger.success(f"Android SDK: {sdk.sdk_root}")
        if sdk.adb.is_installed():
            Logger.success("ADB: available")
        else:
            Logger.warning("ADB: not found")
        ndk = sdk.get_ndk_path()
        if ndk:
            Logger.success(f"Android NDK: {ndk}")
        else:
            Logger.warning("Android NDK: not found")
        if sdk.licenses_accepted():
            Logger.success("Licenses: accepted")
        else:
            Logger.warning("Licenses: not accepted")
            sdk.accept_licenses()
        
        emulator_path = os.path.join(sdk.sdk_root, "emulator", "emulator")
        if platform.system() == "Windows":
            emulator_path += ".exe"
        if os.path.exists(emulator_path):
            Logger.success("Emulator: installed")
        else:
            Logger.warning("Emulator: not installed")
    else:
        Logger.warning("Android SDK: not found")


def cmd_install_sdk(args):
    config = load_config(getattr(args, 'config', None))
    AndroidSDKManager(config).install()


def cmd_create(args):
    config = load_config(getattr(args, 'config', None))
    templates = os.path.join(os.path.dirname(os.path.realpath(__file__)), "templates")
    ProjectCreator(templates, config).create(getattr(args, 'name', None))


def cmd_build(args):
    config = load_config(getattr(args, 'config', None))
    Logger.step(f"Building for {args.platform}")
    bs = BuildSystem(os.getcwd(), config)
    
    if args.platform == "linux":
        bs.build_linux()
    elif args.platform == "android":
        bs.build_android(args.release, args.aab)
    elif args.platform == "web":
        bs.build_web()


def cmd_run(args):
    config = load_config(getattr(args, 'config', None))
    bs = BuildSystem(os.getcwd(), config)
    
    if args.platform == "linux":
        if not bs.build_linux():
            return
        _run_linux(os.getcwd())
    elif args.platform == "android":
        if not bs.build_android(False, False):
            return
        _run_android(os.getcwd(), args.emu, config)
    elif args.platform == "web":
        if not bs.build_web():
            return
        _serve_web(bs.web_build_dir, args.port)


def _run_linux(cwd: str):
    exe = os.path.join(cwd, "build", os.path.basename(cwd))
    if not os.path.exists(exe):
        build_dir = os.path.join(cwd, "build")
        if os.path.exists(build_dir):
            for f in os.listdir(build_dir):
                fp = os.path.join(build_dir, f)
                if os.access(fp, os.X_OK) and not f.endswith('.so'):
                    exe = fp
                    break
    
    if os.path.exists(exe):
        Logger.step(f"Launching {exe}")
        subprocess.run([exe])
    else:
        Logger.error("Executable not found")


def _run_android(cwd: str, use_emu: bool, config: Dict[str, Any]):
    sdk = AndroidSDKManager(config)
    if not sdk.find():
        Logger.error("Android SDK not found")
        return
    
    if not sdk.adb.ensure():
        return
    
    adb_cmd = sdk.adb.get_command()
    if not adb_cmd:
        Logger.error("ADB not found")
        return
    
    if use_emu:
        if not start_emulator(sdk, adb_cmd):
            return
    
    result = run_command([adb_cmd, "devices"], capture_output=True)
    if not result:
        return
    
    lines = result.stdout.strip().split('\n')
    devices = [l for l in lines[1:] if l.strip() and not l.startswith('*')]
    
    if not devices:
        Logger.error("No device connected")
        return
    
    authorized = any('\tdevice' in l or ' device' in l for l in devices)
    if not authorized:
        Logger.error("No authorized device")
        return
    
    apk = os.path.join(cwd, "android", "app", "build", "outputs", "apk", "debug", "app-debug.apk")
    if not os.path.exists(apk):
        Logger.error("APK not found")
        return
    
    Logger.step("Installing")
    result = run_command([adb_cmd, "install", "-r", apk], capture_output=True)
    if result is None or result.returncode != 0:
        Logger.error("Installation failed")
        if result and result.stderr:
            Logger.debug(result.stderr.strip())
        return
    
    pkg = find_package_name(cwd)
    if pkg:
        Logger.step(f"Launching {pkg}")
        run_command([adb_cmd, "shell", "am", "start", "-n", f"{pkg}/android.app.NativeActivity"])


def _serve_web(build_dir: str, port: int):
    Logger.step(f"Serving at http://localhost:{port}")
    subprocess.run(["python3", "-m", "http.server", str(port), "--directory", build_dir])


def cmd_sign(args):
    config = load_config(getattr(args, 'config', None))
    keystore = SecureKeystoreManager(os.getcwd(), config)
    
    if keystore.setup():
        Logger.success("Signing configured")
        Logger.info("  aroma build android --release")
        Logger.info("  aroma build android --release --aab")
    else:
        Logger.error("Failed to setup signing")


def main():
    parser = argparse.ArgumentParser(
        prog="aroma",
        description="AromaUI CLI Tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Examples:\n  aroma doctor\n  aroma create myapp\n  aroma build android\n  aroma run android"
    )
    
    parser.add_argument("--config", help="Config file path")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--no-color", action="store_true")
    
    subparsers = parser.add_subparsers(dest="command")
    subparsers.add_parser("doctor")
    subparsers.add_parser("install-sdk")
    
    p = subparsers.add_parser("create")
    p.add_argument("name", nargs="?")
    
    p = subparsers.add_parser("build")
    p.add_argument("platform", choices=["linux", "android", "web"], nargs="?", default="linux")
    p.add_argument("--release", action="store_true")
    p.add_argument("--aab", action="store_true")
    
    p = subparsers.add_parser("run")
    p.add_argument("platform", choices=["linux", "android", "web"], nargs="?", default="linux")
    p.add_argument("--emu", action="store_true")
    p.add_argument("--port", type=int, default=8000)
    
    subparsers.add_parser("sign")
    
    if len(sys.argv) == 1:
        parser.print_help()
        sys.exit(1)
    
    args = parser.parse_args()
    
    Logger.setup(verbose=args.verbose)
    if args.no_color:
        Colors.disable()
    
    cmds = {
        "doctor": cmd_doctor,
        "install-sdk": cmd_install_sdk,
        "create": cmd_create,
        "build": cmd_build,
        "run": cmd_run,
        "sign": cmd_sign,
    }
    
    if args.command in cmds:
        cmds[args.command](args)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()