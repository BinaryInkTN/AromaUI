#include <glps_common.h>
#include "utils/logger/pico_logger.h"
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#define MAX_KEY_LENGTH 255
#define MAX_VALUE_NAME 16383
#define MAX_FILES 128
#define MAX_PATH_LENGTH MAX_PATH
#define MAX_MIME_LENGTH 64
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif

#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

static const char *special_keys[256] = {
    [VK_ESCAPE] = "Escape", [VK_BACK] = "Backspace", [VK_RETURN] = "Enter", [VK_TAB] = "Tab", [VK_DELETE] = "Delete", [VK_INSERT] = "Insert", [VK_HOME] = "Home", [VK_END] = "End", [VK_PRIOR] = "PageUp", [VK_NEXT] = "PageDown", [VK_LEFT] = "ArrowLeft", [VK_RIGHT] = "ArrowRight", [VK_UP] = "ArrowUp", [VK_DOWN] = "ArrowDown", [VK_F1] = "F1", [VK_F2] = "F2", [VK_F3] = "F3", [VK_F4] = "F4", [VK_F5] = "F5", [VK_F6] = "F6", [VK_F7] = "F7", [VK_F8] = "F8", [VK_F9] = "F9", [VK_F10] = "F10", [VK_F11] = "F11", [VK_F12] = "F12"};

ssize_t __get_window_id_from_hwnd(glps_WindowManager *wm, HWND hwnd)
{
    if (!wm)
        return -1;
    for (SIZE_T i = 0; i < wm->window_count; ++i)
    {
        if (wm->windows[i]->hwnd == hwnd)
            return i;
    }
    return -1;
}

void glps_win32_attach_to_clipboard(glps_WindowManager *wm, char *mime, char *data)
{
    if (!OpenClipboard(NULL))
        return;
    EmptyClipboard();
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, strlen(data) + 1);
    if (hGlobal)
    {
        char *pGlobal = (char *)GlobalLock(hGlobal);
        if (pGlobal)
        {
            strcpy(pGlobal, data);
            GlobalUnlock(hGlobal);
            SetClipboardData(CF_TEXT, hGlobal);
        }
        else
        {
            GlobalFree(hGlobal);
        }
    }
    CloseClipboard();
}

