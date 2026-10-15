#!/usr/bin/env python3
"""Generate Rust bindings for AromaUI using ctypesgen JSON IR.

The bindings themselves are produced from the real C parser in the
``vendors/ctypesgen`` submodule (same backend as
``tools/generate_python_bindings.py``); this script only selects the public
headers, stages sanitized copies (``static inline`` bodies reduced to
prototypes, binary font-data headers stubbed), asks ctypesgen for its JSON
description (``--output-language json``), and converts that IR to a Rust
crate (``Cargo.toml`` + ``build.rs`` + ``src/ffi.rs`` + ``src/lib.rs`` +
``examples/hello.rs``).

Usage:
    python3 tools/generate_rust_bindings.py [--include-dir include] [--output bindings/rust]
    python3 tools/generate_rust_bindings.py --output /tmp/aroma-rust --check
    python3 tools/generate_rust_bindings.py --stats

The generated crate is a build artifact (git-ignored under ``bindings/``,
never committed). Requires gcc (C preprocessor) and the ctypesgen submodule
(``git submodule update --init vendors/ctypesgen``). ``cargo check`` is used
for ``--check`` only when cargo is available; otherwise the check verifies
that the expected symbols were emitted.
"""

import argparse
import ast
import glob
import json
import os
import re
import shutil
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DEFAULT_INCLUDE = os.path.join(ROOT, "include")
DEFAULT_OUTPUT = os.path.join(ROOT, "bindings", "rust")

STUB_HEADERS = frozenset({
    "aroma_ubuntu_font.h",
    "aroma_material_font.h",
    "aroma_chrome_icon_font.h",
})
SKIP_HEADERS = frozenset({"aroma.h"}) | STUB_HEADERS

RUST_KEYWORDS = frozenset({
    "as", "break", "const", "continue", "crate", "else", "enum", "extern",
    "false", "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod",
    "move", "mut", "pub", "ref", "return", "self", "Self", "static",
    "struct", "super", "trait", "true", "type", "unsafe", "use", "where",
    "while", "async", "await", "dyn", "abstract", "become", "box", "do",
    "final", "macro", "override", "priv", "typeof", "unsized", "virtual",
    "yield", "try", "union",
})


def _ensure_pygen():
    tools_dir = os.path.dirname(os.path.abspath(__file__))
    if tools_dir not in sys.path:
        sys.path.insert(0, tools_dir)
    try:
        import generate_python_bindings as pygen
        return pygen
    except ImportError as exc:
        print("error: cannot import generate_python_bindings: %s" % exc,
              file=sys.stderr)
        return None


def find_inline_helpers(include_dir):
    """Names of ``static inline aroma_*`` helpers (no symbols in the .so)."""
    names = set()
    pats = [os.path.join(include_dir, "aroma*.h"),
            os.path.join(include_dir, "widgets", "*.h")]
    files = [h for pat in pats for h in glob.glob(pat)]
    for path in files:
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError:
            continue
        for m in re.finditer(r"\bstatic\s+inline\b", text):
            # Look ahead for the first aroma_* identifier before '('.
            tail = text[m.end():m.end() + 2000]
            fm = re.search(r"\b(aroma_\w+)\s*\(", tail)
            if not fm:
                continue
            # Make sure we did not cross the function body of a previous
            # match: the identifier must appear before any '{' or ';'.
            prefix = tail[:fm.start()]
            if "{" in prefix or ";" in prefix:
                # The identifier belongs to a later declaration; still take
                # it if the prefix looks like a declaration tail.
                if len(prefix) > 600:
                    continue
            names.add(fm.group(1))
    return names


def sanitize_ident(name, fallback="arg"):
    if not name:
        return fallback
    name = re.sub(r"\W", "_", name)
    if not name or name[0].isdigit():
        name = "_" + name
    if name in RUST_KEYWORDS:
        return "r#" + name
    return name


def c_simple_to_rust(name, signed, longs):
    try:
        signed = bool(signed) if isinstance(signed, bool) else bool(signed)
    except Exception:
        signed = True
    try:
        longs = int(longs)
    except Exception:
        longs = 0
    key = (name, signed, longs)
    table = {
        ("void", True, 0): "c_void",
        ("int", True, 0): "c_int",
        ("int", False, 0): "c_uint",
        ("int", True, 1): "c_long",
        ("int", False, 1): "c_ulong",
        ("char", True, 0): "c_char",
        ("char", False, 0): "c_uchar",
        ("short", True, 0): "c_short",
        ("short", False, 0): "c_ushort",
        ("float", True, 0): "c_float",
        ("double", True, 0): "c_double",
        ("double", True, 1): "c_double",  # long double: best effort
        ("int", True, -1): "c_short",
        ("int", False, -1): "c_ushort",
        ("int", True, 2): "i64",
        ("int", False, 2): "u64",
        ("_Bool", True, 0): "bool",
        ("bool", True, 0): "bool",
        ("size_t", True, 0): "usize",
        ("ssize_t", True, 0): "isize",
        ("ptrdiff_t", True, 0): "isize",
        ("wchar_t", True, 0): "c_int",
        ("va_list", True, 0): "*mut c_void",
        ("int8_t", True, 0): "i8",
        ("__int8_t", True, 0): "i8",
        ("int16_t", True, 0): "i16",
        ("__int16_t", True, 0): "i16",
        ("int32_t", True, 0): "i32",
        ("__int32_t", True, 0): "i32",
        ("int64_t", True, 0): "i64",
        ("__int64_t", True, 0): "i64",
        ("uint8_t", False, 0): "u8",
        ("__uint8_t", False, 0): "u8",
        ("uint16_t", False, 0): "u16",
        ("__uint16_t", False, 0): "u16",
        ("uint32_t", False, 0): "u32",
        ("__uint32_t", False, 0): "u32",
        ("uint64_t", False, 0): "u64",
        ("__uint64_t", False, 0): "u64",
        ("uint8_t", True, 0): "u8",
        ("uint16_t", True, 0): "u16",
        ("uint32_t", True, 0): "u32",
        ("uint64_t", True, 0): "u64",
        ("int8_t", False, 0): "i8",
        ("int16_t", False, 0): "i16",
        ("int32_t", False, 0): "i32",
        ("int64_t", False, 0): "i64",
    }
    if key in table:
        return table[key]
    # Fallback: raw C name may be a typedef emitted elsewhere.
    if name in ("unsigned", "signed"):
        return "c_int"
    return name


