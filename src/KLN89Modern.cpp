#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>

namespace fs = std::filesystem;

static HWND g_core = nullptr;
static HANDLE g_process = nullptr;
static DWORD g_pid = 0;
static int g_srcW = 0, g_srcH = 0;
static RECT g_dst{};
static HDC g_captureDC = nullptr, g_backDC = nullptr;
static HBITMAP g_captureBmp = nullptr, g_backBmp = nullptr;
static HGDIOBJ g_oldCapture = nullptr, g_oldBack = nullptr;
static int g_backW = 0, g_backH = 0;
static fs::path g_tempDir;
static fs::path g_originalUserDat;
static HWND g_mouseTarget = nullptr;

static const wchar_t* APP_TITLE = L"KLN 89 Simulator - Modern Windows";

static void Msg(const std::wstring& text, UINT icon = MB_ICONERROR) {
    MessageBoxW(nullptr, text.c_str(), APP_TITLE, MB_OK | icon);
}

static std::vector<unsigned char> ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    auto n = f.tellg();
    if (n <= 0) return {};
    f.seekg(0, std::ios::beg);
    std::vector<unsigned char> b((size_t)n);
    f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

static bool WriteAll(const fs::path& p, const std::vector<unsigned char>& b) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(b.data()), (std::streamsize)b.size());
    return !!f;
}

static bool BytesEqual(const std::vector<unsigned char>& b, size_t off, std::initializer_list<unsigned char> v) {
    if (off + v.size() > b.size()) return false;
    size_t i = 0;
    for (auto x : v) if (b[off + i++] != x) return false;
    return true;
}

static bool PatchOriginal(const fs::path& src, const fs::path& dst, std::wstring& why) {
    auto b = ReadAll(src);
    if (b.size() != 721952) {
        why = L"Unsupported kln89.exe size.\nExpected the 1997 KLN 89 simulator executable (721,952 bytes).";
        return false;
    }

    struct One { size_t off; std::vector<unsigned char> from, to; };
    std::vector<One> patches = {
        {0x5A3B2, {0x75}, {0xEB}},
        {0x5A3EC, {0x75}, {0xEB}},
        {0x842B7, {0xFF,0x25,0x88,0x27,0x6C}, {0x6A,0x01,0x58,0xC2,0x10}}
    };

    bool already = true;
    for (const auto& p : patches) {
        if (p.off + p.to.size() > b.size() || !std::equal(p.to.begin(), p.to.end(), b.begin() + p.off)) {
            already = false; break;
        }
    }
    if (!already) {
        for (const auto& p : patches) {
            if (p.off + p.from.size() > b.size() || !std::equal(p.from.begin(), p.from.end(), b.begin() + p.off)) {
                why = L"This kln89.exe is not the known 1997 build, or it was modified already.\nNo patch was applied.";
                return false;
            }
        }
        for (const auto& p : patches) std::copy(p.to.begin(), p.to.end(), b.begin() + p.off);
    }

    fs::create_directories(dst.parent_path());
    if (!WriteAll(dst, b)) {
        why = L"Could not write the patched executable.";
        return false;
    }
    return true;
}

static bool ChooseOriginalExe(fs::path& out) {
    wchar_t fileBuf[32768]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = L"KLN 89 Simulator (kln89.exe)\0kln89.exe\0Executable files (*.exe)\0*.exe\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = (DWORD)std::size(fileBuf);
    ofn.lpstrTitle = L"Select your original kln89.exe";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;
    out = fs::path(fileBuf);
    return true;
}

static fs::path MakeUniqueTempDir() {
    fs::path base = fs::temp_directory_path();
    DWORD pid = GetCurrentProcessId();
    ULONGLONG tick = GetTickCount64();
    for (unsigned i = 0; i < 1000; ++i) {
        fs::path p = base / (L"KLN89Modern_" + std::to_wstring(pid) + L"_" + std::to_wstring(tick) + L"_" + std::to_wstring(i));
        std::error_code ec;
        if (fs::create_directory(p, ec)) return p;
    }
    return {};
}

