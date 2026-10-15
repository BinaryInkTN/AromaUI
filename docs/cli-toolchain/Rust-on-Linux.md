# Write apps in Rust

Write your app in Rust, build it with the aroma CLI. Rust projects
use the generated `aroma` crate -- the same widgets, themes, and event
loop as the C API -- linked against `libaroma` through a cargo build
script. Desktop Linux is the supported target.

## Creating the Project

```bash
aroma create myapp --rust
```

This creates `src/main.rs` (a tap-counter hello app) and `Cargo.toml`
with a path dependency on the generated `aroma` crate (`bindings/rust`,
written by `aroma build linux`; never edit it by hand).

## Writing the Application

Edit `src/main.rs`. It owns `main()` and drives the event loop itself:

```rust
use aroma::{ffi, ui_button, ui_container, ui_create_window, ui_init,
            ui_is_running, ui_label, ui_process_events, ui_render};

fn main() {
    unsafe {
        assert!(ui_init(), "aroma_ui_init failed");
        let theme = ffi::aroma_theme_create_material_blue_dark();
        ffi::aroma_theme_set_global(&theme);
        let window = ui_create_window("My App", 400, 600);
        let root = window as *mut ffi::AromaNode;
        let bx = ui_container(root, 0, 0, 400, 600);
        // ... widgets, callbacks ...
        while ui_is_running() {
            ui_process_events();
            ui_render(window);
        }
    }
}
```

Rules: callbacks must be plain `extern "C" fn` items (closures cannot
cross the FFI boundary -- pass state via `user_data`); shared callback
state belongs in atomics (`AtomicI32`, `AtomicPtr`) rather than
`static mut`; use `/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf` on
Linux (the template falls back to `/system/fonts/Roboto-Regular.ttf`
where present). See [Rust Bindings](../Rust-Bindings.md) for the raw
`ffi` vs safe `ui_*` split.

## Dependencies

Third-party crates work as usual -- add them to the project's
`Cargo.toml`:

```toml
[dependencies]
aroma = { path = "bindings/rust" }
serde_json = "1"
```

The `aroma` path dependency itself is managed by the CLI (regenerated
from the C headers on every build), so leave that line alone.

## How It Works

You only touch `src/main.rs` and `Cargo.toml`. On every build the CLI
handles the native plumbing for you: it regenerates `bindings/rust`
from the public headers when they are newer, builds `libaroma.so` once
into `build/aroma-lib/`, sets `AROMA_LIB`/`LD_LIBRARY_PATH`, and runs
`cargo build` / `cargo run`. The bindings crate's `build.rs` links
`libaroma` (shared when present, otherwise the static archive plus its
system deps like `GLESv2`, `EGL`, `freetype`, and `vulkan`).

## Running on Linux

```bash
aroma build linux            # generates bindings + builds desktop libaroma.so
aroma run linux              # launches the app via cargo run
```

The first build takes several minutes (it compiles freetype from
source) -- wait for `OK Build successful`.

## Unsupported Targets

Android and Web builds are not wired for Rust yet -- `aroma build
android` / `aroma build web` in a Rust project report an error instead
of failing halfway through. Use a C or Python project for those
targets.