def eval_int_expr(node, known=None):
    """Evaluate a ctypesgen count expression to int, or None."""
    if node is None:
        return None
    k = node.get("Klass")
    if k == "ConstantExpressionNode":
        v = node.get("value")
        if isinstance(v, bool):
            return int(v)
        if isinstance(v, (int, float)):
            return int(v)
        if isinstance(v, str):
            try:
                return int(ast.literal_eval(v))
            except Exception:
                pass
            if known and v in known:
                return known[v]
        return None
    if k == "IdentifierExpressionNode":
        name = node.get("name")
        if known and name in known:
            return known[name]
        return None
    if k == "BinaryExpressionNode":
        op = node.get("name")
        left = eval_int_expr(node.get("left"), known)
        right = eval_int_expr(node.get("right"), known)
        if left is None or right is None:
            return None
        try:
            if op in ("addition", "add"):
                return left + right
            if op in ("subtraction", "sub"):
                return left - right
            if op in ("multiplication", "mul"):
                return left * right
            if op in ("division", "div") and right != 0:
                return left // right
            if op in ("left_shift", "lshift", "shift_left"):
                return left << right
            if op in ("right_shift", "rshift", "shift_right"):
                return left >> right
        except Exception:
            return None
        return None
    if k == "UnaryExpressionNode":
        return eval_int_expr(node.get("child") or node.get("operand"), known)
    return None


def ctype_to_rust(ctype, ctx="value"):
    """Convert a ctypesgen JSON ctype node to a Rust type string."""
    if ctype is None:
        return "c_void"
    k = ctype.get("Klass")
    if k == "CtypesSimple":
        name = ctype.get("name")
        signed = ctype.get("signed", True)
        longs = ctype.get("longs", 0)
        if name == "void":
            if ctx in ("return",):
                return "()"
            return "c_void"
        return c_simple_to_rust(name, signed, longs)
    if k == "CtypesSpecial":
        name = ctype.get("name")
        if name == "String":
            return "*const c_char" if ctx != "return" else "*mut c_char"
        if name == "c_char_p":
            return "*const c_char" if ctx != "return" else "*mut c_char"
        if name == "c_void_p":
            return "*mut c_void"
        if name == "c_ubyte":
            return "u8"
        return name
    if k == "CtypesTypedef":
        return sanitize_ident(ctype.get("name"), fallback="c_void")
    if k == "CtypesPointer":
        dest = ctype.get("destination")
        quals = ctype.get("qualifiers") or []
        if dest and dest.get("Klass") == "CtypesFunction":
            return fn_ptr_to_rust(dest, optional=True)
        if dest and dest.get("Klass") == "CtypesSimple" and dest.get("name") == "void":
            return "*const c_void" if "const" in quals else "*mut c_void"
        inner = ctype_to_rust(dest, ctx="pointee")
        if inner == "()":
            inner = "c_void"
        if inner == "c_void":
            return "*const c_void" if "const" in quals else "*mut c_void"
        return ("*const " if "const" in quals else "*mut ") + inner
    if k == "CtypesArray":
        base = ctype_to_rust(ctype.get("base"), ctx="pointee")
        # C array parameters decay to pointers; emitting `[T; N]` by value
        # in an `extern "C" fn` is not FFI-safe (and rustc warns). Struct
        # fields and statics keep the real array type.
        if ctx == "arg":
            return "*mut %s" % base
        n = eval_int_expr(ctype.get("count")) or 0
        return "[%s; %d]" % (base, n)
    if k == "CtypesStruct":
        tag = ctype.get("tag") or "Anon"
        return sanitize_ident(str(tag))
    if k == "CtypesEnum":
        tag = ctype.get("tag") or "Anon"
        return sanitize_ident(str(tag))
    if k == "CtypesFunction":
        return fn_ptr_to_rust(ctype, optional=False)
    if k == "CtypesBitfield":
        return ctype_to_rust(ctype.get("base"), ctx=ctx)
    return "c_void"


def fn_ptr_to_rust(fn_node, optional=True):
    args = fn_node.get("argtypes") or []
    parts = []
    for a in args:
        # Skip a lone void parameter if it ever appears.
        if (a.get("Klass") == "CtypesSimple" and a.get("name") == "void"):
            continue
        parts.append(ctype_to_rust(a, ctx="arg"))
    ret = ctype_to_rust(fn_node.get("restype"), ctx="return")
    sig = "extern \"C\" fn(%s)" % (", ".join(parts))
    if ret not in ("()", "c_void"):
        sig += " -> %s" % ret
    if optional:
        return "Option<%s>" % sig
    return sig


def rust_string_literal(py_str):
    out = ['"']
    for ch in py_str:
        o = ord(ch)
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif ch == "\0":
            out.append("\\0")
        elif 0x20 <= o < 0x7F:
            out.append(ch)
        elif o <= 0xFFFF:
            out.append("\\u{%x}" % o)
        else:
            out.append("\\u{%x}" % o)
    out.append('"')
    return "".join(out)


def infer_int_const_type(value_str, known_u32):
    try:
        v = ast.literal_eval(value_str)
    except Exception:
        v = None
    if isinstance(v, bool):
        return "bool"
    if isinstance(v, int) and not isinstance(v, bool):
        if 0 <= v <= 0x7FFFFFFF:
            return "c_int"
        if 0 <= v <= 0xFFFFFFFF:
            return "c_uint"
        if -0x80000000 <= v <= 0x7FFFFFFFFFFFFFFF:
            return "i64"
        return "u64"
    if isinstance(value_str, str):
        for name in known_u32:
            if re.search(r"\b%s\b" % re.escape(name), value_str):
                return "c_uint"
        if re.search(r"\bUINT32_MAX\b", value_str):
            return "c_uint"
    return "c_int"


