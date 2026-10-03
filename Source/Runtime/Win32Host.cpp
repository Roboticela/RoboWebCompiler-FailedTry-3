#include "Runtime/HostLoop.h"

#include "Core/Utf8.h"
#include "Layout/LayoutEngine.h"
#include "Render/Renderer.h"
#include "Runtime/FramePlan.h"
#include "Runtime/Interaction.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace RoboWeb {

namespace {

struct App {
    Document* document = nullptr;
    HWND window = nullptr;
    int width = DEFAULT_VIEWPORT_WIDTH_PX;
    int height = DEFAULT_VIEWPORT_HEIGHT_PX;
    int mouseX = 0;
    int mouseY = 0;
    bool refreshing = false;
    bool hasMarquee = false;
    bool imagesSettled = false;
    ViewCache view;
    FrameUpdate present;
    std::wstring pending;
};

std::wstring Widen(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) {
        return std::wstring();
    }
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &wide[0], count);
    return wide;
}

std::string Narrow(const std::wstring& text) {
    if (text.empty()) {
        return "";
    }
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) {
        return "";
    }
    std::string utf8(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &utf8[0], count, nullptr, nullptr);
    return utf8;
}

bool TreeHasMarquee(const Node& node) {
    if (node.kind == ElementKind::Marquee) {
        return true;
    }
    for (const std::unique_ptr<Node>& child : node.children) {
        if (TreeHasMarquee(*child)) {
            return true;
        }
    }
    return false;
}

bool TreeHasSvgAnimation(const Node& node) {
    const std::string tag = ToLowerAscii(node.tagName);
    if (tag == "animate" || tag == "set" || tag == "animatetransform" || tag == "animatemotion") {
        return true;
    }
    for (const std::unique_ptr<Node>& child : node.children) {
        if (TreeHasSvgAnimation(*child)) {
            return true;
        }
    }
    return false;
}

bool IsFileInput(const Node* node) {
    return node != nullptr && node->kind == ElementKind::Input && ToLowerAscii(AttributeValue(*node, "type")) == "file";
}

bool ShowsCaret(const Node* node) {
    if (node == nullptr) {
        return false;
    }
    if (node->kind == ElementKind::Textarea) {
        return true;
    }
    if (node->kind != ElementKind::Input) {
        return false;
    }
    const std::string type = ToLowerAscii(AttributeValue(*node, "type"));
    const std::string effective = type.empty() ? std::string("text") : type;
    return effective != "checkbox" && effective != "radio" && effective != "button" && effective != "submit" && effective != "reset" &&
           effective != "file" && effective != "image" && effective != "hidden" && effective != "color" && effective != "range";
}

std::wstring Caption(const App& app) {
    std::string caption = app.document->title.empty() ? "RoboWeb" : app.document->title;
    std::string extra = app.document->status;
    if (extra.empty()) {
        for (Node* node = app.document->hover; node != nullptr; node = node->parent) {
            if (!AttributeValue(*node, "title").empty()) {
                extra = AttributeValue(*node, "title");
                break;
            }
            if (node->kind == ElementKind::A && !AttributeValue(*node, "href").empty()) {
                extra = AttributeValue(*node, "href");
                break;
            }
        }
    }
    if (!extra.empty()) {
        caption += " - ";
        caption += extra;
    }
    return Widen(caption);
}

void Present(const App& app, HDC dc) {
    const Framebuffer& frame = app.view.frame;
    if (!app.present.paint || frame.width <= 0 || frame.height <= 0 || frame.pixels.empty()) {
        return;
    }
    const int originX = app.present.full ? 0 : app.present.x;
    const int originY = app.present.full ? 0 : app.present.y;
    const int width = app.present.full ? frame.width : app.present.width;
    const int height = app.present.full ? frame.height : app.present.height;
    if (width <= 0 || height <= 0) {
        return;
    }
    BITMAPINFO info;
    ZeroMemory(&info, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    std::vector<std::uint8_t> bgra(static_cast<std::size_t>(width * height * 4));
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            const std::size_t source = static_cast<std::size_t>(((originY + row) * frame.width + originX + column) * 4);
            const std::size_t target = static_cast<std::size_t>((row * width + column) * 4);
            bgra[target] = frame.pixels[source + 2];
            bgra[target + 1] = frame.pixels[source + 1];
            bgra[target + 2] = frame.pixels[source];
            bgra[target + 3] = 255;
        }
    }
    SetDIBitsToDevice(dc, originX, originY, static_cast<UINT>(width), static_cast<UINT>(height), 0, 0, 0, static_cast<UINT>(height),
        bgra.data(), &info, DIB_RGB_COLORS);
}