static void CleanupOldTempDirs() {
    std::error_code ec;
    fs::path base = fs::temp_directory_path(ec);
    if (ec) return;
    for (auto it = fs::directory_iterator(base, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        auto name = it->path().filename().wstring();
        if (name.rfind(L"KLN89Modern_", 0) != 0) continue;
        fs::remove_all(it->path(), ec);
        ec.clear();
    }
}

static void PersistUserDataAndCleanup() {
    if (!g_tempDir.empty()) {
        try {
            fs::path tmpUser = g_tempDir / L"user.dat";
            if (!g_originalUserDat.empty() && fs::exists(tmpUser)) {
                fs::copy_file(tmpUser, g_originalUserDat, fs::copy_options::overwrite_existing);
            }
        } catch (...) {}
        std::error_code ec;
        fs::remove_all(g_tempDir, ec);
        g_tempDir.clear();
    }
}

static BOOL CALLBACK FindWindowForPid(HWND h, LPARAM lp) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == (DWORD)lp && IsWindowVisible(h) && GetWindow(h, GW_OWNER) == nullptr) {
        g_core = h;
        return FALSE;
    }
    return TRUE;
}

static void DestroyBuffers() {
    if (g_captureDC) {
        if (g_oldCapture) SelectObject(g_captureDC, g_oldCapture);
        if (g_captureBmp) DeleteObject(g_captureBmp);
        DeleteDC(g_captureDC);
    }
    if (g_backDC) {
        if (g_oldBack) SelectObject(g_backDC, g_oldBack);
        if (g_backBmp) DeleteObject(g_backBmp);
        DeleteDC(g_backDC);
    }
    g_captureDC = g_backDC = nullptr;
    g_captureBmp = g_backBmp = nullptr;
    g_oldCapture = g_oldBack = nullptr;
    g_backW = g_backH = 0;
}

static bool EnsureBuffers(HWND hwnd, int cw, int ch) {
    HDC wnd = GetDC(hwnd);
    if (!wnd) return false;
    if (!g_captureDC) {
        g_captureDC = CreateCompatibleDC(wnd);
        g_captureBmp = CreateCompatibleBitmap(wnd, (std::max)(1,g_srcW), (std::max)(1,g_srcH));
        g_oldCapture = SelectObject(g_captureDC, g_captureBmp);
    }
    if (!g_backDC || cw != g_backW || ch != g_backH) {
        if (g_backDC) {
            if (g_oldBack) SelectObject(g_backDC, g_oldBack);
            if (g_backBmp) DeleteObject(g_backBmp);
            DeleteDC(g_backDC);
        }
        g_backDC = CreateCompatibleDC(wnd);
        g_backBmp = CreateCompatibleBitmap(wnd, (std::max)(1,cw), (std::max)(1,ch));
        g_oldBack = SelectObject(g_backDC, g_backBmp);
        g_backW = cw; g_backH = ch;
    }
    ReleaseDC(hwnd, wnd);
    return g_captureDC && g_backDC;
}

static void RecalcDestination(HWND hwnd) {
    RECT c{}; GetClientRect(hwnd, &c);
    int cw = c.right, ch = c.bottom;
    if (cw <= 0 || ch <= 0 || g_srcW <= 0 || g_srcH <= 0) return;
    double s = (std::min)((double)cw / g_srcW, (double)ch / g_srcH);
    int w = (std::max)(1, (int)(g_srcW * s + 0.5));
    int h = (std::max)(1, (int)(g_srcH * s + 0.5));
    g_dst.left = (cw - w) / 2;
    g_dst.top = (ch - h) / 2;
    g_dst.right = g_dst.left + w;
    g_dst.bottom = g_dst.top + h;
}

static bool ToCorePoint(int x, int y, POINT& out) {
    int dw = g_dst.right - g_dst.left, dh = g_dst.bottom - g_dst.top;
    if (dw <= 0 || dh <= 0 || x < g_dst.left || x >= g_dst.right || y < g_dst.top || y >= g_dst.bottom) return false;
    out.x = (x - g_dst.left) * g_srcW / dw;
    out.y = (y - g_dst.top) * g_srcH / dh;
    return true;
}