CARGO_TOML_TEMPLATE = """\
# Generated by tools/generate_rust_bindings.py (do not edit by hand).
# Regenerate: python3 tools/generate_rust_bindings.py --output {output_rel}
[package]
name = "aroma"
version = "0.0.1"
edition = "2021"
description = "AromaUI Rust bindings over libaroma (generated, do not edit)"
license = "MIT"
links = "aroma"

[lib]
name = "aroma"
crate-type = ["rlib"]
"""

BUILD_RS_TEMPLATE = """\
// Generated by tools/generate_rust_bindings.py (do not edit by hand).
// Links the compiled `aroma` crate against libaroma.so.
use std::env;
use std::path::PathBuf;

fn candidates() -> Vec<PathBuf> {
    let mut out = Vec::new();
    if let Ok(p) = env::var("AROMA_LIB") {
        let pb = PathBuf::from(&p);
        if pb.is_file() {
            if let Some(dir) = pb.parent() {
                out.push(dir.to_path_buf());
            }
        } else if pb.is_dir() {
            out.push(pb);
        } else {
            println!("cargo:warning=AROMA_LIB={} does not exist; ignoring", pb.display());
        }
    }
    // Crate lives at <root>/bindings/rust; the desktop build lands at
    // <root>/build/src/libaroma.so (or <cwd>/build/aroma-lib/src).
    if let Ok(cur) = env::var("CARGO_MANIFEST_DIR") {
        let cur = PathBuf::from(cur);
        let root = cur.join("..").join("..");
        for rel in [
            "build/src",
            "build/aroma-lib/src",
            "build/python-bindings/src",
            "src",
        ] {
            out.push(root.join(rel));
        }
        // Also try the current workspace (cargo run from an app dir).
        if let Ok(cwd) = env::current_dir() {
            for rel in ["build/src", "build/aroma-lib/src"] {
                out.push(cwd.join(rel));
            }
        }
    }
    out
}

/// Vendored static deps live next to the lib dir inside the CMake build
/// tree: <build>/vendors/freetype, <build>/vendors/sqlite3, <build>/lib.
/// Given a dir containing libaroma.a, return the build root that holds it.
fn build_root_for(lib_dir: &PathBuf) -> Option<PathBuf> {
    // lib_dir is usually <build>/src; the build root is its parent.
    let build = lib_dir.parent()?.to_path_buf();
    if build.join("vendors").is_dir() || build.join("lib").is_dir() {
        return Some(build);
    }
    None
}

fn main() {
    // Prefer the shared lib: its DT_NEEDED entries resolve freetype/GLES
    // automatically, so no extra link flags are required.
    for dir in candidates() {
        let so = dir.join("libaroma.so");
        let so1 = dir.join("libaroma.so.1");
        if so.is_file() || so1.is_file() {
            println!("cargo:rustc-link-search=native={}", dir.display());
            println!("cargo:rustc-link-lib=dylib=aroma");
            println!("cargo:rerun-if-env-changed=AROMA_LIB");
            return;
        }
    }
    // No shared lib? Fall back to the default static build
    // (<build>/src/libaroma.a). A static archive carries no dependency
    // info, so link its transitive deps explicitly -- same set the CMake
    // test targets link (see build/tests/CMakeFiles/aroma_tests.dir/link.txt).
    for dir in candidates() {
        if !dir.join("libaroma.a").is_file() {
            continue;
        }
        println!("cargo:rustc-link-search=native={}", dir.display());
        if let Some(build) = build_root_for(&dir) {
            let ft = build.join("vendors").join("freetype");
            let sq = build.join("vendors").join("sqlite3");
            let glps = build.join("lib");
            if ft.is_dir() {
                println!("cargo:rustc-link-search=native={}", ft.display());
            }
            if sq.is_dir() {
                println!("cargo:rustc-link-search=native={}", sq.display());
            }
            if glps.is_dir() {
                println!("cargo:rustc-link-search=native={}", glps.display());
            }
        }
        println!("cargo:rustc-link-lib=static=aroma");
        println!("cargo:rustc-link-lib=static=freetype");
        println!("cargo:rustc-link-lib=static=sqlite3");
        println!("cargo:rustc-link-lib=static=GLPS");
        for lib in ["GLESv2", "EGL", "vulkan", "X11", "asound", "curl", "pthread", "rt", "m", "dl"] {
            println!("cargo:rustc-link-lib={}", lib);
        }
        println!("cargo:rerun-if-env-changed=AROMA_LIB");
        return;
    }
    // Nothing found: keep going so the error comes from the linker,
    // but tell the user exactly how to fix it.
    println!("cargo:warning=libaroma not found (looked for libaroma.so and libaroma.a in candidate dirs). Build it first:");
    println!("cargo:warning=  cmake -S . -B build && cmake --build build   # static, works as-is");
    println!("cargo:warning=  cmake -S . -B build -DBUILD_SHARED_LIBS=ON && cmake --build build   # shared");
    println!("cargo:warning=or set AROMA_LIB=/path/to/libaroma.so (or its directory)");
    println!("cargo:rustc-link-lib=dylib=aroma");
    println!("cargo:rerun-if-env-changed=AROMA_LIB");
}
"""

