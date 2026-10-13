"""Python bindings generation (aroma bindings)."""

import glob
import os
import shutil
import sys

from aroma_output import Colors, Logger
from aroma_util import find_aroma_root


def load_bindings_generator():
    """Import tools/generate_python_bindings.py. Returns None on failure."""
    cli_dir = os.path.dirname(os.path.realpath(__file__))
    tools_dir = os.path.dirname(cli_dir)
    if tools_dir not in sys.path:
        sys.path.insert(0, tools_dir)
    try:
        import generate_python_bindings as gen
        return gen
    except ImportError:
        return None


def _load_generator():
    """Import tools/generate_python_bindings.py. Returns None on failure."""
    gen = load_bindings_generator()
    if gen is None:
        Logger.error("Cannot load the bindings generator (tools/generate_python_bindings.py)")
    return gen


def _has_headers(include_dir: str) -> bool:
    return bool(glob.glob(os.path.join(include_dir, "aroma*.h")))


def _resolve_include_dir(flag: str = None) -> str:
    if flag:
        return os.path.abspath(flag)
    cwd_include = os.path.join(os.getcwd(), "include")
    if _has_headers(cwd_include):
        return cwd_include
    return os.path.join(find_aroma_root(), "include")


def _resolve_output(flag: str = None) -> str:
    if flag:
        path = os.path.abspath(flag)
        if os.path.isdir(path):
            path = os.path.join(path, "aroma.py")
        return path
    return os.path.join(os.getcwd(), "bindings", "python", "aroma.py")


def cmd_bindings(args):
    if getattr(args, "check", False) and getattr(args, "stats", False):
        Logger.error("--check and --stats are mutually exclusive (choose one)")
        sys.exit(2)

    gen = _load_generator()
    if gen is None:
        sys.exit(1)

    include_dir = _resolve_include_dir(getattr(args, "include_dir", None))
    if not os.path.isdir(include_dir):
        Logger.error(f"Include directory not found: {include_dir}")
        sys.exit(1)
    if not _has_headers(include_dir):
        Logger.error(f"No public headers (aroma*.h) in {include_dir}")
        sys.exit(1)

    if not shutil.which("gcc"):
        Logger.error("gcc is required (C preprocessor) but was not found on PATH")
        sys.exit(1)
    vendor_main = os.path.join(find_aroma_root(), "vendors", "ctypesgen",
                               "ctypesgen", "main.py")
    if not os.path.isfile(vendor_main):
        Logger.error("ctypesgen submodule is empty; run:\n"
                     "  git submodule update --init vendors/ctypesgen")
        sys.exit(1)

    output = _resolve_output(getattr(args, "output", None))

    Logger.step("Generate Python bindings")
    print(f"  Headers: {include_dir}")
    print(f"  Output:  {output}")

    gen_argv = ["--include-dir", include_dir, "--output", output]
    if getattr(args, "check", False):
        gen_argv.append("--check")
    if getattr(args, "stats", False):
        Logger.info("Stats only; no files will be written")
        gen_argv.append("--stats")
        try:
            rc = gen.main(gen_argv)
        except Exception as e:
            Logger.error(f"Binding stats failed: {e}")
            sys.exit(1)
        sys.exit(rc if isinstance(rc, int) else 0)

    try:
        rc = gen.main(gen_argv)
    except Exception as e:
        Logger.error(f"Binding generation failed: {e}")
        sys.exit(1)
    if isinstance(rc, int) and rc != 0:
        sys.exit(rc)

    raw_out = os.path.join(os.path.dirname(output), "aroma_raw.py")
    missing = [p for p in (output, raw_out) if not os.path.isfile(p)]
    if missing:
        Logger.error(f"Generator reported success but missing: {', '.join(missing)}")
        sys.exit(1)

    Logger.success(f"Bindings written: {output} (+ aroma_raw.py)")
    print(f"\n{Colors.BOLD}Next steps:{Colors.ENDC}")
    print("  LD_LIBRARY_PATH=build/src AROMA_LIB=build/src/libaroma.so python3 -c \"import aroma; print(aroma.is_available())\"")
