"""AromaUI CLI entry point (argument parsing and command dispatch)."""

import argparse
import sys

from aroma_bundle import cmd_bundle
from aroma_build import cmd_build, cmd_run
from aroma_constants import AROMA_INSTALL_DIR, AROMA_SDK_DIR
from aroma_doctor import cmd_doctor
from aroma_output import Colors, Logger
from aroma_project import cmd_create

def main():
    parser = argparse.ArgumentParser(
        prog="aroma",
        description="AromaUI CLI Tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"Examples:\n  aroma doctor\n  aroma create myapp\n  aroma build android\n  aroma run android --emu\n  aroma bundle linux deb appimage\n  aroma bundle linux --format deb,rpm,flatpak,snap,tar.gz\n\nPaths:\n  Install: {AROMA_INSTALL_DIR}\n  SDK:     {AROMA_SDK_DIR}"
    )
    
    parser.add_argument("--config", help="Config file path")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--no-color", action="store_true")
    
    subparsers = parser.add_subparsers(dest="command")
    subparsers.add_parser("doctor", help="Check installed components")
    
    p = subparsers.add_parser("create", help="Create new project")
    p.add_argument("name", nargs="?", help="Project name")
    
    p = subparsers.add_parser("build", help="Build project")
    p.add_argument("platform", choices=["linux", "android", "web"], nargs="?", default="linux")
    p.add_argument("--release", action="store_true")
    p.add_argument("--aab", action="store_true")
    p.add_argument("--sdk-version", help="Android SDK version")
    p.add_argument("--build-tools", help="Build tools version")
    p.add_argument("--ndk-version", help="NDK version")
    p.add_argument("--cmake-version", help="CMake version")
    
    p = subparsers.add_parser("run", help="Run project")
    p.add_argument("platform", choices=["linux", "android", "web"], nargs="?", default="linux")
    p.add_argument("--emu", action="store_true", help="Start/use emulator for Android")

    p = subparsers.add_parser(
        "bundle",
        help="Package a Linux build (AppImage, deb, rpm, Flatpak, Snap, archives)",
        description="Build (if needed) and package a Linux desktop app. "
                    "Formats mirror electron-builder/Tauri/CPack: "
                    "appimage, deb, rpm, flatpak, snap, tar.gz, tar.xz, tar.bz2, zip, dir.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Examples:\n"
               "  aroma bundle linux deb appimage\n"
               "  aroma bundle linux --format deb,rpm,tar.gz\n"
               "  aroma bundle linux --format all --out dist\n"
               "  aroma bundle linux appimage --appimagetool ./appimagetool.AppImage\n\n"
               "Metadata comes from aroma.json (project.* and bundle.*) with CLI overrides.",
    )
    p.add_argument("platform", choices=["linux"], nargs="?", default="linux")
    p.add_argument("bundle_formats", nargs="*", metavar="FORMAT",
                   help="Formats: appimage, deb, rpm, flatpak, snap, tar.gz, tar.xz, tar.bz2, zip, dir, all")
    p.add_argument("--format", action="append", dest="format", default=None,
                   help="Comma-separated formats (repeatable). Same values as FORMAT.")
    p.add_argument("--out", default="dist", help="Output directory (default: dist)")
    p.add_argument("--name", dest="bundle_name", help="Display name")
    p.add_argument("--package-name", help="Lowercase package name for deb/rpm")
    p.add_argument("--app-id", help="Reverse-DNS app ID (Flatpak/metainfo)")
    p.add_argument("--version", dest="bundle_version", help="Version (e.g. 1.0.0)")
    p.add_argument("--description", dest="bundle_description", help="Short/long description")
    p.add_argument("--maintainer", dest="bundle_maintainer", help="Maintainer 'Name <email>'")
    p.add_argument("--license", dest="bundle_license", help="License identifier (default: MIT)")
    p.add_argument("--homepage", dest="bundle_homepage", help="Homepage URL")
    p.add_argument("--icon", dest="bundle_icon", help="Path to .svg/.png icon")
    p.add_argument("--categories", dest="bundle_categories",
                   help="Freedesktop categories (default: Utility;)")
    p.add_argument("--depends", dest="bundle_depends",
                   help="Extra Debian Depends (default covers GL/EGL/GLES/curl/libc)")
    p.add_argument("--requires", dest="bundle_requires",
                   help="RPM Requires (default covers GL/EGL/curl/glibc)")
    p.add_argument("--arch", default="auto",
                   help="Target arch (auto, x86_64/amd64, aarch64/arm64, armv7l, i686)")
    p.add_argument("--debug", action="store_true",
                   help="Bundle a Debug build (default is Release)")
    p.add_argument("--rebuild", action="store_true", help="Force rebuild before bundling")
    p.add_argument("--skip-build", action="store_true",
                   help="Use existing build/ binary without rebuilding")
    p.add_argument("--keep-staging", action="store_true",
                   help="Keep the staged AppDir under dist/ for inspection")
    p.add_argument("--appimagetool", help="Path to appimagetool binary/AppImage")
    
    if len(sys.argv) == 1:
        parser.print_help()
        sys.exit(1)
    
    args = parser.parse_args()
    
    Logger.setup(verbose=args.verbose)
    if args.no_color:
        Colors.disable()
    
    if args.command == "doctor":
        cmd_doctor(args)
    elif args.command == "create":
        cmd_create(args)
    elif args.command == "build":
        cmd_build(args)
    elif args.command == "run":
        cmd_run(args)
    elif args.command == "bundle":
        cmd_bundle(args)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