void glps_win32_get_from_clipboard(glps_WindowManager *wm, char *data, size_t data_size)
{
    if (!OpenClipboard(NULL))
        return;
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (hData)
    {
        char *pText = (char *)GlobalLock(hData);
        if (pText)
        {
            strncpy(data, pText, data_size - 1);
            data[data_size - 1] = '\0';
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    glps_WindowManager *wm = (glps_WindowManager *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!wm)
        return DefWindowProc(hwnd, msg, wParam, lParam);

    ssize_t window_id = __get_window_id_from_hwnd(wm, hwnd);
    static bool key_states[256] = {false};
    static bool is_mouse_in_window = false;

    switch (msg)
    {
    case WM_DESTROY:
    {
        if (window_id < 0)
            break;
        bool is_parent = (window_id == 0);
        glps_Win32Window *window = wm->windows[window_id];
        if (!window)
            break;

        if (window->hdc)
        {
            wglMakeCurrent(NULL, NULL);
            ReleaseDC(window->hwnd, window->hdc);
        }

        if (is_parent)
        {
            for (SIZE_T j = 1; j < wm->window_count; j++)
            {
                if (wm->windows[j])
                    DestroyWindow(wm->windows[j]->hwnd);
            }
            if (wm->win32_ctx && wm->win32_ctx->hglrc)
            {
                wglDeleteContext(wm->win32_ctx->hglrc);
                wm->win32_ctx->hglrc = NULL;
            }
        }

        free(window);
        wm->windows[window_id] = NULL;

        if (!is_parent)
        {
            for (SIZE_T j = window_id; j < wm->window_count - 1; j++)
            {
                wm->windows[j] = wm->windows[j + 1];
            }
            wm->windows[--wm->window_count] = NULL;
        }
        else
        {
            wm->window_count = 0;
            PostQuitMessage(0);
        }
        break;
    }

    case WM_KEYDOWN:
    case WM_KEYUP:
    {
        if (window_id < 0 || wParam >= 256)
            break;
        bool is_down = (msg == WM_KEYDOWN);

        if (is_down ? (!(lParam & 0x40000000) && !key_states[wParam]) : true)
        {
            key_states[wParam] = is_down;
            if (wm->callbacks.keyboard_callback)
            {
                char char_value[32] = {0};
                const char *special = special_keys[wParam];
                if (special)
                {
                    strncpy(char_value, special, sizeof(char_value));
                }
                else
                {
                    BYTE keyboardState[256];
                    GetKeyboardState(keyboardState);
                    WCHAR unicodeChar = 0;
                    if (ToUnicode(wParam, (lParam >> 16) & 0xFF, keyboardState, &unicodeChar, 1, 0) == 1)
                        WideCharToMultiByte(CP_UTF8, 0, &unicodeChar, 1, char_value, sizeof(char_value), NULL, NULL);
                }
                unsigned long keycode = MapVirtualKey(wParam, MAPVK_VK_TO_VSC);
                wm->callbacks.keyboard_callback(window_id, is_down, char_value, keycode, wm->callbacks.keyboard_data);
            }
        }
        break;
    }

    case WM_SETFOCUS:
        if (window_id >= 0 && wm->callbacks.keyboard_enter_callback)
            wm->callbacks.keyboard_enter_callback(window_id, wm->callbacks.keyboard_enter_data);
        break;

    case WM_KILLFOCUS:
        if (window_id >= 0 && wm->callbacks.keyboard_leave_callback)
            wm->callbacks.keyboard_leave_callback(window_id, wm->callbacks.keyboard_leave_data);
        break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (window_id >= 0 && wm->callbacks.window_frame_update_callback)
            wm->callbacks.window_frame_update_callback(window_id, wm->callbacks.window_frame_update_data);
        EndPaint(hwnd, &ps);
        break;
    }

    case WM_SIZE:
        if (window_id >= 0)
        {
            RECT rect;
            if (GetClientRect(hwnd, &rect))
            {
                int width = rect.right - rect.left;
                int height = rect.bottom - rect.top;
                if (wm->callbacks.window_resize_callback)
                    wm->callbacks.window_resize_callback(window_id, width, height, wm->callbacks.window_resize_data);
            }
        }
        break;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            SetCursor(wm->win32_ctx->user_cursor);
            return TRUE;
        }
        break;

    case WM_MOUSEMOVE:
        if (window_id >= 0)
        {
            double x = GET_X_LPARAM(lParam);
            double y = GET_Y_LPARAM(lParam);
            if (!is_mouse_in_window)
            {
                is_mouse_in_window = true;
                if (wm->callbacks.mouse_enter_callback)
                    wm->callbacks.mouse_enter_callback(window_id, x, y, wm->callbacks.mouse_enter_data);
                TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
            }
            if (wm->callbacks.mouse_move_callback)
                wm->callbacks.mouse_move_callback(window_id, x, y, wm->callbacks.mouse_move_data);
        }
        break;

    case WM_MOUSELEAVE:
        is_mouse_in_window = false;
        if (wm->callbacks.mouse_leave_callback)
            wm->callbacks.mouse_leave_callback(window_id, wm->callbacks.mouse_leave_data);
        break;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        if (window_id >= 0 && wm->callbacks.mouse_click_callback)
            wm->callbacks.mouse_click_callback(window_id, (msg == WM_LBUTTONDOWN), wm->callbacks.mouse_click_data);
        break;

    case WM_MOUSEWHEEL:
        if (window_id >= 0)
        {
            DOUBLE delta = (DOUBLE)(GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA);
            DWORD extra_info = GetMessageExtraInfo();
            GLPS_SCROLL_SOURCE source = extra_info == 0 ? GLPS_SCROLL_SOURCE_WHEEL : GLPS_SCROLL_SOURCE_FINGER;
            if (wm->callbacks.mouse_scroll_callback)
                wm->callbacks.mouse_scroll_callback(window_id, GLPS_SCROLL_V_AXIS, source, delta, -1, false, wm->callbacks.mouse_scroll_data);
        }
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

bool glps_win32_should_close(glps_WindowManager *wm)
{
    static MSG msg;
    static DWORD last_frame_time = 0;
    static bool has_pending_messages = false;

    DWORD current_time = GetTickCount();
    bool needs_frame_update = wm->callbacks.window_frame_update_callback;

    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
            return true;

        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (msg.message == WM_DESTROY)
        {
            ssize_t window_id = __get_window_id_from_hwnd(wm, msg.hwnd);
            if (window_id >= 0 && wm->callbacks.window_close_callback)
                wm->callbacks.window_close_callback((SIZE_T)window_id, wm->callbacks.window_close_data);
        }

        has_pending_messages = (PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE) != 0);
    }
    else
    {
        has_pending_messages = false;
    }

    if (needs_frame_update && (current_time - last_frame_time >= 16))
    {
        for (SIZE_T i = 0; i < wm->window_count; i++)
        {
            if (wm->windows[i])
            {
                wm->callbacks.window_frame_update_callback(i, wm->callbacks.window_frame_update_data);
            }
        }
        last_frame_time = current_time;
    }

    if (!has_pending_messages && !needs_frame_update)
    {
        WaitMessage();
    }
    else if (!has_pending_messages && needs_frame_update && (current_time - last_frame_time < 1))
    {
        Sleep(1);
    }

    return false;
}

