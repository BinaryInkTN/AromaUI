use aroma::{
    ffi, ui_button, ui_container, ui_create_window, ui_destroy_window, ui_init,
    ui_is_running, ui_label, ui_process_events, ui_render, ui_shutdown,
};
use std::ffi::CString;
use std::os::raw::{c_char, c_void};
use std::sync::atomic::{AtomicI32, AtomicPtr, Ordering};

const FONT_PATH: &str = "/system/fonts/Roboto-Regular.ttf";
const DESKTOP_FONT_PATH: &str = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";

static TAPS: AtomicI32 = AtomicI32::new(0);
static COUNTER: AtomicPtr<ffi::AromaNode> = AtomicPtr::new(std::ptr::null_mut());

extern "C" fn on_tap(_node: *mut ffi::AromaNode, _ud: *mut c_void) -> bool {
    let taps = TAPS.fetch_add(1, Ordering::SeqCst) + 1;
    let counter = COUNTER.load(Ordering::SeqCst);
    if !counter.is_null() {
        let text = CString::new(format!("Taps: {}", taps)).unwrap();
        unsafe { ffi::aroma_label_set_text(counter, text.as_ptr() as *const c_char) };
    }
    println!("tapped {}", taps);
    true
}

fn main() {
    unsafe {
        let font_path = if std::path::Path::new(DESKTOP_FONT_PATH).exists() {
            DESKTOP_FONT_PATH
        } else {
            FONT_PATH
        };
        assert!(ui_init(), "aroma_ui_init failed");
        let theme = ffi::aroma_theme_create_material_blue_dark();
        ffi::aroma_theme_set_global(&theme);
        let fp = CString::new(font_path).unwrap();
        let font = ffi::aroma_font_create(fp.as_ptr(), 20);
        assert!(!font.is_null(), "font load failed: {}", font_path);
        let window = ui_create_window("{{PROJECT_NAME}}", 400, 600);
        assert!(!window.is_null(), "window creation failed");
        let root = window as *mut ffi::AromaNode;
        let bx = ui_container(root, 0, 0, 400, 600);
        ui_label(bx, "{{PROJECT_NAME}}", 40, 60, 1, font);
        COUNTER.store(ui_label(bx, "Taps: 0", 40, 140, 1, font), Ordering::SeqCst);
        ui_button(
            bx,
            "Tap me",
            40,
            220,
            200,
            52,
            Some(on_tap),
            std::ptr::null_mut(),
            font,
        );
        while ui_is_running() {
            ui_process_events();
            ui_render(window);
        }
        ui_destroy_window(window);
        ffi::aroma_font_destroy(font);
        ui_shutdown();
    }
}