static HWND DeepestChildAtPoint(HWND parent, POINT parentClientPt, POINT& targetClientPt) {
    HWND target = parent;
    POINT pt = parentClientPt;
    for (;;) {
        HWND child = ChildWindowFromPointEx(target, pt, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
        if (!child || child == target) break;
        POINT screen = pt;
        ClientToScreen(target, &screen);
        ScreenToClient(child, &screen);
        target = child;
        pt = screen;
    }
    targetClientPt = pt;
    return target;
}

static void ForwardMouse(UINT msg, WPARAM wp, LPARAM lp) {
    if (!g_core) return;
    POINT corePt{};
    int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
    if (!ToCorePoint(x, y, corePt)) return;

    const bool isDown = (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN);
    const bool isUp   = (msg == WM_LBUTTONUP || msg == WM_RBUTTONUP || msg == WM_MBUTTONUP);

    HWND target = nullptr;
    POINT targetPt{};
    if ((isUp || msg == WM_MOUSEMOVE) && g_mouseTarget && IsWindow(g_mouseTarget)) {
        target = g_mouseTarget;
        POINT screen = corePt;
        ClientToScreen(g_core, &screen);
        ScreenToClient(target, &screen);
        targetPt = screen;
    } else {
        target = DeepestChildAtPoint(g_core, corePt, targetPt);
    }
    if (!target) target = g_core;
    if (isDown) g_mouseTarget = target;

    LPARAM mapped = MAKELPARAM((SHORT)targetPt.x, (SHORT)targetPt.y);
    SendMessageW(target, msg, wp, mapped);
    if (isUp) g_mouseTarget = nullptr;
}

static void PaintScaled(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT c{}; GetClientRect(hwnd, &c);
    int cw = c.right, ch = c.bottom;
    if (cw <= 0 || ch <= 0 || !g_core || !EnsureBuffers(hwnd, cw, ch)) {
        FillRect(dc, &c, (HBRUSH)GetStockObject(BLACK_BRUSH));
        EndPaint(hwnd, &ps); return;
    }

    RECT black{0,0,cw,ch};
    FillRect(g_backDC, &black, (HBRUSH)GetStockObject(BLACK_BRUSH));

    HDC src = GetDC(g_core);
    if (src) {
        BitBlt(g_captureDC, 0, 0, g_srcW, g_srcH, src, 0, 0, SRCCOPY);
        ReleaseDC(g_core, src);
    }

    RecalcDestination(hwnd);
    SetStretchBltMode(g_backDC, COLORONCOLOR);
    StretchBlt(g_backDC, g_dst.left, g_dst.top, g_dst.right-g_dst.left, g_dst.bottom-g_dst.top,
               g_captureDC, 0,0,g_srcW,g_srcH, SRCCOPY);
    BitBlt(dc, 0,0,cw,ch,g_backDC,0,0,SRCCOPY);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: RecalcDestination(hwnd); InvalidateRect(hwnd,nullptr,FALSE); return 0;
    case WM_TIMER:
        if (g_process && WaitForSingleObject(g_process,0) == WAIT_OBJECT_0) { PostMessageW(hwnd,WM_CLOSE,0,0); return 0; }
        InvalidateRect(hwnd,nullptr,FALSE); return 0;
    case WM_PAINT: PaintScaled(hwnd); return 0;
    case WM_LBUTTONDOWN: SetCapture(hwnd); SetFocus(hwnd); ForwardMouse(msg,wp,lp); return 0;
    case WM_LBUTTONUP: ForwardMouse(msg,wp,lp); ReleaseCapture(); return 0;
    case WM_RBUTTONDOWN: SetCapture(hwnd); SetFocus(hwnd); ForwardMouse(msg,wp,lp); return 0;
    case WM_RBUTTONUP: ForwardMouse(msg,wp,lp); ReleaseCapture(); return 0;
    case WM_MBUTTONDOWN: SetCapture(hwnd); SetFocus(hwnd); ForwardMouse(msg,wp,lp); return 0;
    case WM_MBUTTONUP: ForwardMouse(msg,wp,lp); ReleaseCapture(); return 0;
    case WM_MOUSEMOVE: ForwardMouse(msg,wp,lp); return 0;
    case WM_MOUSEWHEEL:
        if (g_core) {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd,&p); POINT q{};
            if (ToCorePoint(p.x,p.y,q)) { ClientToScreen(g_core,&q); PostMessageW(g_core,msg,wp,MAKELPARAM((SHORT)q.x,(SHORT)q.y)); }
        } return 0;
    case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR: case WM_SYSKEYDOWN: case WM_SYSKEYUP:
        if (g_core) PostMessageW(g_core,msg,wp,lp); return 0;
    case WM_CLOSE:
        if (g_core && IsWindow(g_core)) PostMessageW(g_core, WM_CLOSE, 0, 0);
        DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        KillTimer(hwnd,1); DestroyBuffers();
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    CleanupOldTempDirs();
    Msg(L"Select your original kln89.exe in the next window.", MB_ICONINFORMATION);
    fs::path original;
    if (!ChooseOriginalExe(original)) return 0;

    fs::path srcDir = original.parent_path();
    fs::path navdb = srcDir / L"c_navdb.dat";
    fs::path user = srcDir / L"user.dat";
    if (!fs::exists(navdb) || !fs::exists(user)) {
        Msg(L"Required simulator data files were not found next to the selected kln89.exe.\n\nExpected:\n  c_navdb.dat\n  user.dat");
        return 2;
    }

    g_tempDir = MakeUniqueTempDir();
    if (g_tempDir.empty()) { Msg(L"Could not create a temporary working folder."); return 3; }
    g_originalUserDat = user;

    fs::path patched = g_tempDir / L"kln89_modern_core.exe";
    std::wstring why;
    if (!PatchOriginal(original, patched, why)) { CleanupOldTempDirs(); Msg(why); return 4; }

    try {
        fs::copy_file(navdb, g_tempDir / L"c_navdb.dat", fs::copy_options::overwrite_existing);
        fs::copy_file(user, g_tempDir / L"user.dat", fs::copy_options::overwrite_existing);
    } catch (...) {
        PersistUserDataAndCleanup(); Msg(L"Could not prepare the temporary working folder."); return 5;
    }

    std::wstring cmd = L"\"" + patched.wstring() + L"\"";
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdbuf(cmd.begin(), cmd.end()); cmdbuf.push_back(0);
    if (!CreateProcessW(patched.c_str(), cmdbuf.data(), nullptr, nullptr, FALSE, 0, nullptr, g_tempDir.c_str(), &si, &pi)) {
        DWORD err = GetLastError(); PersistUserDataAndCleanup();
        Msg(L"Failed to start patched KLN89 simulator.\nWindows error: " + std::to_wstring(err)); return 6;
    }
    CloseHandle(pi.hThread); g_process = pi.hProcess; g_pid = pi.dwProcessId;

    for (int i = 0; i < 240 && !g_core; ++i) {
        EnumWindows(FindWindowForPid, (LPARAM)g_pid);
        if (!g_core) Sleep(25);
    }
    if (!g_core) {
        Msg(L"KLN89 started, but its main window could not be found.");
        TerminateProcess(g_process, 1); WaitForSingleObject(g_process, 3000); CloseHandle(g_process); g_process = nullptr;
        PersistUserDataAndCleanup(); return 7;
    }

    RECT cr{}; GetClientRect(g_core, &cr); g_srcW = cr.right - cr.left; g_srcH = cr.bottom - cr.top;
    if (g_srcW < 100 || g_srcH < 100) {
        Msg(L"Unexpected KLN89 client size."); PostMessageW(g_core, WM_CLOSE, 0, 0);
        WaitForSingleObject(g_process, 3000); CloseHandle(g_process); g_process = nullptr; PersistUserDataAndCleanup(); return 8;
    }

    SetWindowPos(g_core, nullptr, -32000, -32000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);

    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH); wc.lpszClassName = L"KLN89ModernWindow";
    RegisterClassW(&wc);

    int initW = g_srcW * 2, initH = g_srcH * 2;
    RECT wr{0, 0, initW, initH}; AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, APP_TITLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, hi, nullptr);
    if (!hwnd) {
        Msg(L"Could not create scaler window."); PostMessageW(g_core, WM_CLOSE, 0, 0);
        WaitForSingleObject(g_process, 3000); CloseHandle(g_process); g_process = nullptr; PersistUserDataAndCleanup(); return 9;
    }
    SetTimer(hwnd, 1, 50, nullptr);
    SetFocus(hwnd);

    MSG m{};
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }

    if (g_process) {
        DWORD state = WaitForSingleObject(g_process, 5000);
        if (state == WAIT_TIMEOUT && g_core && IsWindow(g_core)) {
            PostMessageW(g_core, WM_CLOSE, 0, 0); WaitForSingleObject(g_process, 3000);
        }
        CloseHandle(g_process); g_process = nullptr;
    }
    PersistUserDataAndCleanup();
    return (int)m.wParam;
}
