"""New-project scaffolding (aroma create)."""

import os
import platform
import re
import shutil
import stat
from typing import Any, Dict, List, Optional

from aroma_constants import PREFERRED_GRADLE_VERSIONS
from aroma_detector import Detector
from aroma_output import Colors, Logger
from aroma_util import (
    find_aroma_root,
    load_config,
    resolve_value,
    run_command,
    save_project_config,
    secure_input,
    validate_int,
    validate_package_name,
)

class ProjectCreator:
    def __init__(self, templates_dir: str, config: Dict[str, Any]):
        self.templates_dir = templates_dir
        self.config = config
    
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
                              error_msg="Invalid package name (e.g. com.example.myapp)")
        
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
                
                if not self._setup_gradle_wrapper(android_dst):
                    Logger.warning(
                        "Project created, but Gradle wrapper generation failed. "
                        "You can retry manually — see the error above for the fix."
                    )
            
            project_config = {
                "project": {
                    "name": name,
                    "package": package,
                    "version": "0.1.0",
                    "min_sdk": int(min_sdk),
                    "target_sdk": int(target_sdk),
                    "compile_sdk": int(compile_sdk),
                },
                "bundle": {
                    "description": f"{name} built with AromaUI",
                    "maintainer": "Aroma Developer <dev@example.com>",
                    "license": "MIT",
                    "categories": "Utility;",
                },
            }
            save_project_config(target_dir, project_config)
            
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
        java_dir = os.path.join(android_dir, "app", "src", "main", "java")
        pkg_path = package.replace('.', os.sep)
        final_dir = os.path.join(java_dir, pkg_path)
        os.makedirs(final_dir, exist_ok=True)

        for entry in sorted(os.listdir(java_dir)):
            src = os.path.join(java_dir, entry)
            if not os.path.isfile(src):
                continue
            if entry.endswith('.java.tpl'):
                dst_name = entry[:-4]
            elif entry.endswith('.java'):
                dst_name = entry
            else:
                continue
            with open(src, 'r') as f:
                content = f.read()
            for k, v in replacements.items():
                content = content.replace(k, str(v))
            with open(os.path.join(final_dir, dst_name), 'w') as f:
                f.write(content)
            os.remove(src)
        
        for root, dirs, _ in os.walk(java_dir, topdown=False):
            for d in dirs:
                dir_path = os.path.join(root, d)
                try:
                    if not os.listdir(dir_path):
                        os.rmdir(dir_path)
                except OSError:
                    pass
    
    def _create_local_props(self, android_dir: str):
        detector = Detector()
        sdk_root = detector.sdk_root
        if os.path.isdir(sdk_root):
            with open(os.path.join(android_dir, "local.properties"), 'w') as f:
                f.write(f"sdk.dir={sdk_root}\n")
                ndk_versions = detector.detect_all_ndk()
                if ndk_versions:
                    ndk_dir = os.path.join(sdk_root, "ndk", ndk_versions[0])
                    if os.path.isdir(ndk_dir):
                        f.write(f"ndk.dir={ndk_dir}\n")
    
    @staticmethod
    def _pick_gradle_version(installed: List[str]) -> Optional[str]:
        if not installed:
            return None
        
        def version_key(v: str):
            parts = []
            for p in re.split(r'[.\-]', v):
                try:
                    parts.append(int(p))
                except ValueError:
                    parts.append(-1)
            return tuple(parts)
        
        preferred_installed = [v for v in installed if v in PREFERRED_GRADLE_VERSIONS]
        if preferred_installed:
            return sorted(preferred_installed, key=version_key)[-1]
        return sorted(installed, key=version_key)[-1]
    
    def _setup_gradle_wrapper(self, android_dir: str) -> bool:
        detector = Detector()
        installed_versions = detector.detect_all_gradle()
        version = self._pick_gradle_version(installed_versions)
        
        if not version:
            Logger.error(
                "No Gradle distribution found under "
                f"{os.path.join(detector.install_path, 'gradle')} "
                "(expected e.g. gradle-8.7/bin/gradle). Install a Gradle "
                "distribution there, or generate the wrapper manually with "
                "'gradle wrapper --gradle-version <version>' inside the "
                "project's android/ directory."
            )
            return False
        
        gradle_bin = os.path.join(detector.install_path, "gradle", f"gradle-{version}", "bin", "gradle")
        if platform.system() == "Windows":
            gradle_bin_bat = gradle_bin + ".bat"
            if os.path.isfile(gradle_bin_bat):
                gradle_bin = gradle_bin_bat
        
        if not os.path.isfile(gradle_bin):
            Logger.error(f"Gradle binary not found at expected path: {gradle_bin}")
            return False
        
        gradle_home = os.path.join(detector.install_path, "gradle", f"gradle-{version}")
        wrapper_props_dir = os.path.join(android_dir, "gradle", "wrapper")
        os.makedirs(wrapper_props_dir, exist_ok=True)
        
        wrapper_props_path = os.path.join(wrapper_props_dir, "gradle-wrapper.properties")
        with open(wrapper_props_path, 'w') as f:
            f.write(f"distributionBase=GRADLE_USER_HOME\n")
            f.write(f"distributionPath=wrapper/dists\n")
            f.write(f"distributionUrl=file\\://{gradle_home}\n")
            f.write(f"networkTimeout=10000\n")
            f.write(f"zipStoreBase=GRADLE_USER_HOME\n")
            f.write(f"zipStorePath=wrapper/dists\n")
        
        gradlew_path = os.path.join(android_dir, "gradlew")
        if not os.path.exists(gradlew_path):
            Logger.step("Downloading gradlew script...")
            result = run_command(
                [gradle_bin, "wrapper", "--gradle-version", version, "--no-daemon"],
                cwd=android_dir,
                capture_output=True,
                timeout=120,
            )
            if result is None or result.returncode != 0:
                detail = (result.stderr.strip() if result and result.stderr else None) or "unknown error"
                Logger.error(f"Failed to generate gradlew: {detail}")
                with open(wrapper_props_path, 'w') as f:
                    f.write(f"distributionBase=GRADLE_USER_HOME\n")
                    f.write(f"distributionPath=wrapper/dists\n")
                    f.write(f"distributionUrl=https\\://services.gradle.org/distributions/gradle-{version}-bin.zip\n")
                    f.write(f"networkTimeout=10000\n")
                    f.write(f"zipStoreBase=GRADLE_USER_HOME\n")
                    f.write(f"zipStorePath=wrapper/dists\n")
                return False
        
        if os.path.isfile(gradle_home):
            with open(wrapper_props_path, 'w') as f:
                f.write(f"distributionBase=GRADLE_USER_HOME\n")
                f.write(f"distributionPath=wrapper/dists\n")
                f.write(f"distributionUrl=file\\://{gradle_home}\n")
                f.write(f"networkTimeout=10000\n")
                f.write(f"zipStoreBase=GRADLE_USER_HOME\n")
                f.write(f"zipStorePath=wrapper/dists\n")
        
        if platform.system() != "Windows":
            st = os.stat(gradlew_path)
            os.chmod(gradlew_path, st.st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        
        Logger.success(f"Gradle wrapper ready (Gradle {version}, project-local, no download needed)")
        return True
    
    def _show_next(self, name: str):
        print(f"\n{Colors.BOLD}Next steps:{Colors.ENDC}")
        print(f"  cd {name}")
        print("  aroma run linux")
        print("  aroma run android --emu")
        print("  aroma build android --release")
        print("  aroma bundle linux deb appimage   # package for Linux")


def cmd_create(args):
    config = load_config(getattr(args, 'config', None))
    templates = os.path.join(os.path.dirname(os.path.realpath(__file__)), "templates")
    ProjectCreator(templates, config).create(getattr(args, 'name', None))
