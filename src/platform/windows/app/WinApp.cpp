#include "lib/platform/windows/app/WinApp.hpp"

int WinApp::run(HINSTANCE instance) {
    instance_ = instance;
    WNDCLASSW wc {};
    wc.lpfnWndProc = &WinApp::windowProcThunk;
    wc.hInstance = instance_;
    wc.lpszClassName = L"BJLEDAmbilightWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(24, 26, 30));
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"BJ LED Ambilight",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        760,
        420,
        nullptr,
        nullptr,
        instance_,
        this);
    ShowWindow(hwnd_, SW_SHOW);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return int(msg.wParam);
}

LRESULT CALLBACK WinApp::windowProcThunk(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    WinApp* app = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        app = reinterpret_cast<WinApp*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
        app = reinterpret_cast<WinApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return app ? app->windowProc(hwnd, message, wparam, lparam) : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT WinApp::windowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE:
            CreateWindowW(L"STATIC", L"BJ LED Ambilight", WS_CHILD | WS_VISIBLE, 24, 22, 240, 28, hwnd, nullptr, instance_, nullptr);
            outputButtons_[0] = CreateWindowW(L"BUTTON", L"Auto", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP, 24, 78, 84, 26, hwnd, HMENU(10), instance_, nullptr);
            outputButtons_[1] = CreateWindowW(L"BUTTON", L"Red", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 112, 78, 84, 26, hwnd, HMENU(11), instance_, nullptr);
            outputButtons_[2] = CreateWindowW(L"BUTTON", L"Green", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 200, 78, 94, 26, hwnd, HMENU(12), instance_, nullptr);
            outputButtons_[3] = CreateWindowW(L"BUTTON", L"Blue", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 298, 78, 84, 26, hwnd, HMENU(13), instance_, nullptr);
            outputButtons_[4] = CreateWindowW(L"BUTTON", L"White", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 386, 78, 90, 26, hwnd, HMENU(14), instance_, nullptr);
            SendMessageW(outputButtons_[0], BM_SETCHECK, BST_CHECKED, 0);
            swatch_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_BLACKRECT, 590, 70, 110, 110, hwnd, nullptr, instance_, nullptr);
            status_ = CreateWindowW(L"STATIC", L"RGB 0 0 0", WS_CHILD | WS_VISIBLE, 24, 128, 440, 24, hwnd, nullptr, instance_, nullptr);
            capture_ = new GdiScreenCapture(settings_);
            led_.connect(nullptr);
            SetTimer(hwnd, 1, 1000 / settings_.fps, nullptr);
            break;
        case WM_COMMAND:
            if (LOWORD(wparam) >= 10 && LOWORD(wparam) <= 14) setOutputMode(int(LOWORD(wparam) - 10));
            break;
        case WM_TIMER:
            tick();
            break;
        case WM_DESTROY:
            stop();
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    return 0;
}

void WinApp::stop() {
    KillTimer(hwnd_, 1);
    delete capture_;
    capture_ = nullptr;
    if (status_) SetWindowTextW(status_, L"Stopped");
}

void WinApp::tick() {
    if (!capture_) return;
    auto pixels = capture_->capture();
    frameAnalysis_ = analyzer_.analyzeFrame(pixels, settings_.sampleWidth, settings_.sampleHeight, settings_);
    bj::RGB color = selectedOutputColor(frameAnalysis_.output);
    smoothed_ = outputMode_ == 0 && hasSmoothed_ ? bj::smooth(smoothed_, color, settings_.smoothing) : color;
    hasSmoothed_ = true;
    wchar_t text[96];
    wsprintfW(text, L"RGB %u %u %u", smoothed_.r, smoothed_.g, smoothed_.b);
    SetWindowTextW(status_, text);
    HBRUSH brush = CreateSolidBrush(RGB(smoothed_.r, smoothed_.g, smoothed_.b));
    HDC dc = GetDC(swatch_);
    RECT rect;
    GetClientRect(swatch_, &rect);
    FillRect(dc, &rect, brush);
    ReleaseDC(swatch_, dc);
    DeleteObject(brush);
    if (led_.isReady()) led_.write(smoothed_, settings_.maxChannel);
}

bj::RGB WinApp::selectedOutputColor(bj::RGB autoColor) const {
    switch (outputMode_) {
        case 1: return bj::RGB(255, 0, 0);
        case 2: return bj::RGB(0, 255, 0);
        case 3: return bj::RGB(0, 0, 255);
        case 4: return bj::RGB(255, 255, 255);
        default: return autoColor;
    }
}

void WinApp::setOutputMode(int mode) {
    outputMode_ = mode;
    hasSmoothed_ = false;
}