LIB_RS_TEMPLATE = """\
//! Safe wrappers over [`ffi`] (generated, do not edit by hand).
//!
//! [`ffi`] is the raw `extern "C"` translation of the public C headers
//! (see `src/ffi.rs`). The helpers below are Rust ports of the C
//! `static inline` conveniences from `aroma_ui.h` (`aroma_ui_init`,
//! `aroma_ui_button`, ...). Those inlines have no symbols in `libaroma.so`,
//! so -- like the Python bindings -- we call the same `*_impl` /
//! `aroma_*_create` entry points the C versions use.

pub mod ffi;
pub use ffi::*;

use std::ffi::CString;
use std::os::raw::{c_char, c_int, c_void};

fn cstr(s: &str) -> CString {
    CString::new(s).expect("interior NUL in string")
}

/// Button click callback: `true` = handled (matches the C convention).
pub type ClickCallback = Option<extern "C" fn(*mut ffi::AromaNode, *mut c_void) -> bool>;
/// Generic redraw callback used by widget `setup_events` helpers.
pub type RedrawCallback = Option<extern "C" fn(*mut c_void)>;

extern "C" fn redraw_trampoline(user_data: *mut c_void) {
    unsafe { ffi::aroma_ui_request_redraw(user_data) };
}

/// Port of C `aroma_ui_init()` (calls `aroma_ui_init_impl`).
pub unsafe fn ui_init() -> bool {
    ffi::aroma_ui_init_impl()
}

/// Port of C `aroma_ui_shutdown()`.
pub unsafe fn ui_shutdown() {
    ffi::aroma_ui_shutdown_impl()
}

/// Port of C `aroma_ui_is_running()`.
pub unsafe fn ui_is_running() -> bool {
    ffi::aroma_ui_is_running_impl()
}

/// Port of C `aroma_ui_process_events()`.
pub unsafe fn ui_process_events() {
    ffi::aroma_ui_process_events_impl()
}

/// Port of C `aroma_ui_render(window)`.
///
/// The portable equivalent renders all dirty windows (same as the Python
/// bindings); the argument is kept for API parity.
pub unsafe fn ui_render(_window: *mut ffi::AromaWindow) {
    ffi::aroma_ui_render_all_windows_impl()
}

/// Port of C `aroma_ui_create_window()`.
pub unsafe fn ui_create_window(title: &str, width: c_int, height: c_int) -> *mut ffi::AromaWindow {
    let t = cstr(title);
    ffi::aroma_ui_create_window_impl(t.as_ptr() as *const c_char, width, height)
}

/// Port of C `aroma_ui_destroy_window()`.
pub unsafe fn ui_destroy_window(window: *mut ffi::AromaWindow) {
    ffi::aroma_ui_destroy_window_impl(window)
}

/// Port of C `aroma_ui_button()` (without the `font: NULL` default dance).
pub unsafe fn ui_button(
    parent: *mut ffi::AromaNode,
    text: &str,
    x: c_int,
    y: c_int,
    width: c_int,
    height: c_int,
    on_click: ClickCallback,
    user_data: *mut c_void,
    font: *mut ffi::AromaFont,
) -> *mut ffi::AromaNode {
    let t = cstr(text);
    let btn = ffi::aroma_button_create(parent, t.as_ptr() as *const c_char, x, y, width, height);
    if !btn.is_null() {
        if let Some(cb) = on_click {
            // `aroma_button_set_on_click` takes the callback by value.
            ffi::aroma_button_set_on_click(btn, Some(cb), user_data);
        }
        ffi::aroma_button_setup_events(btn, Some(redraw_trampoline), std::ptr::null_mut());
        if !font.is_null() {
            ffi::aroma_button_set_font(btn, font);
        }
    }
    btn
}

/// Port of C `aroma_ui_label()` (`style` defaults to 1 like the Python port).
pub unsafe fn ui_label(
    parent: *mut ffi::AromaNode,
    text: &str,
    x: c_int,
    y: c_int,
    style: ffi::AromaLabelStyle,
    font: *mut ffi::AromaFont,
) -> *mut ffi::AromaNode {
    let t = cstr(text);
    let label = ffi::aroma_label_create(parent, t.as_ptr() as *const c_char, x, y, style);
    if !label.is_null() && !font.is_null() {
        ffi::aroma_label_set_font(label, font);
    }
    label
}

/// Port of C `aroma_ui_container()`.
pub unsafe fn ui_container(
    parent: *mut ffi::AromaNode,
    x: c_int,
    y: c_int,
    width: c_int,
    height: c_int,
) -> *mut ffi::AromaNode {
    ffi::aroma_container_create(parent, x, y, width, height)
}
"""

HELLO_RS_TEMPLATE = """\
//! Hello-world port of docs/Python-Bindings.md (generated, do not edit).
//! Run: AROMA_LIB=build/src/libaroma.so cargo run --example hello
use aroma::{ffi, ui_button, ui_container, ui_create_window, ui_init, ui_is_running, ui_label, ui_process_events, ui_render};
use std::ffi::CString;
use std::os::raw::c_void;

extern "C" fn on_tap(_node: *mut ffi::AromaNode, _ud: *mut c_void) -> bool {
    println!("tapped");
    true
}

fn main() {
    unsafe {
        assert!(ui_init(), "aroma_ui_init failed");
        let theme = ffi::aroma_theme_create_material_blue_dark();
        ffi::aroma_theme_set_global(&theme);
        let font_path = CString::new("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf").unwrap();
        let font = ffi::aroma_font_create(font_path.as_ptr(), 20);
        assert!(!font.is_null(), "font load failed");
        let window = ui_create_window("Hello", 400, 600);
        assert!(!window.is_null(), "window creation failed");
        let root = window as *mut ffi::AromaNode;
        let bx = ui_container(root, 0, 0, 400, 600);
        ui_label(bx, "Hello, Rust", 40, 60, 1, font);
        ui_button(bx, "Tap me", 40, 140, 200, 52, Some(on_tap), std::ptr::null_mut(), font);
        while ui_is_running() {
            ui_process_events();
            ui_render(window);
        }
    }
}
"""

README_TEMPLATE = """\
# AromaUI Rust bindings (generated, do not edit)

Generated by `tools/generate_rust_bindings.py` on top of the vendored
`ctypesgen` C parser (same backend as the Python bindings) from
{N_HEADERS} public headers. Regenerate:

    git submodule update --init vendors/ctypesgen
    python3 tools/generate_rust_bindings.py --output {output_rel}
    python3 tools/generate_rust_bindings.py --output {output_rel} --check

Requires `libaroma.so` (hint: `AROMA_LIB=/path/to/libaroma.so`).
If only the default static build exists (`build/src/libaroma.a`), `build.rs`
links that instead -- no extra step needed.

## Layout

- `src/ffi.rs` — raw `extern "C"` bindings (structs, enums, functions).
- `src/lib.rs` — safe ports of the C `static inline` helpers from
  `aroma_ui.h` (`ui_init`, `ui_button`, `ui_label`, `ui_container`, ...).
  Those inlines have no symbols in the `.so`; like the Python bindings we
  call the same `*_impl` / `aroma_*_create` entry points.
- `examples/hello.rs` — hello-world port of `docs/Python-Bindings.md`.

## Build

```bash
cargo build -p aroma
AROMA_LIB=$PWD/build/src/libaroma.so cargo run --example hello
```
"""