static void __init_window_class(glps_WindowManager *wm, const char *class_name)
{
    HINSTANCE hInstance = GetModuleHandle(NULL);
    wm->wc = (WNDCLASSEX){0};
    wm->wc.cbSize = sizeof(WNDCLASSEX);
    wm->wc.style = CS_HREDRAW | CS_VREDRAW;
    wm->wc.lpfnWndProc = WndProc;
    wm->wc.hInstance = hInstance;
    wm->wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wm->wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wm->wc.hbrBackground = NULL;
    wm->wc.lpszClassName = class_name;
    wm->wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    RegisterClassEx(&wm->wc);
}

void glps_win32_init(glps_WindowManager *wm)
{
    wm->windows = calloc(MAX_WINDOWS, sizeof(glps_Win32Window *));
    if (!wm->windows)
    {
        free(wm);
        return;
    }

    wm->win32_ctx = malloc(sizeof(glps_Win32Context));
    if (!wm->win32_ctx)
    {
        free(wm->windows);
        free(wm);
        return;
    }
    *wm->win32_ctx = (glps_Win32Context){0};
    wm->win32_ctx->user_cursor = LoadCursor(NULL, IDC_ARROW);
    wm->window_count = 0;

    __init_window_class(wm, "glpsWindowClass");
}

static BOOL SetPixelFormatForOpenGL(HDC hdc)
{
    static PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR), 1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 24, 8, 0,
        PFD_MAIN_PLANE, 0, 0, 0, 0};

    INT pixelFormat = ChoosePixelFormat(hdc, &pfd);
    return pixelFormat && SetPixelFormat(hdc, pixelFormat, &pfd);
}

