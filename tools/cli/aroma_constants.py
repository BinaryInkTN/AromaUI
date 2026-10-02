"""Shared paths, toolchain versions, and validation tables for the aroma CLI."""

import os
import re

AROMA_DEFAULT_INSTALL_DIR = ".aroma"
AROMA_DEFAULT_SDK_DIR = "Android/Sdk"

def get_default_install_path() -> str:
    home = os.path.expanduser("~")
    aroma_home = os.environ.get("AROMA_HOME")
    if aroma_home:
        return aroma_home
    return os.path.join(home, AROMA_DEFAULT_INSTALL_DIR)

def get_default_sdk_path() -> str:
    home = os.path.expanduser("~")
    android_home = os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT")
    if android_home:
        return android_home
    return os.path.join(home, AROMA_DEFAULT_SDK_DIR)

AROMA_INSTALL_DIR = get_default_install_path()
AROMA_SDK_DIR = get_default_sdk_path()

JAVA_SEARCH_DIRS = [
    "/usr/lib/jvm",
    "/Library/Java/JavaVirtualMachines",
]

SDK_SEARCH_PATHS = [
    AROMA_SDK_DIR,
    "~/Android/Sdk",
    "/usr/lib/android-sdk",
    "/Library/Android/sdk",
    "C:\\Android\\Sdk",
]

AROMA_COMPATIBLE_NDK_VERSIONS = [
    "25.2.9519653",
    "25.1.8937393",
    "24.0.8215888",
    "23.2.8568313",
]

PREFERRED_GRADLE_VERSIONS = ["8.4", "8.5", "8.6", "8.7"]

_PACKAGE_SEGMENT_RE = re.compile(r'^[a-z][a-z0-9_]*$')
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
