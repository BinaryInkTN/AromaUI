"""Android emulator lifecycle: AVD creation, boot, and app install/launch."""

import atexit
import os
import shutil
import subprocess
import sys
import time
from typing import List, Optional

from aroma_detector import Detector
from aroma_output import Colors, Logger
from aroma_util import get_connected_devices, run_command

_EMULATOR_PROCESS = None
_EMULATOR_SERIAL = None


def select_from_list(title: str, options: List[str], default_idx: int = 0) -> Optional[int]:
    if not options:
        return None
    
    print(f"\n{Colors.BOLD}{title}:{Colors.ENDC}")
    for i, option in enumerate(options):
        badge = ""
        option_lower = option.lower()
        if "25.2" in option_lower or "25.1" in option_lower or "24.0" in option_lower or "23.2" in option_lower:
            badge = f" {Colors.OKGREEN}[compatible]{Colors.ENDC}"
        elif "8.4" in option_lower or "8.5" in option_lower or "8.6" in option_lower or "8.7" in option_lower:
            badge = f" {Colors.OKGREEN}[stable]{Colors.ENDC}"
        print(f"  {i+1}. {option}{badge}")
    
    while True:
        try:
            choice = input(f"\nSelect (1-{len(options)}) [Q=quit]: ").strip()
            if choice.lower() == 'q':
                return None
            idx = int(choice) - 1
            if 0 <= idx < len(options):
                return idx
            print(f"{Colors.FAIL}Invalid selection. Enter 1-{len(options)}.{Colors.ENDC}")
        except ValueError:
            print(f"{Colors.FAIL}Invalid input. Enter a number.{Colors.ENDC}")
        except KeyboardInterrupt:
            print()
            sys.exit(0)


