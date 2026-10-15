"""Bindings generation (aroma bindings --language python|rust)."""

import glob
import os
import shutil
import sys

from aroma_output import Colors, Logger
from aroma_util import find_aroma_root


def load_bindings_generator(language="python"):
    """Import tools/generate_{python,rust}_bindings.py. None on failure."""
    cli_dir = os.path.dirname(os.path.realpath(__file__))
    tools_dir = os.path.dirname(cli_dir)
    if tools_dir not in sys.path:
        sys.path.insert(0, tools_dir)
    mod = {"python": "generate_python_bindings",
           "rust": "generate_rust_bindings"}.get(language, "generate_python_bindings")
    try:
        __import__(mod)
        return sys.modules[mod]
    except ImportError:
        return None


def load_python_generator():
    """Back-compat alias used by aroma_build (always the Python generator)."""
    return load_bindings_generator("python")


# Back-compat: aroma_build imports this name directly.
def load_bindings_generator_py():
    return load_python_generator()


def _load_generator(language="python"):
    """Import the bindings generator for a language. None + error on failure."""
    gen = load_bindings_generator(language)
    if gen is None:
        Logger.error(f"Cannot load the bindings generator (tools/generate_{language}_bindings.py)")
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


def _resolve_output(flag: str = None, language: str = "python") -> str:
    if flag:
        path = os.path.abspath(flag)
        if os.path.isdir(path):
            if language == "rust":
                return path
            path = os.path.join(path, "aroma.py")
        return path
    if language == "rust":
        return os.path.join(os.getcwd(), "bindings", "rust")
    return os.path.join(os.getcwd(), "bindings", "python", "aroma.py")


def _check_ctypesgen():
    if not shutil.which("gcc"):
        Logger.error("gcc is required (C preprocessor) but was not found on PATH")
        sys.exit(1)
    vendor_main = os.path.join(find_aroma_root(), "vendors", "ctypesgen",
                               "ctypesgen", "main.py")
    if not os.path.isfile(vendor_main):
        Logger.error("ctypesgen submodule is empty; run:\n"
                     "  git submodule update --init vendors/ctypesgen")
        sys.exit(1)


def _check_bindgen():
    if shutil.which("bindgen") is None:
        Logger.error("`bindgen` CLI not found; install it with:\n"
                     "  cargo install bindgen-cli\n"
                     "and make libclang available:\n"
                     "  apt install libclang-dev  # or export LIBCLANG_PATH=/usr/lib/llvm-19/lib")
        sys.exit(1)


def cmd_bindings(args):
    if getattr(args, "check", False) and getattr(args, "stats", False):
        Logger.error("--check and --stats are mutually exclusive (choose one)")
        sys.exit(2)

    language = getattr(args, "language", None) or "python"
    if language not in ("python", "rust"):
        Logger.error(f"Unknown language: {language} (choose python or rust)")
        sys.exit(2)

    gen = _load_generator(language)
    if gen is None:
        sys.exit(1)

    include_dir = _resolve_include_dir(getattr(args, "include_dir", None))
    if not os.path.isdir(include_dir):
        Logger.error(f"Include directory not found: {include_dir}")
        sys.exit(1)
    if not _has_headers(include_dir):
        Logger.error(f"No public headers (aroma*.h) in {include_dir}")
        sys.exit(1)

    if language == "rust":
        _check_bindgen()
    else:
        _check_ctypesgen()

    output = _resolve_output(getattr(args, "output", None), language)

    if language == "rust":
        Logger.step("Generate Rust bindings")
    else:
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

    if language == "rust":
        missing = [p for p in (os.path.join(output, "Cargo.toml"),
                               os.path.join(output, "src", "ffi.rs"),
                               os.path.join(output, "src", "lib.rs"))
                   if not os.path.isfile(p)]
        if missing:
            Logger.error(f"Generator reported success but missing: {', '.join(missing)}")
            sys.exit(1)
        Logger.success(f"Bindings written: {output} (Cargo.toml + src/ffi.rs + src/lib.rs)")
        print(f"\n{Colors.BOLD}Next steps:{Colors.ENDC}")
        print("  AROMA_LIB=build/src/libaroma.so cargo run --manifest-path bindings/rust/Cargo.toml --example hello")
        return

    raw_out = os.path.join(os.path.dirname(output), "aroma_raw.py")
    missing = [p for p in (output, raw_out) if not os.path.isfile(p)]
    if missing:
        Logger.error(f"Generator reported success but missing: {', '.join(missing)}")
        sys.exit(1)

    Logger.success(f"Bindings written: {output} (+ aroma_raw.py)")
    print(f"\n{Colors.BOLD}Next steps:{Colors.ENDC}")
    print("  LD_LIBRARY_PATH=build/src AROMA_LIB=build/src/libaroma.so python3 -c \"import aroma; print(aroma.is_available())\"")