def convert_items(items, inline_helpers):
    """Split ctypesgen JSON IR into Rust-ready groups."""
    structs = []   # (rust_name, variety, fields|None)
    enums = []     # (rust_name, [(variant, expr_node)])
    typedefs = []  # (name, ctype)
    funcs = []
    variables = []
    constants = []
    macros = []
    for it in items:
        t = it.get("type")
        if t in ("struct", "union"):
            structs.append((it.get("name"), t, it.get("fields")))
        elif t == "enum":
            enums.append((it.get("name"), it.get("fields") or []))
        elif t == "typedef":
            typedefs.append(it)
        elif t == "function":
            funcs.append(it)
        elif t == "variable":
            variables.append(it)
        elif t == "constant":
            constants.append(it)
        elif t == "macro":
            macros.append(it)
        # macro_function / undef / libraries: handled separately / skipped.
    return structs, enums, typedefs, funcs, variables, constants, macros


def build_rust_module(items, n_headers, inline_helpers):
    structs, enums, typedefs, funcs, variables, constants, macros = convert_items(items, inline_helpers)

    # Map anonymous struct/union/enum tags to their public typedef names.
    anon_to_pub = {}
    for td in typedefs:
        ctype = td.get("ctype") or {}
        if ctype.get("Klass") in ("CtypesStruct", "CtypesEnum"):
            tag = ctype.get("tag")
            if tag and str(tag).startswith("anon_"):
                anon_to_pub.setdefault(str(tag), td.get("name"))

    # ---- structs / unions (dedup, prefer definitions with fields) ----
    # ctypesgen emits both raw struct/union items (tagged, e.g. "AromaRect"
    # or "anon_3") and typedef items wrapping them (e.g. Aroma3DCamera ->
    # anon_3). Anonymous tags covered by a public typedef are emitted once
    # under the public name; the union-vs-struct keyword comes from the raw
    # item's variety for the same tag.
    struct_defs = {}  # rust_name -> (variety, fields|None)
    raw_variety = {}
    for tag, variety, fields in structs:
        raw_variety[str(tag)] = variety
    for tag, variety, fields in structs:
        if str(tag).startswith("anon_") and str(tag) in anon_to_pub:
            continue  # covered by the typedef below
        name = sanitize_ident(str(tag))
        if name in struct_defs:
            if fields and not struct_defs[name][1]:
                struct_defs[name] = (variety, fields)
        else:
            struct_defs[name] = (variety, fields)
    for td in typedefs:
        ctype = td.get("ctype") or {}
        if ctype.get("Klass") == "CtypesStruct":
            tag = str(ctype.get("tag") or "")
            variety = raw_variety.get(tag, "struct")
            name = sanitize_ident(td.get("name"))
            members = ctype.get("members")
            fields = None
            if members:
                fields = [{"name": n, "ctype": c} for n, c in members]
            if name in struct_defs:
                if fields and not struct_defs[name][1]:
                    struct_defs[name] = (variety, fields)
            else:
                struct_defs[name] = (variety, fields)

    # ---- enums (dedup) ----
    enum_defs = {}  # rust_name -> [(variant, expr_node)]
    for td in typedefs:
        ctype = td.get("ctype") or {}
        if ctype.get("Klass") == "CtypesEnum":
            name = sanitize_ident(td.get("name"))
            members = ctype.get("enumerators") or []
            conv = []
            for m in members:
                if isinstance(m, (list, tuple)) and len(m) == 2:
                    conv.append((m[0], m[1]))
            if name not in enum_defs or (not enum_defs[name] and conv):
                enum_defs[name] = conv
    for tag, fields in enums:
        if str(tag).startswith("anon_") and str(tag) in anon_to_pub:
            continue
        name = sanitize_ident(str(tag))
        conv = []
        for f in fields or []:
            conv.append((f.get("name"), f.get("ctype")))
        if name in enum_defs:
            if not enum_defs[name] and conv:
                enum_defs[name] = conv
        else:
            enum_defs[name] = conv

    # ---- other typedefs ----
    alias_defs = []  # (name, ctype) excluding struct/enum/function
    fn_typedefs = []  # (name, fn_node)
    plain_struct_typedefs = set(struct_defs.keys())
    for td in typedefs:
        ctype = td.get("ctype") or {}
        k = ctype.get("Klass")
        name = td.get("name")
        if k in ("CtypesStruct", "CtypesEnum"):
            continue  # emitted as struct/enum above
        if k == "CtypesFunction":
            fn_typedefs.append((name, ctype))
        elif (k == "CtypesPointer" and (ctype.get("destination") or {}).get("Klass") == "CtypesFunction"):
            fn_typedefs.append((name, ctype.get("destination")))
        else:
            alias_defs.append((name, ctype))
    # Dedup aliases by name (first wins; prefer non-trivial?).
    seen_alias = set()
    uniq_aliases = []
    for name, ctype in alias_defs:
        if name in seen_alias:
            continue
        seen_alias.add(name)
        uniq_aliases.append((name, ctype))
    alias_defs = uniq_aliases

    # ---- functions ----
    inline_set = set(inline_helpers)
    extern_fns = []
    skipped_inline = []
    skipped_variadic = []
    seen_fn = set()
    for fn in funcs:
        name = fn.get("name")
        if not name or name in seen_fn:
            continue
        seen_fn.add(name)
        if name in inline_set:
            skipped_inline.append(name)
            continue
        if fn.get("variadic"):
            skipped_variadic.append(name)
            continue
        extern_fns.append(fn)

    # ---- variables (only real globals) ----
    extern_vars = []
    seen_var = set()
    for v in variables:
        name = v.get("name")
        if not name or name in seen_var:
            continue
        seen_var.add(name)
        if not (name.startswith("g_") or name.startswith("aroma_")):
            continue  # struct-member noise from unparsable headers
        extern_vars.append(v)

    # ---- constants / macros ----
    emitted_consts = set()
    for variants in enum_defs.values():
        for vname, _ in variants:
            emitted_consts.add(vname)
    const_items = []  # (name, expr_str)
    for c in constants:
        name = c.get("name")
        if not name or name in emitted_consts:
            continue
        emitted_consts.add(name)
        const_items.append((name, c.get("value")))
    macro_items = []  # (name, value)
    skipped_fn_macros = 0
    for m in macros:
        name = m.get("name")
        if not name or name in emitted_consts:
            continue
        emitted_consts.add(name)
        macro_items.append((name, m.get("value")))
    # macro_function items are function-like macros (AROMA_DP, ...): skip.
    for it in items:
        if it.get("type") == "macro_function":
            skipped_fn_macros += 1

    # Known u32 consts for type inference (collect literal-evaluated big ones).
    known_u32 = {"UINT32_MAX"}
    for name, val in const_items + macro_items:
        try:
            v = ast.literal_eval(val) if isinstance(val, str) else None
        except Exception:
            v = None
        if isinstance(v, int) and not isinstance(v, bool) and v > 0x7FFFFFFF:
            known_u32.add(name)

    # ================= emit =================
    L = []
    L.append("// Generated by tools/generate_rust_bindings.py (do not edit by hand).")
    L.append("// %d public headers -> %d functions, %d structs/unions, %d enums." % (
        n_headers, len(extern_fns), len(struct_defs), len(enum_defs)))
    L.append("// Regenerate: python3 tools/generate_rust_bindings.py")
    L.append("#![allow(non_camel_case_types, non_snake_case, non_upper_case_globals, dead_code, unused_imports)]")
    L.append("use std::os::raw::{c_char, c_double, c_float, c_int, c_long, c_short, c_uchar, c_uint, c_ulong, c_ushort, c_void};")
    L.append("")
    L.append("// Fixed-width aliases always available (some headers also typedef these).")
    L.append("#[allow(dead_code)]")
    L.append("pub type __aroma_u8 = u8;")
    L.append("")

    # Structs / unions, sorted for deterministic output.
    for name in sorted(struct_defs):
        variety, fields = struct_defs[name]
        rname = sanitize_ident(name)
        if not fields:
            L.append("/// Opaque type (forward declaration in C).")
            L.append("#[repr(C)]")
            L.append("pub struct %s { _private: [u8; 0] }" % rname)
            L.append("")
            continue
        kw = "union" if variety == "union" else "struct"
        L.append("#[repr(C)]")
        L.append("#[derive(Copy, Clone)]")
        L.append("pub %s %s {" % (kw, rname))
        seen_fields = set()
        for f in fields:
            fname = sanitize_ident(f.get("name") or "field", fallback="field")
            base = fname
            i = 0
            while fname in seen_fields:
                i += 1
                fname = "%s_%d" % (base, i)
            seen_fields.add(fname)
            ftype = f.get("ctype")
            if ftype and ftype.get("Klass") == "CtypesFunction":
                rtype = fn_ptr_to_rust(ftype, optional=True)
            else:
                rtype = ctype_to_rust(ftype)
            L.append("    pub %s: %s," % (fname, rtype))
        L.append("}")
        L.append("")

    # Enums: type alias + per-variant constants.
    for name in sorted(enum_defs):
        variants = enum_defs[name]
        rname = sanitize_ident(name)
        L.append("pub type %s = c_int;" % rname)
        for vname, expr in variants or []:
            rv = sanitize_ident(vname)
            val = "0"
            if isinstance(expr, dict):
                k = expr.get("Klass")
                if k == "ConstantExpressionNode":
                    v = expr.get("value")
                    try:
                        lit = ast.literal_eval(repr(v) if isinstance(v, str) and not v.lstrip("-").isdigit() else v)
                        val = str(int(lit))
                    except Exception:
                        try:
                            val = str(int(v))
                        except Exception:
                            val = "0"
                elif k == "IdentifierExpressionNode":
                    val = sanitize_ident(expr.get("name") or "0")
                elif k == "BinaryExpressionNode":
                    fmt = expr.get("format") or "(%s + %s)"
                    left = expr_to_rust_operand(expr.get("left"))
                    right = expr_to_rust_operand(expr.get("right"))
                    try:
                        val = fmt % (left, right)
                    except Exception:
                        val = "0"
                    # ctypesgen formats enums as "(A + 1)"; outer parens are
                    # unnecessary in Rust and trigger warnings.
                    val = val.strip()
                    if val.startswith("(") and val.endswith(")"):
                        val = val[1:-1].strip()
                else:
                    val = "0"
            L.append("pub const %s: %s = %s;" % (rv, rname, val))
        L.append("")

    # Function-pointer typedefs.
    for name, fn_node in sorted(fn_typedefs):
        rname = sanitize_ident(name)
        L.append("pub type %s = %s;" % (rname, fn_ptr_to_rust(fn_node, optional=True)))
    if fn_typedefs:
        L.append("")

    # Plain typedefs.
    for name, ctype in sorted(alias_defs):
        rname = sanitize_ident(name)
        if rname in struct_defs or rname in enum_defs:
            continue
        rtype = ctype_to_rust(ctype)
        if rtype == rname:
            continue
        L.append("pub type %s = %s;" % (rname, rtype))
    if alias_defs:
        L.append("")

    # Integer / string constants.
    for name, val in const_items + macro_items:
        rname = sanitize_ident(name)
        if not isinstance(val, str):
            continue
        # Try string literal first.
        try:
            lit = ast.literal_eval(val)
        except Exception:
            lit = None
        if isinstance(lit, str):
            # ctypesgen double-escapes C string escapes (C "\x1B" arrives as
            # the 7-char Python string "\\x1B..."); decode to real chars.
            if "\\" in lit:
                try:
                    lit = lit.encode("utf-8").decode("unicode_escape")
                except Exception:
                    pass
            L.append("pub const %s: &str = %s;" % (rname, rust_string_literal(lit)))
            continue
        if isinstance(lit, bool):
            L.append("pub const %s: bool = %s;" % (rname, "true" if lit else "false"))
            continue
        if isinstance(lit, (int, float)) and not isinstance(lit, bool):
            if isinstance(lit, float):
                L.append("pub const %s: c_double = %r;" % (rname, lit))
            else:
                t = infer_int_const_type(val, known_u32)
                L.append("pub const %s: %s = %s;" % (rname, t, str(lit)))
            continue
        # Expression referencing other consts (e.g. (X + 1), UINT32_MAX).
        # Outer parens are stripped: unnecessary in Rust, rustc warns.
        if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", val.strip()):
            t = infer_int_const_type(val, known_u32)
            L.append("pub const %s: %s = %s;" % (rname, t, sanitize_ident(val.strip())))
            continue
        if re.fullmatch(r"\(?[A-Za-z_][A-Za-z0-9_]*(\s*[-+*/|&^<>]+\s*\(?[A-Za-z0-9_]+\)?)+\)?", val.strip()):
            t = infer_int_const_type(val, known_u32)
            expr = val.strip()
            if expr.startswith("(") and expr.endswith(")"):
                expr = expr[1:-1].strip()
            L.append("pub const %s: %s = %s;" % (rname, t, expr))
            continue
        if re.fullmatch(r"\(?\d[\dxXa-fA-F_]*\)?", val.strip()):
            t = infer_int_const_type(val, known_u32)
            expr = val.strip()
            if expr.startswith("(") and expr.endswith(")"):
                expr = expr[1:-1].strip()
            L.append("pub const %s: %s = %s;" % (rname, t, expr))
            continue
        if re.fullmatch(r"0x[0-9a-fA-F]+", val.strip()):
            L.append("pub const %s: c_uint = %s;" % (rname, val.strip()))
            continue
        # Anything else (casts, sizeof, string pastes): keep as comment.
        L.append("// skipped macro %s = %s (non-integer expression)" % (rname, val.strip()[:160]))
    if const_items or macro_items:
        L.append("")

    # Functions.
    L.append("extern \"C\" {")
    for fn in sorted(extern_fns, key=lambda f: f.get("name")):
        fname = sanitize_ident(fn.get("name"))
        args = fn.get("args") or []
        parts = []
        for i, a in enumerate(args):
            aname = sanitize_ident(a.get("identifier") or ("arg%d" % i), fallback=("arg%d" % i))
            # Function-pointer params arrive as CtypesFunction (by value in
            # JSON); they are really pointers.
            if a.get("Klass") == "CtypesFunction":
                atype = fn_ptr_to_rust(a, optional=True)
            else:
                atype = ctype_to_rust(a, ctx="arg")
                if atype == "()":
                    continue
            parts.append("%s: %s" % (aname, atype))
        ret = ctype_to_rust(fn.get("return"), ctx="return")
        if ret in ("()", "c_void"):
            L.append("    pub fn %s(%s);" % (fname, ", ".join(parts)))
        else:
            L.append("    pub fn %s(%s) -> %s;" % (fname, ", ".join(parts), ret))
    # Globals.
    for v in sorted(extern_vars, key=lambda v: v.get("name")):
        vname = sanitize_ident(v.get("name"))
        vtype = ctype_to_rust(v.get("ctype"), ctx="value")
        L.append("    pub static %s: %s;" % (vname, vtype))
    L.append("}")
    L.append("")
    if skipped_inline:
        L.append("// %d C `static inline` helpers (no .so symbol) are not declared here;" % len(skipped_inline))
        L.append("// safe ports live in `src/lib.rs` (ui_init, ui_button, ...).")
    if skipped_variadic:
        L.append("// Skipped %d variadic C functions (no stable Rust extern): %s." % (
            len(skipped_variadic), ", ".join(sorted(skipped_variadic)[:8])))
    if skipped_fn_macros:
        L.append("// Skipped %d function-like macros (use the safe wrappers instead)." % skipped_fn_macros)

    stats = {
        "functions": len(extern_fns),
        "structs": len(struct_defs),
        "enums": len(enum_defs),
        "typedefs": len(alias_defs) + len(fn_typedefs),
        "constants": len(const_items),
        "macros": len(macro_items),
        "variables": len(extern_vars),
        "skipped_inline": len(skipped_inline),
        "skipped_variadic": len(skipped_variadic),
    }
    return "\n".join(L) + "\n", stats