def create_avd(detector: Detector, avd_name: str = "aroma_emu") -> Optional[str]:
    avdmanager_cmd = detector.get_avdmanager_command()
    
    if avdmanager_cmd:
        sdk_versions = detector.detect_all_sdk()
        if not sdk_versions:
            Logger.error("No SDK platforms installed")
            return None
        
        emu_details = detector.detect_emulator_details()
        system_images = emu_details.get("system_images", [])
        
        if not system_images:
            Logger.error("No system images installed")
            return None
        
        x86_images = [img for img in system_images if "x86_64" in img and "google_apis" in img]
        arm_images = [img for img in system_images if "arm64-v8a" in img and "google_apis" in img]
        
        if x86_images:
            system_image = x86_images[0]
        elif arm_images:
            system_image = arm_images[0]
        else:
            system_image = system_images[0]
        
        Logger.step(f"Creating AVD with avdmanager: {avd_name}")
        Logger.info(f"System image: {system_image}")
        
        process = subprocess.Popen(
            [avdmanager_cmd, "create", "avd", "-n", avd_name, "-k", system_image, "--force"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
        )
        try:
            stdout, stderr = process.communicate(input="no\n", timeout=120)
            if process.returncode == 0:
                Logger.success(f"AVD created: {avd_name}")
                return avd_name
            Logger.error(f"avdmanager failed: {stderr}")
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            Logger.error("AVD creation timed out")
            return None
    
    Logger.step(f"Creating AVD manually: {avd_name}")
    
    sdk_versions = detector.detect_all_sdk()
    if not sdk_versions:
        Logger.error("No SDK platforms installed")
        return None
    
    emu_details = detector.detect_emulator_details()
    system_images = emu_details.get("system_images", [])
    
    if not system_images:
        Logger.error("No system images installed")
        return None
    
    x86_images = [img for img in system_images if "x86_64" in img]
    arm_images = [img for img in system_images if "arm64-v8a" in img]
    
    if x86_images:
        system_image = x86_images[0]
    elif arm_images:
        system_image = arm_images[0]
    else:
        system_image = system_images[0]
    
    Logger.info(f"Using system image: {system_image}")
    
    sys_img_path = os.path.join(detector.sdk_root, "system-images", system_image)
    if not os.path.isdir(sys_img_path):
        Logger.error(f"System image path not found: {sys_img_path}")
        return None
    
    avd_dir = os.path.expanduser(f"~/.android/avd/{avd_name}.avd")
    os.makedirs(avd_dir, exist_ok=True)
    
    ini_path = os.path.expanduser(f"~/.android/avd/{avd_name}.ini")
    with open(ini_path, 'w') as f:
        f.write(f"avd.ini.encoding=UTF-8\n")
        f.write(f"path={avd_dir}\n")
        f.write(f"path.rel=avd/{avd_name}.avd\n")
        f.write(f"target=android-{sdk_versions[0]}\n")
    
    arch = "x86_64" if "x86_64" in system_image else "arm64-v8a"
    
    config_path = os.path.join(avd_dir, "config.ini")
    with open(config_path, 'w') as f:
        f.write(f"AvdId={avd_name}\n")
        f.write(f"PlayStore.enabled=false\n")
        f.write(f"abi.type={arch}\n")
        f.write(f"avd.ini.displayname={avd_name}\n")
        f.write(f"avd.ini.encoding=UTF-8\n")
        f.write(f"disk.dataPartition.size=6442450944\n")
        f.write(f"fastboot.forceColdBoot=no\n")
        f.write(f"fastboot.forceFastBoot=yes\n")
        f.write(f"hw.accelerometer=yes\n")
        f.write(f"hw.audioInput=yes\n")
        f.write(f"hw.battery=yes\n")
        f.write(f"hw.camera.back=emulated\n")
        f.write(f"hw.camera.front=emulated\n")
        f.write(f"hw.cpu.arch={arch}\n")
        f.write(f"hw.cpu.ncore=4\n")
        f.write(f"hw.dPad=no\n")
        f.write(f"hw.device.manufacturer=Google\n")
        f.write(f"hw.device.name=pixel_6\n")
        f.write(f"hw.gps=yes\n")
        f.write(f"hw.gpu.enabled=yes\n")
        f.write(f"hw.gpu.mode=auto\n")
        f.write(f"hw.keyboard=yes\n")
        f.write(f"hw.lcd.density=420\n")
        f.write(f"hw.lcd.height=2400\n")
        f.write(f"hw.lcd.width=1080\n")
        f.write(f"hw.mainKeys=no\n")
        f.write(f"hw.ramSize=2048\n")
        f.write(f"hw.sdCard=yes\n")
        f.write(f"hw.sensors.orientation=yes\n")
        f.write(f"hw.sensors.proximity=yes\n")
        f.write(f"hw.trackBall=no\n")
        f.write(f"image.sysdir.1=system-images/{system_image}/\n")
        f.write(f"runtime.network.speed=full\n")
        f.write(f"tag.display=Google APIs\n")
        f.write(f"tag.id=google_apis\n")
        f.write(f"vm.heapSize=256\n")
    
    Logger.success(f"AVD created: {avd_name}")
    return avd_name

def start_emulator(detector: Detector) -> Optional[str]:
    global _EMULATOR_PROCESS, _EMULATOR_SERIAL
    
    emu_path = detector.get_emulator_command()
    if not emu_path:
        Logger.error("Emulator not found")
        return None
    
    adb_cmd = detector.get_adb_command()
    if not adb_cmd:
        Logger.error("ADB not found")
        return None
    
    existing_devices = get_connected_devices(adb_cmd)
    existing_emulators = [d for d in existing_devices if d.startswith("emulator-")]
    
    if existing_emulators:
        _EMULATOR_SERIAL = existing_emulators[0]
        Logger.info(f"Emulator already running: {_EMULATOR_SERIAL}")
        return _EMULATOR_SERIAL
    
    avd_dir = os.path.expanduser("~/.android/avd")
    avds = []
    
    if os.path.isdir(avd_dir):
        for item in os.listdir(avd_dir):
            if item.endswith(".avd"):
                avd_name = item.replace(".avd", "")
                ini_file = os.path.join(avd_dir, f"{avd_name}.ini")
                if os.path.exists(ini_file):
                    avds.append(avd_name)
            elif item.endswith(".ini"):
                avd_name = item.replace(".ini", "")
                avd_path = os.path.join(avd_dir, f"{avd_name}.avd")
                if os.path.exists(avd_path) and avd_name not in avds:
                    avds.append(avd_name)
    
    if not avds:
        Logger.warning("No AVDs found, creating one...")
        avd_name = create_avd(detector)
        if not avd_name:
            return None
        avds = [avd_name]
    
    if len(avds) > 1:
        idx = select_from_list("Select AVD:", avds)
        if idx is None:
            return None
        avd_name = avds[idx]
    else:
        avd_name = avds[0]
    
    Logger.step(f"Starting emulator: {avd_name}")
    log_path = os.path.join(detector.sdk_root, "emulator.log")
    log = open(log_path, "w")
    
    env = os.environ.copy()
    if "ANDROID_HOME" not in env:
        env["ANDROID_HOME"] = detector.sdk_root
    if "ANDROID_SDK_ROOT" not in env:
        env["ANDROID_SDK_ROOT"] = detector.sdk_root
    
    has_display = bool(os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"))
    emu_cmd = [emu_path, "-avd", avd_name, "-no-snapshot-load", "-no-boot-anim"]
    if not has_display:
        emu_cmd += ["-no-window", "-no-audio", "-gpu", "swiftshader_indirect"]
    _EMULATOR_PROCESS = subprocess.Popen(
        emu_cmd,
        stdout=log, stderr=log, env=env
    )
    
    Logger.info("Waiting for emulator to appear...")
    new_serial = None
    for i in range(60):
        current_devices = get_connected_devices(adb_cmd)
        current_emulators = [d for d in current_devices if d.startswith("emulator-")]
        new_emulators = [d for d in current_emulators if d not in existing_devices]
        if new_emulators:
            new_serial = new_emulators[0]
            break
        time.sleep(2)
    
    if not new_serial:
        Logger.error("Emulator did not appear")
        cleanup_emulator(adb_cmd)
        return None
    
    _EMULATOR_SERIAL = new_serial
    Logger.info(f"Emulator detected: {_EMULATOR_SERIAL}")
    Logger.info("Waiting for device to be ready...")
    
    run_command([adb_cmd, "-s", _EMULATOR_SERIAL, "wait-for-device"], timeout=120)
    
    for i in range(120):
        result = run_command([adb_cmd, "-s", _EMULATOR_SERIAL, "shell", "getprop", "sys.boot_completed"], capture_output=True)
        if result and result.stdout.strip() == "1":
            break
        time.sleep(2)
    
    run_command([adb_cmd, "-s", _EMULATOR_SERIAL, "shell", "wm", "dismiss-keyguard"])
    
    Logger.success(f"Emulator {_EMULATOR_SERIAL} ready")
    return _EMULATOR_SERIAL

def install_and_launch_app(adb_cmd: str, serial: str, apk_path: str, pkg: str) -> bool:
    Logger.step("Installing APK...")
    result = run_command([adb_cmd, "-s", serial, "install", "-r", apk_path], capture_output=True)
    if result is None or result.returncode != 0:
        Logger.error("Installation failed")
        if result and result.stderr:
            Logger.info(result.stderr.strip())
        return False
    
    Logger.success("APK installed")
    Logger.step("Launching app...")
    run_command([adb_cmd, "-s", serial, "shell", "am", "start",
                "-n", f"{pkg}/.AromaActivity"])
    
    run_command([adb_cmd, "-s", serial, "shell", "wm", "dismiss-keyguard"])
    Logger.success("App launched")
    return True


def cleanup_emulator(adb_cmd: str = None):
    global _EMULATOR_PROCESS, _EMULATOR_SERIAL
    
    if _EMULATOR_SERIAL and adb_cmd:
        Logger.info("Stopping emulator...")
        run_command([adb_cmd, "-s", _EMULATOR_SERIAL, "emu", "kill"], timeout=10)
        time.sleep(2)
    
    if _EMULATOR_PROCESS and _EMULATOR_PROCESS.poll() is None:
        try:
            _EMULATOR_PROCESS.terminate()
            _EMULATOR_PROCESS.wait(timeout=10)
        except subprocess.TimeoutExpired:
            _EMULATOR_PROCESS.kill()
            _EMULATOR_PROCESS.wait()
    
    _EMULATOR_PROCESS = None
    _EMULATOR_SERIAL = None


def final_cleanup():
    if _EMULATOR_SERIAL:
        adb_cmd = shutil.which("adb") or "adb"
        run_command([adb_cmd, "-s", _EMULATOR_SERIAL, "emu", "kill"], timeout=10)
        time.sleep(1)
    if _EMULATOR_PROCESS and _EMULATOR_PROCESS.poll() is None:
        _EMULATOR_PROCESS.terminate()
        try:
            _EMULATOR_PROCESS.wait(timeout=5)
        except subprocess.TimeoutExpired:
            _EMULATOR_PROCESS.kill()


atexit.register(final_cleanup)