void Refresh(App& app, FrameCause cause, Node* previousHover) {
    if (app.window == nullptr || app.refreshing || app.document == nullptr) {
        return;
    }
    app.refreshing = true;
    RECT client;
    GetClientRect(app.window, &client);
    app.width = std::max(MIN_VIEWPORT_PX, static_cast<int>(client.right - client.left));
    app.height = std::max(MIN_VIEWPORT_PX, static_cast<int>(client.bottom - client.top));
    app.present = AdvanceFrame(app.view, *app.document, app.width, app.height, cause, previousHover, app.imagesSettled);
    if (cause == FrameCause::Viewport || cause == FrameCause::Content) {
        const int scrollBefore = app.document->scrollY;
        DispatchScroll(*app.document, 0, app.height, app.view.layout.contentHeight);
        if (app.document->scrollY != scrollBefore) {
            app.present = AdvanceFrame(app.view, *app.document, app.width, app.height, FrameCause::Scroll, nullptr, app.imagesSettled);
        }
    }
    bool waitingImage = false;
    for (const DrawOp& op : app.view.layout.ops) {
        if (op.deferImage) {
            waitingImage = true;
            break;
        }
    }
    if (waitingImage && !app.imagesSettled) {
        SetTimer(app.window, 3, 33, nullptr);
    } else {
        KillTimer(app.window, 3);
    }
    if (cause == FrameCause::Viewport || cause == FrameCause::Content || cause == FrameCause::Scroll) {
        SCROLLINFO info;
        ZeroMemory(&info, sizeof(info));
        info.cbSize = sizeof(info);
        info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
        info.nMin = 0;
        info.nMax = std::max(0, app.view.layout.contentHeight - 1);
        info.nPage = static_cast<UINT>(app.height);
        info.nPos = app.document->scrollY;
        SetScrollInfo(app.window, SB_VERT, &info, TRUE);
    }
    SetWindowTextW(app.window, Caption(app).c_str());
    const bool marquee = app.document->root != nullptr && TreeHasMarquee(*app.document->root);
    const bool animated = app.document->root != nullptr && TreeHasSvgAnimation(*app.document->root);
    if (marquee != app.hasMarquee || animated) {
        app.hasMarquee = marquee || animated;
        if (marquee || animated) {
            SetTimer(app.window, 2, 33, nullptr);
        } else {
            KillTimer(app.window, 2);
        }
    }
    if (app.present.scrollDelta != 0) {
        ScrollWindowEx(app.window, 0, -app.present.scrollDelta, nullptr, nullptr, nullptr, nullptr, 0);
    }
    if (app.present.paint) {
        RECT dirty;
        if (app.present.full) {
            GetClientRect(app.window, &dirty);
        } else {
            dirty.left = app.present.x;
            dirty.top = app.present.y;
            dirty.right = app.present.x + app.present.width;
            dirty.bottom = app.present.y + app.present.height;
        }
        InvalidateRect(app.window, &dirty, FALSE);
    }
    app.refreshing = false;
}