def expr_to_rust_operand(node):
    if not isinstance(node, dict):
        return "0"
    k = node.get("Klass")
    if k == "ConstantExpressionNode":
        try:
            return str(int(node.get("value")))
        except Exception:
            return "0"
    if k == "IdentifierExpressionNode":
        return sanitize_ident(node.get("name") or "0")
    # Nested binary: best effort.
    if k == "BinaryExpressionNode":
        fmt = node.get("format") or "(%s + %s)"
        try:
            return fmt % (expr_to_rust_operand(node.get("left")),
                          expr_to_rust_operand(node.get("right")))
        except Exception:
            return "0"
    return "0"


def run_ctypesgen_json(pygen, staged, staged_dir):
    tmp = tempfile.mkdtemp(prefix="aroma-rustgen-")
    js = os.path.join(tmp, "ir.json")
    try:
        ok = pygen.run_ctypesgen(staged, staged_dir, js, ["--output-language", "json"])
        if not ok or not os.path.isfile(js):
            return None, None
        with open(js, encoding="utf-8") as f:
            items = json.load(f)
        return items, tmp
    except Exception as exc:
        print("error: ctypesgen failed: %s" % exc, file=sys.stderr)
        shutil.rmtree(tmp, ignore_errors=True)
        return None, None


def write_crate(output_dir, ffi_src, n_headers, stats):
    os.makedirs(os.path.join(output_dir, "src"), exist_ok=True)
    os.makedirs(os.path.join(output_dir, "examples"), exist_ok=True)
    rel = os.path.relpath(output_dir, ROOT)
    with open(os.path.join(output_dir, "Cargo.toml"), "w", encoding="utf-8") as f:
        f.write(CARGO_TOML_TEMPLATE.format(output_rel=rel))
    with open(os.path.join(output_dir, "build.rs"), "w", encoding="utf-8") as f:
        f.write(BUILD_RS_TEMPLATE)
    with open(os.path.join(output_dir, "src", "ffi.rs"), "w", encoding="utf-8") as f:
        f.write(ffi_src)
    with open(os.path.join(output_dir, "src", "lib.rs"), "w", encoding="utf-8") as f:
        f.write(LIB_RS_TEMPLATE)
    with open(os.path.join(output_dir, "examples", "hello.rs"), "w", encoding="utf-8") as f:
        f.write(HELLO_RS_TEMPLATE)
    with open(os.path.join(output_dir, "README.md"), "w", encoding="utf-8") as f:
        f.write(README_TEMPLATE.format(N_HEADERS=n_headers, output_rel=rel))
    return [os.path.join(output_dir, p) for p in
            ("Cargo.toml", "build.rs", "src/ffi.rs", "src/lib.rs",
             "examples/hello.rs", "README.md")]