void glps_win32_get_window_dimensions(glps_WindowManager *wm, size_t window_id, int *width, int *height)
{
    if (window_id < wm->window_count && wm->windows[window_id])
    {
        RECT rect;
        if (GetClientRect(wm->windows[window_id]->hwnd, &rect))
        {
            *width = rect.right - rect.left;
            *height = rect.bottom - rect.top;
        }
    }
}
ssize_t glps_win32_window_create(glps_WindowManager *wm, int x, int y, const char *title, int width, int height)
{
    if (!wm || !title || width <= 0 || height <= 0)
    {
        return -1;
    }

    HINSTANCE hInstance = GetModuleHandle(NULL);
    glps_Win32Window *win32_window = calloc(1, sizeof(glps_Win32Window));
    if (!win32_window)
        return -1;

    RECT rect = {0, 0, width, height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    int window_width = rect.right - rect.left;
    int window_height = rect.bottom - rect.top;

    int window_x = (x != CW_USEDEFAULT) ? x : CW_USEDEFAULT;
    int window_y = (y != CW_USEDEFAULT) ? y : CW_USEDEFAULT;

    win32_window->hwnd = CreateWindowEx(
        0,
        wm->wc.lpszClassName,
        title,
        WS_OVERLAPPEDWINDOW,
        window_x, window_y,
        window_width, window_height,
        NULL, NULL, hInstance, NULL);

    if (!win32_window->hwnd)
    {
        free(win32_window);
        return -1;
    }

    win32_window->hdc = GetDC(win32_window->hwnd);
    if (!win32_window->hdc)
        goto cleanup_window;

    if (!SetPixelFormatForOpenGL(win32_window->hdc))
        goto cleanup_dc;

    if (wm->window_count == 0)
    {
        wm->win32_ctx->hglrc = wglCreateContext(win32_window->hdc);
        if (!wm->win32_ctx->hglrc)
            goto cleanup_dc;
    }

    if (!wglMakeCurrent(win32_window->hdc, wm->win32_ctx->hglrc))
        goto cleanup_dc;

    ShowWindow(win32_window->hwnd, SW_SHOW);
    UpdateWindow(win32_window->hwnd);
    DragAcceptFiles(win32_window->hwnd, TRUE);

    RECT client_rect;
    GetClientRect(win32_window->hwnd, &client_rect);
    win32_window->properties.width = client_rect.right - client_rect.left;
    win32_window->properties.height = client_rect.bottom - client_rect.top;

    size_t title_len = strlen(title);
    size_t copy_len = (title_len < sizeof(win32_window->properties.title) - 1)
                          ? title_len
                          : sizeof(win32_window->properties.title) - 1;
    memcpy(win32_window->properties.title, title, copy_len);
    win32_window->properties.title[copy_len] = '\0';

    wm->windows[wm->window_count] = win32_window;
    SetWindowLongPtr(win32_window->hwnd, GWLP_USERDATA, (LONG_PTR)wm);

    return wm->window_count++;

cleanup_dc:
    ReleaseDC(win32_window->hwnd, win32_window->hdc);
cleanup_window:
    DestroyWindow(win32_window->hwnd);
    free(win32_window);
    return -1;
}

void glps_win32_destroy(glps_WindowManager *wm)
{
    if (!wm)
        return;

    for (size_t i = 0; i < wm->window_count; ++i)
    {
        if (wm->windows[i])
        {
            if (wm->windows[i]->hwnd)
            {
                DragAcceptFiles(wm->windows[i]->hwnd, FALSE);
                DestroyWindow(wm->windows[i]->hwnd);
            }
            free(wm->windows[i]);
        }
    }

    free(wm->windows);
    if (wm->win32_ctx)
    {
        if (wm->win32_ctx->hglrc)
            wglDeleteContext(wm->win32_ctx->hglrc);
        free(wm->win32_ctx);
    }
    free(wm);
}

HDC glps_win32_get_window_hdc(glps_WindowManager *wm, size_t window_id)
{
    if (window_id < wm->window_count && wm->windows[window_id])
        return wm->windows[window_id]->hdc;
    return NULL;
}

void glps_win32_swap_buffers(glps_WindowManager *wm, size_t window_id)
{
    if (window_id < wm->window_count && wm->windows[window_id] && wm->windows[window_id]->hdc)
        SwapBuffers(wm->windows[window_id]->hdc);
}

void glps_win32_cursor_change(glps_WindowManager *wm, GLPS_CURSOR_TYPE cursor_type)
{
    if (!wm || !wm->win32_ctx)
        return;
    static const LPCSTR cursor_ids[] = {
        IDC_ARROW, IDC_IBEAM, IDC_CROSS, IDC_HAND,
        IDC_SIZEWE, IDC_SIZENS, IDC_NO};
    wm->win32_ctx->user_cursor = LoadCursor(NULL, cursor_ids[cursor_type]);
    SetCursor(wm->win32_ctx->user_cursor);
}
void glps_win32_set_window_blur(glps_WindowManager *wm, size_t window_id, bool enable, int blur_radius)
{
    if (!wm || window_id >= wm->window_count || !wm->windows[window_id])
    {
        return;
    }

    HWND hwnd = wm->windows[window_id]->hwnd;

    // Use DWM blur effect (works without WS_EX_LAYERED)
    HMODULE hDwmApi = LoadLibraryA("dwmapi.dll");
    if (hDwmApi)
    {
        typedef HRESULT(WINAPI * DWMENABLEBLURBEHINDWINDOW)(HWND, const void *);
        DWMENABLEBLURBEHINDWINDOW DwmEnableBlurBehindWindow =
            (DWMENABLEBLURBEHINDWINDOW)GetProcAddress(hDwmApi, "DwmEnableBlurBehindWindow");

        if (DwmEnableBlurBehindWindow)
        {
            struct DWM_BLURBEHIND
            {
                DWORD dwFlags;
                BOOL fEnable;
                HRGN hRgnBlur;
                BOOL fTransitionOnMaximized;
            };

            struct DWM_BLURBEHIND bb = {0};
            bb.dwFlags = 0x00000001; // DWM_BB_ENABLE
            bb.fEnable = enable;
            bb.hRgnBlur = NULL; // Apply to entire window
            bb.fTransitionOnMaximized = FALSE;

            DwmEnableBlurBehindWindow(hwnd, &bb);
        }
        FreeLibrary(hDwmApi);
    }

    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
}

void glps_win32_set_window_opacity(glps_WindowManager *wm, size_t window_id, float opacity)
{
    if (!wm || window_id >= wm->window_count || !wm->windows[window_id])
    {
        return;
    }

    if (opacity < 0.0f)
        opacity = 0.0f;
    if (opacity > 1.0f)
        opacity = 1.0f;

    HWND hwnd = wm->windows[window_id]->hwnd;
    BYTE alpha = (BYTE)(opacity * 255);
    LONG_PTR current_style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

    if (alpha < 255)
    {
        // Enable layered window for transparency
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, current_style | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
    }
    else
    {
        // Disable layered window for normal rendering
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, current_style & ~WS_EX_LAYERED);
    }

    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
}

void glps_win32_set_window_background_transparent(glps_WindowManager *wm, size_t window_id)
{
    if (!wm || window_id >= wm->window_count || !wm->windows[window_id])
    {
        return;
    }

    HWND hwnd = wm->windows[window_id]->hwnd;

    // Remove background brush
    SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)NULL);

    // Make sure we're not using layered window for this (it breaks rendering)
    LONG_PTR current_style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, current_style & ~WS_EX_LAYERED);

    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);
}