void CopyFocus(const App& app) {
    if (app.document->focus == nullptr) {
        return;
    }
    const std::wstring text = Widen(app.document->focus->liveValue);
    if (!OpenClipboard(app.window)) {
        return;
    }
    EmptyClipboard();
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory != nullptr) {
        void* destination = GlobalLock(memory);
        if (destination != nullptr) {
            memcpy(destination, text.c_str(), bytes);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

void PasteFocus(App& app) {
    if (!OpenClipboard(app.window)) {
        return;
    }
    HANDLE memory = GetClipboardData(CF_UNICODETEXT);
    std::string text;
    if (memory != nullptr) {
        const wchar_t* wide = static_cast<const wchar_t*>(GlobalLock(memory));
        if (wide != nullptr) {
            text = Narrow(wide);
            GlobalUnlock(memory);
        }
    }
    CloseClipboard();
    if (!text.empty() && DispatchText(*app.document, text)) {
        Refresh(app, FrameCause::Content, nullptr);
    }
}

void ChooseFile(App& app, Node& input) {
    wchar_t buffer[MAX_PATH];
    buffer[0] = L'\0';
    OPENFILENAMEW dialog;
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app.window;
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrFilter = L"All files\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) {
        input.liveValue = Narrow(buffer);
        app.document->status = input.liveValue;
        Refresh(app, FrameCause::Content, nullptr);
    }
}

void OnScroll(App& app, WPARAM wParam) {
    SCROLLINFO info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = SIF_ALL;
    GetScrollInfo(app.window, SB_VERT, &info);
    int next = info.nPos;
    switch (LOWORD(wParam)) {
    case SB_TOP:
        next = info.nMin;
        break;
    case SB_BOTTOM:
        next = info.nMax;
        break;
    case SB_LINEUP:
        next -= 40;
        break;
    case SB_LINEDOWN:
        next += 40;
        break;
    case SB_PAGEUP:
        next -= static_cast<int>(info.nPage);
        break;
    case SB_PAGEDOWN:
        next += static_cast<int>(info.nPage);
        break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
        next = info.nTrackPos;
        break;
    default:
        return;
    }
    DispatchScroll(*app.document, next - app.document->scrollY, app.height, app.view.layout.contentHeight);
    Refresh(app, FrameCause::Scroll, nullptr);
}

void OnKeyDown(App& app, WPARAM key) {
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (control && (key == 'C' || key == 'X')) {
        CopyFocus(app);
        Node* focus = app.document->focus;
        if (key == 'X' && ShowsCaret(focus) && !HasAttribute(*focus, "readonly") && !HasAttribute(*focus, "disabled")) {
            focus->liveValue.clear();
            app.document->caretOffset = 0;
            app.document->caretOn = true;
            Refresh(app, FrameCause::Content, nullptr);
        }
        return;
    }
    if (control && key == 'V') {
        PasteFocus(app);
        return;
    }
    const char* name = nullptr;
    if (key == VK_BACK) {
        name = "backspace";
    } else if (key == VK_DELETE) {
        name = "delete";
    } else if (key == VK_LEFT) {
        name = "left";
    } else if (key == VK_RIGHT) {
        name = "right";
    } else if (key == VK_HOME) {
        name = "home";
    } else if (key == VK_END) {
        name = "end";
    } else if (key == VK_RETURN) {
        name = "enter";
    } else if (key == VK_TAB) {
        name = (GetKeyState(VK_SHIFT) & 0x8000) != 0 ? "shift-tab" : "tab";
    }
    if (name != nullptr && DispatchKey(*app.document, name)) {
        RevealFocus(*app.document, app.view.layout, app.height);
        Refresh(app, FrameCause::Content, nullptr);
    }
}

void OnChar(App& app, wchar_t character) {
    if (character < 32) {
        return;
    }
    if (character >= 0xD800 && character <= 0xDBFF) {
        app.pending.assign(1, character);
        return;
    }
    std::wstring text;
    if (!app.pending.empty() && character >= 0xDC00 && character <= 0xDFFF) {
        text = app.pending;
        text.push_back(character);
    } else {
        text.assign(1, character);
    }
    app.pending.clear();
    if (DispatchText(*app.document, Narrow(text))) {
        Refresh(app, FrameCause::Content, nullptr);
    }
}

HCURSOR CursorFor(PointerShape shape) {
    if (shape == PointerShape::Hand) {
        return LoadCursor(nullptr, IDC_HAND);
    }
    if (shape == PointerShape::Text) {
        return LoadCursor(nullptr, IDC_IBEAM);
    }
    return LoadCursor(nullptr, IDC_ARROW);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW* created = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created->lpCreateParams));
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        if (app != nullptr) {
            Present(*app, dc);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_SIZE:
        if (app != nullptr && wParam != SIZE_MINIMIZED) {
            Refresh(*app, FrameCause::Viewport, nullptr);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (app != nullptr) {
            app->mouseX = GET_X_LPARAM(lParam);
            app->mouseY = GET_Y_LPARAM(lParam);
            TRACKMOUSEEVENT track;
            ZeroMemory(&track, sizeof(track));
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = window;
            TrackMouseEvent(&track);
            Node* previousHover = app->document->hover;
            if (DispatchHover(*app->document, app->view.layout, app->mouseX, app->mouseY)) {
                Refresh(*app, FrameCause::Hover, previousHover);
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (app != nullptr && app->document->hover != nullptr) {
            Node* previousHover = app->document->hover;
            app->document->hover = nullptr;
            Refresh(*app, FrameCause::Hover, previousHover);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (app != nullptr) {
            SetFocus(window);
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            DispatchClick(*app->document, app->view.layout, x, y);
            if (IsFileInput(app->document->focus)) {
                ChooseFile(*app, *app->document->focus);
            }
            RevealFocus(*app->document, app->view.layout, app->height);
            Refresh(*app, FrameCause::Content, nullptr);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (app != nullptr) {
            const int ticks = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            DispatchScroll(*app->document, -ticks * 48, app->height, app->view.layout.contentHeight);
            Refresh(*app, FrameCause::Scroll, nullptr);
        }
        return 0;
    case WM_VSCROLL:
        if (app != nullptr) {
            OnScroll(*app, wParam);
        }
        return 0;
    case WM_KEYDOWN:
        if (app != nullptr) {
            OnKeyDown(*app, wParam);
        }
        return 0;
    case WM_CHAR:
        if (app != nullptr) {
            OnChar(*app, static_cast<wchar_t>(wParam));
        }
        return 0;
    case WM_SETCURSOR:
        if (app != nullptr && LOWORD(lParam) == HTCLIENT) {
            POINT point;
            GetCursorPos(&point);
            ScreenToClient(window, &point);
            SetCursor(CursorFor(PointerAt(app->view.layout, point.x, point.y, app->document->scrollY)));
            return TRUE;
        }
        break;
    case WM_TIMER:
        if (app != nullptr && wParam == 1) {
            if (ShowsCaret(app->document->focus)) {
                app->document->caretOn = !app->document->caretOn;
                Refresh(*app, FrameCause::Caret, nullptr);
            }
        } else if (app != nullptr && wParam == 2) {
            TickMarquee(*app->document);
            SetSvgAnimationTime(SvgAnimationTime() + 0.033);
            Refresh(*app, FrameCause::Content, nullptr);
        } else if (app != nullptr && wParam == 3) {
            app->imagesSettled = true;
            KillTimer(window, 3);
            Refresh(*app, FrameCause::Content, nullptr);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

void AttachParentConsole() {
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    auto attach = reinterpret_cast<BOOL(WINAPI*)(DWORD)>(GetProcAddress(kernel, "AttachConsole"));
    if (attach == nullptr || !attach(static_cast<DWORD>(-1))) {
        return;
    }
    (void)freopen("CONOUT$", "w", stdout);
    (void)freopen("CONOUT$", "w", stderr);
    (void)freopen("CONIN$", "r", stdin);
    std::cout.clear();
    std::cerr.clear();
    std::cin.clear();
}

int RunWindow(Document& document, int width, int height) {
    HMODULE user = GetModuleHandleW(L"user32.dll");
    auto dpi = reinterpret_cast<BOOL(WINAPI*)()>(GetProcAddress(user, "SetProcessDPIAware"));
    if (dpi != nullptr) {
        dpi();
    }
    ApplyAutofocus(document);
    width = std::max(MIN_VIEWPORT_PX, std::min(MAX_VIEWPORT_PX, width));
    height = std::max(MIN_VIEWPORT_PX, std::min(MAX_VIEWPORT_PX, height));
    HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"RoboWebPage";
    WNDCLASSW windowClass;
    if (!GetClassInfoW(instance, className, &windowClass)) {
        ZeroMemory(&windowClass, sizeof(windowClass));
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = className;
        if (RegisterClassW(&windowClass) == 0) {
            MessageBoxW(nullptr, L"Cannot register the page window.", L"RoboWeb", MB_OK | MB_ICONERROR);
            return 1;
        }
    }
    App app;
    app.document = &document;
    app.width = width;
    app.height = height;
    RECT bounds{0, 0, width, height};
    const DWORD style = WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN;
    AdjustWindowRect(&bounds, style, FALSE);
    const std::wstring caption = document.title.empty() ? L"RoboWeb" : Widen(document.title);
    app.window = CreateWindowExW(0, className, caption.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
        bounds.bottom - bounds.top, nullptr, nullptr, instance, &app);
    if (app.window == nullptr) {
        MessageBoxW(nullptr, L"Cannot open the page window.", L"RoboWeb", MB_OK | MB_ICONERROR);
        return 1;
    }
    SetTimer(app.window, 1, 530, nullptr);
    Refresh(app, FrameCause::Viewport, nullptr);
    ShowWindow(app.window, SW_SHOW);
    UpdateWindow(app.window);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}

}