def cmd_stats(pygen, include_dir):
    headers = pygen.collect_headers(include_dir)
    if not headers:
        print("error: no headers found in %s" % include_dir, file=sys.stderr)
        return 1
    if pygen.ensure_ctypesgen() is None:
        return 1
    tmp = tempfile.mkdtemp(prefix="aroma-rustgen-")
    try:
        staged_dir = os.path.join(tmp, "staged")
        pygen.stage_headers(pygen.collect_headers(include_dir, for_staging=True),
                            include_dir, staged_dir)
        staged = [os.path.join(staged_dir, os.path.relpath(h, include_dir))
                  for h in headers]
        items, js_tmp = run_ctypesgen_json(pygen, staged, staged_dir)
        if items is None:
            return 1
        try:
            kinds = {}
            for item in items:
                kinds[item.get("type", "?")] = kinds.get(item.get("type", "?"), 0) + 1
            print("headers: %d" % len(headers))
            for kind in sorted(kinds):
                print("  %s: %d" % (kind, kinds[kind]))
            inline_helpers = find_inline_helpers(include_dir)
            print("  static_inline_helpers: %d" % len(inline_helpers))
        finally:
            if js_tmp:
                shutil.rmtree(js_tmp, ignore_errors=True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--include-dir", default=DEFAULT_INCLUDE)
    ap.add_argument("--output", "-o", default=DEFAULT_OUTPUT,
                    help="crate directory to write (default: bindings/rust)")
    ap.add_argument("--check", action="store_true",
                    help="verify the generated crate and report counts")
    ap.add_argument("--stats", action="store_true", help="print parse stats only")
    args = ap.parse_args(argv)

    pygen = _ensure_pygen()
    if pygen is None:
        return 1
    if args.stats:
        return cmd_stats(pygen, args.include_dir)

    headers = pygen.collect_headers(args.include_dir)
    if not headers:
        print("error: no headers found in %s" % args.include_dir, file=sys.stderr)
        return 1
    if pygen.ensure_ctypesgen() is None:
        return 1
    if not shutil.which("gcc"):
        print("error: gcc is required (C preprocessor)", file=sys.stderr)
        return 1

    out_dir = os.path.abspath(args.output)
    tmp = tempfile.mkdtemp(prefix="aroma-rustgen-")
    try:
        staged_dir = os.path.join(tmp, "staged")
        pygen.stage_headers(pygen.collect_headers(args.include_dir, for_staging=True),
                            args.include_dir, staged_dir)
        staged = [os.path.join(staged_dir, os.path.relpath(h, args.include_dir))
                  for h in headers]
        items, js_tmp = run_ctypesgen_json(pygen, staged, staged_dir)
        if items is None:
            print("error: ctypesgen JSON generation failed", file=sys.stderr)
            return 1
        try:
            inline_helpers = find_inline_helpers(args.include_dir)
            ffi_src, stats = build_rust_module(items, len(headers), inline_helpers)
        finally:
            if js_tmp:
                shutil.rmtree(js_tmp, ignore_errors=True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    files = write_crate(out_dir, ffi_src, len(headers), stats)
    print("wrote %s (%d headers, ~%d functions, %d structs, %d enums)" % (
        out_dir, len(headers), stats["functions"], stats["structs"], stats["enums"]))
    for p in files:
        print("  " + os.path.relpath(p, ROOT))

    if args.check:
        missing = [p for p in files if not os.path.isfile(p)]
        if missing:
            print("check: missing files: %s" % ", ".join(missing), file=sys.stderr)
            return 1
        # Sanity: key symbols must be present in ffi.rs.
        with open(os.path.join(out_dir, "src", "ffi.rs"), encoding="utf-8") as f:
            ffi_text = f.read()
        probes = ["pub fn aroma_button_create", "pub fn aroma_ui_init_impl",
                  "pub struct AromaNode", "pub type AromaNodeType"]
        absent = [p for p in probes if p not in ffi_text]
        if absent:
            print("check: ffi.rs missing: %s" % ", ".join(absent), file=sys.stderr)
            return 1
        # Inlines must NOT be declared extern (they have no .so symbol).
        for name in ("aroma_ui_init", "aroma_ui_button", "aroma_ui_label"):
            if ("pub fn %s(" % name) in ffi_text:
                print("check: ffi.rs must not declare static-inline %s" % name,
                      file=sys.stderr)
                return 1
        print("check: crate ok; functions=%d structs=%d enums=%d "
              "skipped_inline=%d skipped_variadic=%d" % (
                  stats["functions"], stats["structs"], stats["enums"],
                  stats["skipped_inline"], stats["skipped_variadic"]))
        if shutil.which("cargo"):
            import subprocess
            r = subprocess.run(["cargo", "metadata", "--no-deps", "--format-version", "1"],
                               cwd=out_dir, capture_output=True, text=True)
            if r.returncode != 0:
                print("check: cargo metadata failed:\n%s" % (r.stderr.strip() or r.stdout.strip()),
                      file=sys.stderr)
                return 1
            print("check: cargo metadata ok")
        else:
            print("check: cargo not on PATH; skipped cargo metadata")
    return 0


if __name__ == "__main__":
    sys.exit(main())
