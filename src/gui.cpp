// gui.cpp -- Win32 图形界面：打开图片 / 导出TXT矩阵 / 导入TXT矩阵 / 另存为图片
//            支持滚轮缩放、拖动平移、像素级网格与取色显示。
#include "gui.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <string>
#include <vector>

#include "gdiplus_io.h"
#include "image.h"
#include "matrix.h"
#include "util.h"

namespace im {
namespace {

// ------------------------------------------------------------------ 常量
const wchar_t* kMainClass = L"ImageMatrixMainWnd";
const wchar_t* kCanvasClass = L"ImageMatrixCanvasWnd";
const wchar_t* kTextClass = L"ImageMatrixTextWnd";
const wchar_t* kAskClass = L"ImageMatrixAskWnd";

enum {
    ID_BTN_OPEN = 1001,
    ID_BTN_EXPORT,
    ID_BTN_IMPORT,
    ID_BTN_SAVEAS,
    ID_CMB_FORMAT,
    ID_CHK_HEADER,
    ID_BTN_FIT,
    ID_BTN_ONE,
    ID_BTN_ZOOMIN,
    ID_BTN_ZOOMOUT,
    ID_BTN_GEN,
    ID_BTN_TEXT,
    ID_BTN_ABOUT,
    ID_CANVAS = 1200,
    ID_STATUS,
    // 文本窗口
    ID_TXT_EDIT = 1300,
    ID_TXT_SAVE,
    ID_TXT_COPY,
    ID_TXT_CLOSE,
    // 询问窗口
    ID_ASK_EDIT = 1400,
    ID_ASK_OK,
    ID_ASK_CANCEL,
};

const wchar_t* kFilterImages =
    L"所有支持的图片 (*.bmp;*.png;*.jpg;*.jpeg;*.gif;*.tif;*.tiff)\0*.bmp;*.png;*.jpg;*.jpeg;*.gif;*.tif;*.tiff\0"
    L"BMP 图片 (*.bmp)\0*.bmp\0PNG 图片 (*.png)\0*.png\0JPEG 图片 (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0"
    L"GIF 图片 (*.gif)\0*.gif\0TIFF 图片 (*.tif;*.tiff)\0*.tif;*.tiff\0所有文件 (*.*)\0*.*\0";
const wchar_t* kFilterTxt = L"文本矩阵 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
const wchar_t* kFilterSaveImg =
    L"PNG 图片 (*.png)\0*.png\0JPEG 图片 (*.jpg)\0*.jpg\0BMP 图片 (*.bmp)\0*.bmp\0"
    L"TIFF 图片 (*.tif)\0*.tif\0GIF 图片 (*.gif)\0*.gif\0所有文件 (*.*)\0*.*\0";

// ------------------------------------------------------------------ 状态
struct App {
    HINSTANCE inst = nullptr;
    HWND main = nullptr, canvas = nullptr, status = nullptr;
    HWND btn_open = nullptr, btn_export = nullptr, btn_import = nullptr, btn_saveas = nullptr;
    HWND btn_fit = nullptr, btn_one = nullptr, btn_zoomin = nullptr, btn_zoomout = nullptr;
    HWND btn_gen = nullptr, btn_text = nullptr, btn_about = nullptr;
    HWND lbl_fmt = nullptr, cmb_fmt = nullptr, chk_header = nullptr;
    HWND text_wnd = nullptr, text_edit = nullptr;
    HFONT font = nullptr, mono_font = nullptr;
    HBRUSH canvas_bg = nullptr;

    Image img;
    std::string path;            // 图片来源（图片文件或 TXT）
    bool from_txt = false;       // 当前图像是否来自 TXT 矩阵
    TxtFormat fmt = TxtFormat::CHANNELS;
    bool include_header = true;

    bool fit_mode = true;
    double zoom = 1.0;
    int pan_x = 0, pan_y = 0;
    int hover_x = -1, hover_y = -1;
    bool dragging = false;
    POINT drag_from{};
    int drag_pan_x = 0, drag_pan_y = 0;

    HBITMAP dib = nullptr;
    void* dib_bits = nullptr;
    HDC dib_dc = nullptr;
    HBITMAP dib_old = nullptr;
    int dib_w = 0, dib_h = 0;

    int canvas_w = 0, canvas_h = 0;
};
App g;

// ------------------------------------------------------------------ 小工具
std::wstring ws(const std::string& s) { return utf8_to_wide(s); }
std::wstring num(long long v) { return std::to_wstring(v); }

void apply_font(HWND h) {
    if (h && g.font) SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE);
}

void mb_info(const std::wstring& text, const std::wstring& title = L"提示") {
    MessageBoxW(g.main, text.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
}
void mb_warn(const std::wstring& text, const std::wstring& title = L"提示") {
    MessageBoxW(g.main, text.c_str(), title.c_str(), MB_OK | MB_ICONWARNING);
}
void mb_error(const std::wstring& text, const std::wstring& title = L"出错了") {
    MessageBoxW(g.main, text.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
}

struct WaitCursor {
    WaitCursor() { SetCursor(LoadCursorW(nullptr, IDC_WAIT)); }
    ~WaitCursor() { SetCursor(LoadCursorW(nullptr, IDC_ARROW)); }
};

bool open_file_dialog(const wchar_t* title, const wchar_t* filter, std::wstring& out) {
    wchar_t buf[4096] = L"";
    OPENFILENAMEW ofn;
    std::memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.main;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 4096;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return false;
    out = buf;
    return true;
}

bool save_file_dialog(const wchar_t* title, const wchar_t* filter, const wchar_t* def_ext,
                      const std::wstring& def_name, std::wstring& out) {
    wchar_t buf[4096] = L"";
    if (!def_name.empty()) {
        wcsncpy(buf, def_name.c_str(), 4000);
        buf[4000] = L'\0';
    }
    OPENFILENAMEW ofn;
    std::memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.main;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 4096;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = def_ext;
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return false;
    out = buf;
    return true;
}

// ------------------------------------------------------------------ 询问整数（输入宽度）
struct AskState {
    int def = 0;
    int value = 0;
    bool done = false;
    bool ok = false;
    HFONT font = nullptr;
};

LRESULT CALLBACK AskProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    AskState* st = (AskState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
            st = (AskState*)cs->lpCreateParams;
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)st);
            HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                                     16, 56, 372, 26, h, (HMENU)ID_ASK_EDIT, g.inst, nullptr);
            CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                            200, 100, 88, 30, h, (HMENU)ID_ASK_OK, g.inst, nullptr);
            CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                            300, 100, 88, 30, h, (HMENU)ID_ASK_CANCEL, g.inst, nullptr);
            wchar_t num[32];
            std::swprintf(num, 32, L"%d", st->def);
            SetWindowTextW(e, num);
            SendMessageW(e, EM_SETSEL, 0, -1);
            EnumChildWindows(h, [](HWND c, LPARAM p) -> BOOL {
                SendMessageW(c, WM_SETFONT, (WPARAM)p, TRUE);
                return TRUE;
            }, (LPARAM)st->font);
            SetFocus(e);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
            SetBkMode((HDC)wp, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        case WM_COMMAND:
            if (!st) break;
            if (LOWORD(wp) == ID_ASK_OK) {
                wchar_t buf[64] = L"";
                GetDlgItemTextW(h, ID_ASK_EDIT, buf, 63);
                int v = _wtoi(buf);
                if (v <= 0) {
                    MessageBoxW(h, L"请输入一个大于 0 的整数。", L"输入无效", MB_OK | MB_ICONWARNING);
                    SetFocus(GetDlgItem(h, ID_ASK_EDIT));
                    return 0;
                }
                st->value = v;
                st->ok = true;
                st->done = true;
                DestroyWindow(h);
                return 0;
            }
            if (LOWORD(wp) == ID_ASK_CANCEL) {
                st->ok = false;
                st->done = true;
                DestroyWindow(h);
                return 0;
            }
            break;
        case WM_CLOSE:
            if (st) { st->ok = false; st->done = true; }
            DestroyWindow(h);
            return 0;
        default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool ask_int(const wchar_t* title, const wchar_t* prompt, int def, int* out) {
    AskState st;
    st.def = def;
    st.value = def;
    st.font = g.font;

    RECT pr;
    GetWindowRect(g.main, &pr);
    const int W = 420, H = 180;
    const int x = pr.left + ((pr.right - pr.left) - W) / 2;
    const int y = pr.top + ((pr.bottom - pr.top) - H) / 2;

    HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME, kAskClass, title,
                             WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, x, y, W, H, g.main,
                             nullptr, g.inst, &st);
    if (!h) return false;
    // 提示文字（窗口创建后再放，避免 CREATE 结构体太复杂）
    HWND lbl = CreateWindowExW(0, L"STATIC", prompt, WS_CHILD | WS_VISIBLE, 16, 14, 372, 38, h,
                               nullptr, g.inst, nullptr);
    apply_font(lbl);
    SetWindowTextW(h, title);

    EnableWindow(g.main, FALSE);
    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(h, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(g.main, TRUE);
    SetForegroundWindow(g.main);
    if (st.ok && out) *out = st.value;
    return st.ok;
}

// ------------------------------------------------------------------ DIB 显示缓冲
void release_dib() {
    if (g.dib_dc) {
        if (g.dib_old) SelectObject(g.dib_dc, g.dib_old);
        DeleteDC(g.dib_dc);
        g.dib_dc = nullptr;
        g.dib_old = nullptr;
    }
    if (g.dib) {
        DeleteObject(g.dib);
        g.dib = nullptr;
    }
    g.dib_bits = nullptr;
    g.dib_w = g.dib_h = 0;
}

bool ensure_dib() {
    if (g.img.empty()) { release_dib(); return false; }
    if (g.dib && g.dib_w == g.img.w && g.dib_h == g.img.h) {
        std::memcpy(g.dib_bits, g.img.px.data(), g.img.px.size() * 4);
        return true;
    }
    release_dib();
    BITMAPINFO bi;
    std::memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = g.img.w;
    bi.bmiHeader.biHeight = -g.img.h;  // 负值 = 自上而下，与内部像素顺序一致
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    g.dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!g.dib || !bits) { release_dib(); return false; }
    g.dib_bits = bits;
    g.dib_w = g.img.w;
    g.dib_h = g.img.h;
    g.dib_dc = CreateCompatibleDC(nullptr);
    g.dib_old = (HBITMAP)SelectObject(g.dib_dc, g.dib);
    std::memcpy(g.dib_bits, g.img.px.data(), g.img.px.size() * 4);
    return true;
}

void reset_view() {
    g.fit_mode = true;
    g.zoom = 1.0;
    g.pan_x = g.pan_y = 0;
    g.hover_x = g.hover_y = -1;
}

// ------------------------------------------------------------------ 状态栏
void update_status() {
    if (!g.status) return;
    std::wstring p1, p2, p3;

    if (g.img.empty()) {
        p1 = L"未加载图片 —— 请点击“打开图片”，或把图片/TXT 直接拖到窗口里";
    } else {
        p1 = L"图片 ";
        if (!g.path.empty()) p1 += ws(file_name_of(g.path));
        p1 += L"   " + num(g.img.w) + L" x " + num(g.img.h) + L"   " +
              num((long long)g.img.px.size()) + L" 像素";
        if (g.from_txt) p1 += L"   [来自 TXT 矩阵]";
        else p1 += std::wstring(L"   [") + ws(format_name(format_from_path(g.path))) + L"]";
    }
    if (g.hover_x >= 0 && !g.img.empty()) {
        uint32_t c = g.img.at(g.hover_x, g.hover_y);
        wchar_t buf[256];
        std::swprintf(buf, 256, L"像素(%d, %d)  R=%d G=%d B=%d  Gray=%d  #%02X%02X%02X", g.hover_x,
                      g.hover_y, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF,
                      Image::gray_of((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF), (c >> 16) & 0xFF,
                      (c >> 8) & 0xFF, c & 0xFF);
        p2 = buf;
    } else {
        p2 = L"把鼠标移到图片上可查看该像素的 R/G/B/灰度值";
    }
    if (!g.img.empty()) {
        double scale = g.zoom;
        if (g.fit_mode) {
            double sx = (double)(g.canvas_w - 8) / g.img.w, sy = (double)(g.canvas_h - 8) / g.img.h;
            scale = std::min(sx, sy);
        }
        wchar_t buf[64];
        std::swprintf(buf, 64, L"缩放 %.0f%%%s", scale * 100.0, g.fit_mode ? L"（适应窗口）" : L"");
        p3 = buf;
    } else {
        p3 = L"滚轮缩放 / 拖动平移";
    }

    int parts[3];
    RECT rc;
    GetClientRect(g.status, &rc);
    const int w = rc.right;
    parts[0] = w * 45 / 100;
    parts[1] = w * 80 / 100;
    parts[2] = -1;
    SendMessageW(g.status, SB_SETPARTS, 3, (LPARAM)parts);
    SendMessageW(g.status, SB_SETTEXTW, 0, (LPARAM)p1.c_str());
    SendMessageW(g.status, SB_SETTEXTW, 1, (LPARAM)p2.c_str());
    SendMessageW(g.status, SB_SETTEXTW, 2, (LPARAM)p3.c_str());
}

// ------------------------------------------------------------------ 视图计算
void view_metrics(double& scale, int& dw, int& dh, int& ox, int& oy) {
    const int cw = g.canvas_w, ch = g.canvas_h;
    const int iw = g.img.w, ih = g.img.h;
    scale = g.zoom;
    if (g.fit_mode && iw > 0 && ih > 0 && cw > 8 && ch > 8) {
        double sx = (double)(cw - 8) / iw, sy = (double)(ch - 8) / ih;
        scale = std::min(sx, sy);
        if (scale <= 0.0) scale = 1.0;
    }
    dw = (int)std::lround(iw * scale);
    dh = (int)std::lround(ih * scale);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    if (dw <= cw) ox = (cw - dw) / 2;
    else {
        ox = g.pan_x;
        if (ox > 0) ox = 0;
        if (ox < cw - dw) ox = cw - dw;
    }
    if (dh <= ch) oy = (ch - dh) / 2;
    else {
        oy = g.pan_y;
        if (oy > 0) oy = 0;
        if (oy < ch - dh) oy = ch - dh;
    }
}

void set_zoom(double nz, bool keep_center) {
    if (g.img.empty()) return;
    nz = std::max(0.02, std::min(nz, 64.0));
    POINT anchor;
    if (keep_center) {
        anchor.x = g.canvas_w / 2;
        anchor.y = g.canvas_h / 2;
    } else {
        GetCursorPos(&anchor);
        ScreenToClient(g.canvas, &anchor);
    }
    double sc = 1.0;
    int dw = 0, dh = 0, ox = 0, oy = 0;
    view_metrics(sc, dw, dh, ox, oy);

    const double img_x = (sc > 0) ? (anchor.x - ox) / sc : 0.0;
    const double img_y = (sc > 0) ? (anchor.y - oy) / sc : 0.0;

    g.fit_mode = false;
    g.zoom = nz;

    view_metrics(sc, dw, dh, ox, oy);  // 此时 ox/oy 已按新缩放求值，重算锚点位置
    g.pan_x = (int)std::lround(anchor.x - img_x * sc);
    g.pan_y = (int)std::lround(anchor.y - img_y * sc);
    InvalidateRect(g.canvas, nullptr, FALSE);
    update_status();
}

void canvas_mouse_to_image(int mx, int my, int& ix, int& iy) {
    double sc = 1.0;
    int dw = 0, dh = 0, ox = 0, oy = 0;
    view_metrics(sc, dw, dh, ox, oy);
    ix = (sc > 0) ? (int)std::floor((mx - ox) / sc) : -1;
    iy = (sc > 0) ? (int)std::floor((my - oy) / sc) : -1;
    if (ix < 0 || iy < 0 || ix >= g.img.w || iy >= g.img.h) { ix = -1; iy = -1; }
}

// ------------------------------------------------------------------ 画布绘制
void paint_canvas(HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, g.canvas_bg);

    if (g.img.empty() || !g.dib) {
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(170, 172, 178));
        HFONT old = (HFONT)SelectObject(hdc, g.font);
        const wchar_t* tip =
            L"点击“打开图片”读取 BMP / PNG / JPEG / GIF / TIFF\n"
            L"或点击“导入TXT”把数值矩阵还原成图片\n"
            L"也可以把文件直接拖进窗口";
        DrawTextW(hdc, tip, -1, &rc, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        SelectObject(hdc, old);
        return;
    }

    double sc = 1.0;
    int dw = 0, dh = 0, ox = 0, oy = 0;
    view_metrics(sc, dw, dh, ox, oy);

    // 放大时用最近邻（保持像素方块清晰，方便看"矩阵"），缩小时用 HALFTONE 平滑
    const bool upscale = (dw >= g.img.w && dh >= g.img.h);
    SetStretchBltMode(hdc, upscale ? COLORONCOLOR : HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, nullptr);
    StretchBlt(hdc, ox, oy, dw, dh, g.dib_dc, 0, 0, g.img.w, g.img.h, SRCCOPY);

    // 像素网格（放大到 8 倍以上时显示，便于逐格观察像素值）
    if (sc >= 8.0) {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(96, 98, 104));
        HGDIOBJ old = SelectObject(hdc, pen);
        const int cl = (int)rc.left, ct = (int)rc.top, cr = (int)rc.right, cb = (int)rc.bottom;
        for (int x = 0; x <= g.img.w; ++x) {
            int px = ox + (int)std::lround(x * sc);
            if (px < cl - 1 || px > cr + 1) continue;
            MoveToEx(hdc, px, std::max(oy, ct), nullptr);
            LineTo(hdc, px, std::min(oy + dh, cb));
        }
        for (int y = 0; y <= g.img.h; ++y) {
            int py = oy + (int)std::lround(y * sc);
            if (py < ct - 1 || py > cb + 1) continue;
            MoveToEx(hdc, std::max(ox, cl), py, nullptr);
            LineTo(hdc, std::min(ox + dw, cr), py);
        }
        SelectObject(hdc, old);
        DeleteObject(pen);
    }

    // 边框
    RECT fr{ox - 1, oy - 1, ox + dw + 1, oy + dh + 1};
    FrameRect(hdc, &fr, (HBRUSH)GetStockObject(GRAY_BRUSH));

    // 鼠标所在像素高亮
    if (g.hover_x >= 0 && g.hover_y >= 0) {
        int px = ox + (int)std::lround(g.hover_x * sc);
        int py = oy + (int)std::lround(g.hover_y * sc);
        int pw = (int)std::lround(sc);
        if (pw < 4) pw = 4;
        RECT hr{px - 1, py - 1, px + pw + 1, py + pw + 1};
        if (hr.left < rc.left) hr.left = rc.left;
        if (hr.top < rc.top) hr.top = rc.top;
        if (hr.right > rc.right) hr.right = rc.right;
        if (hr.bottom > rc.bottom) hr.bottom = rc.bottom;
        if (hr.right > hr.left && hr.bottom > hr.top)
            FrameRect(hdc, &hr, (HBRUSH)GetStockObject(WHITE_BRUSH));
    }
}

// ------------------------------------------------------------------ 载入 / 导出 / 导入
void set_image(Image&& img, const std::string& path, bool from_txt) {
    g.img = std::move(img);
    g.path = path;
    g.from_txt = from_txt;
    reset_view();
    ensure_dib();
    InvalidateRect(g.canvas, nullptr, TRUE);
    update_status();
}

void open_image_path(const std::string& path) {
    Image tmp;
    std::string err;
    WaitCursor wc;
    if (!load_image(path, tmp, err)) {
        mb_error(L"无法打开图片：\n" + ws(path) + L"\n\n" + ws(err));
        return;
    }
    set_image(std::move(tmp), path, false);
}

void import_txt_path(const std::string& path) {
    Image tmp;
    ImportInfo info;
    std::string err;
    WaitCursor wc;
    if (!import_matrix_txt(path, 0, tmp, info, err)) {
        if (!info.needs_width) {
            mb_error(L"无法读取 TXT 矩阵：\n" + ws(path) + L"\n\n" + ws(err));
            return;
        }
        int w = 0;
        std::wstring prompt = L"这个 TXT 里没有宽高信息（可能没有注释头）。\n请给出图片宽度，程序按数值总个数推算高度：";
        if (!ask_int(L"需要图片宽度", prompt.c_str(), 64, &w)) return;
        WaitCursor wc2;
        if (!import_matrix_txt(path, w, tmp, info, err)) {
            mb_error(L"仍然无法读取：\n\n" + ws(err));
            return;
        }
    }
    const int W = tmp.w, H = tmp.h;
    set_image(std::move(tmp), path, true);

    std::wstring msg = L"已从 TXT 矩阵还原图片：" + num(W) + L" x " + num(H) + L"\n";
    msg += L"识别到的格式：" + ws(info.format_name) + L"\n";
    msg += L"参与计算的数值个数：" + num((long long)info.values);
    if (!info.note.empty()) msg += L"\n\n注意：" + ws(info.note);
    mb_info(msg, L"导入成功");
}

void do_open_image() {
    std::wstring path;
    if (!open_file_dialog(L"选择要识别的图片", kFilterImages, path)) return;
    open_image_path(wide_to_utf8(path));
}

void do_export_txt() {
    if (g.img.empty()) { mb_warn(L"请先打开一张图片。"); return; }

    std::wstring base = g.path.empty() ? L"image" : ws(strip_ext(file_name_of(g.path)));
    if (base.empty()) base = L"image";
    std::wstring def = base + L"_" + ws(txt_format_name(g.fmt)) + L".txt";

    std::wstring path;
    if (!save_file_dialog(L"导出数值矩阵到 TXT", kFilterTxt, L"txt", def, path)) return;

    ExportOptions opt;
    opt.fmt = g.fmt;
    opt.include_header = g.include_header;

    std::string err;
    WaitCursor wc;
    if (!export_matrix_txt(wide_to_utf8(path), g.img, opt, err)) {
        mb_error(L"导出失败：\n\n" + ws(err));
        return;
    }
    std::error_code ec;
    auto sz = std::filesystem::file_size(std::filesystem::path(path), ec);

    unsigned long long vals;
    switch (g.fmt) {
        case TxtFormat::CHANNELS: vals = (unsigned long long)g.img.w * g.img.h * 4; break;
        case TxtFormat::RGB: vals = (unsigned long long)g.img.w * g.img.h * 3; break;
        case TxtFormat::RGBA: vals = (unsigned long long)g.img.w * g.img.h * 4; break;
        default: vals = (unsigned long long)g.img.w * g.img.h; break;
    }
    std::wstring msg = L"矩阵已写入：" + std::wstring(path) + L"\n\n";
    msg += L"格式：" + ws(txt_format_name(g.fmt)) + L"\n";
    msg += L"尺寸：" + num(g.img.w) + L" x " + num(g.img.h) + L"\n";
    msg += L"数值个数：" + num((long long)vals) + L"\n";
    msg += L"文件大小：" + ws(ec ? "?" : human_bytes(sz));
    if (g.fmt == TxtFormat::CHANNELS) msg += L"\n（含 R、G、B、GRAY 四个矩阵）";
    mb_info(msg, L"导出成功");
}

void do_import_txt() {
    std::wstring path;
    if (!open_file_dialog(L"选择要还原的 TXT 矩阵", kFilterTxt, path)) return;
    import_txt_path(wide_to_utf8(path));
}

void do_save_as_image() {
    if (g.img.empty()) { mb_warn(L"请先打开图片或导入 TXT 矩阵。"); return; }
    std::wstring base = g.path.empty() ? L"image" : ws(strip_ext(file_name_of(g.path)));
    if (base.empty()) base = L"image";
    std::wstring path;
    if (!save_file_dialog(L"另存为图片", kFilterSaveImg, L"png", base + L".png", path)) return;

    std::string err;
    WaitCursor wc;
    if (!save_image(wide_to_utf8(path), g.img, err, 92)) {
        mb_error(L"保存失败：\n\n" + ws(err));
        return;
    }
    mb_info(L"图片已保存：\n" + std::wstring(path), L"保存成功");
}

void do_gen_test() {
    int w = 320;
    if (!ask_int(L"生成测试图案", L"请输入宽度（像素）：", 320, &w)) return;
    int h = 240;
    if (!ask_int(L"生成测试图案", L"请输入高度（像素）：", 240, &h)) return;
    if (w < 1 || h < 1 || (long long)w * h > 400000000LL) {
        mb_error(L"尺寸非法。");
        return;
    }
    set_image(make_test_pattern(w, h), std::string(), false);
}

void do_about() {
    const wchar_t* text =
        L"ImageMatrix 1.0 —— 图片 与 数值矩阵(TXT) 双向转换\n"
        L"（纯 C/C++ 编写：Win32 图形界面 + 自写 BMP 编解码 + Windows GDI+ 处理其它格式）\n"
        L"\n"
        L"【打开图片】BMP / PNG / JPEG / GIF / TIFF（也可直接把文件拖进窗口）\n"
        L"【导出TXT】把图像写成 R、G、B、灰度 数值矩阵，可原样导回\n"
        L"【导入TXT】把矩阵文本还原成图片；缺宽高时会询问宽度\n"
        L"【另存为图片】PNG / JPEG / BMP / GIF / TIFF\n"
        L"\n"
        L"矩阵格式：\n"
        L"  CHANNELS  R矩阵 + G矩阵 + B矩阵 + GRAY矩阵（默认）\n"
        L"  RGB       每行一个像素：R, G, B\n"
        L"  GRAY      灰度矩阵，每行对应图像一行\n"
        L"  HEX       每行一个像素：16 进制 RRGGBB\n"
        L"  RGBA      每行一个像素：R, G, B, A\n"
        L"\n"
        L"灰度公式：Y = 0.299R + 0.587G + 0.114B（BT.601，四舍五入）\n"
        L"快捷键：Ctrl+O 打开  Ctrl+S 导出TXT  Ctrl+T 导入TXT  Ctrl+E 另存为图片\n"
        L"鼠标：滚轮缩放（以光标为中心）、左键拖动平移、放大 10 倍以上显示像素网格";
    MessageBoxW(g.main, text, L"使用说明", MB_OK | MB_ICONINFORMATION);
}

// ------------------------------------------------------------------ 矩阵文本预览窗口
void refresh_text_window() {
    if (!g.text_wnd || !g.text_edit) return;
    ExportOptions opt;
    opt.fmt = g.fmt;
    opt.include_header = g.include_header;

    // 预览文本量控制：约 12 万字符以内（宽图就少显示几行）
    size_t max_lines = 400;
    if (g.img.w > 0) {
        size_t est = 120000 / ((size_t)g.img.w * 4 + 8);
        if (est < 20) est = 20;
        if (est > 400) est = 400;
        max_lines = est;
    }
    std::string s = build_matrix_text(g.img, opt, max_lines);

    std::wstring w = ws(s);
    std::wstring crlf;
    crlf.reserve(w.size() + w.size() / 8 + 8);
    for (wchar_t c : w) {
        if (c == L'\n') crlf.push_back(L'\r');
        crlf.push_back(c);
    }
    SetWindowTextW(g.text_edit, crlf.c_str());
    std::wstring title = L"矩阵文本预览 —— " + ws(txt_format_name(g.fmt));
    if (!g.img.empty())
        title += L"  (" + num(g.img.w) + L" x " + num(g.img.h) + L"，最多显示前 " +
                 num((long long)max_lines) + L" 行)";
    SetWindowTextW(g.text_wnd, title.c_str());
}

void layout_text_window(HWND h) {
    if (!h) return;
    RECT rc;
    GetClientRect(h, &rc);
    const int btn_h = 34, pad = 8;
    const int cw = (int)rc.right, chh = (int)rc.bottom;
    if (g.text_edit)
        MoveWindow(g.text_edit, pad, pad, std::max(80, cw - pad * 2),
                   std::max(60, chh - btn_h - pad * 3), TRUE);
    HWND b1 = GetDlgItem(h, ID_TXT_SAVE), b2 = GetDlgItem(h, ID_TXT_COPY),
         b3 = GetDlgItem(h, ID_TXT_CLOSE);
    int x = pad;
    const int y = chh - btn_h - pad;
    if (b1) { MoveWindow(b1, x, y, 150, btn_h, TRUE); x += 158; }
    if (b2) { MoveWindow(b2, x, y, 130, btn_h, TRUE); x += 138; }
    if (b3) { MoveWindow(b3, std::max(pad, cw - pad - 110), y, 110, btn_h, TRUE); }
}

LRESULT CALLBACK TextProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            layout_text_window(h);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == ID_TXT_SAVE) {
                if (!g.img.empty()) do_export_txt();
                refresh_text_window();
                return 0;
            }
            if (LOWORD(wp) == ID_TXT_COPY) {
                if (g.text_edit) {
                    SendMessageW(g.text_edit, EM_SETSEL, 0, -1);
                    SendMessageW(g.text_edit, WM_COPY, 0, 0);
                    SendMessageW(g.text_edit, EM_SETSEL, 0, 0);
                    mb_info(L"预览文本已复制到剪贴板。");
                }
                return 0;
            }
            if (LOWORD(wp) == ID_TXT_CLOSE) { DestroyWindow(h); return 0; }
            return 0;
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            g.text_wnd = nullptr;
            g.text_edit = nullptr;
            return 0;
        default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void do_show_text() {
    if (g.img.empty()) { mb_warn(L"请先打开图片或导入 TXT 矩阵。"); return; }
    if (!g.text_wnd) {
        g.text_wnd = CreateWindowExW(WS_EX_APPWINDOW, kTextClass, L"矩阵文本预览",
                                     WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN, CW_USEDEFAULT,
                                     CW_USEDEFAULT, 940, 660, g.main, nullptr, g.inst, nullptr);
        if (!g.text_wnd) return;
        g.text_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                      WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                                          ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                                      8, 8, 100, 100, g.text_wnd, (HMENU)ID_TXT_EDIT, g.inst, nullptr);
        SendMessageW(g.text_edit, WM_SETFONT, (WPARAM)g.mono_font, TRUE);
        struct {
            const wchar_t* t;
            int id;
        } btns[] = {{L"导出为 TXT 文件…", ID_TXT_SAVE}, {L"复制到剪贴板", ID_TXT_COPY}, {L"关闭", ID_TXT_CLOSE}};
        for (auto& b : btns) {
            HWND hb = CreateWindowExW(0, L"BUTTON", b.t, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 8, 8, 100,
                                      30, g.text_wnd, (HMENU)(INT_PTR)b.id, g.inst, nullptr);
            apply_font(hb);
        }
        layout_text_window(g.text_wnd);
    } else {
        ShowWindow(g.text_wnd, SW_SHOW);
    }
    refresh_text_window();
    SetForegroundWindow(g.text_wnd);
}

// ------------------------------------------------------------------ 主窗口
void layout_main(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int ph = 32;   // 控件高度
    const int row1 = 12;
    const int row2 = 52;
    const int gap = 8;

    int x = 12;
    struct Item {
        HWND h;
        int w;
    };
    Item r1[] = {{g.btn_open, 108}, {g.btn_export, 108}, {g.btn_import, 108}, {g.btn_saveas, 116}};
    for (auto& it : r1) {
        MoveWindow(it.h, x, row1, it.w, ph, TRUE);
        x += it.w + gap;
    }
    MoveWindow(g.lbl_fmt, x + 6, row1 + 6, 70, 20, TRUE);
    x += 6 + 76;
    MoveWindow(g.cmb_fmt, x, row1, 200, 240, TRUE);
    x += 200 + gap;
    MoveWindow(g.chk_header, x, row1 + 6, 130, 22, TRUE);

    x = 12;
    Item r2[] = {{g.btn_fit, 100}, {g.btn_one, 68}, {g.btn_zoomin, 64},
                 {g.btn_zoomout, 64}, {g.btn_gen, 118}, {g.btn_text, 118}, {g.btn_about, 100}};
    for (auto& it : r2) {
        MoveWindow(it.h, x, row2, it.w, ph, TRUE);
        x += it.w + gap;
    }

    RECT sr;
    int status_h = 24;
    if (g.status) {
        GetWindowRect(g.status, &sr);
        status_h = sr.bottom - sr.top;
    }
    const int top = row2 + ph + 10;
    const int client_w = (int)rc.right, client_h = (int)rc.bottom;
    MoveWindow(g.canvas, 10, top, client_w - 20,
               std::max(50, client_h - top - status_h - 8), TRUE);
    SendMessageW(g.status, WM_SIZE, 0, 0);
}

HWND make_ctrl(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
    HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent,
                            (HMENU)(INT_PTR)id, g.inst, nullptr);
    apply_font(h);
    return h;
}

void create_children(HWND hwnd) {
    g.btn_open = make_ctrl(hwnd, L"BUTTON", L"打开图片…", WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_OPEN);
    g.btn_export = make_ctrl(hwnd, L"BUTTON", L"导出为 TXT…", WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_EXPORT);
    g.btn_import = make_ctrl(hwnd, L"BUTTON", L"导入 TXT…", WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_IMPORT);
    g.btn_saveas = make_ctrl(hwnd, L"BUTTON", L"另存为图片…", WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_SAVEAS);

    g.lbl_fmt = make_ctrl(hwnd, L"STATIC", L"矩阵格式:", SS_LEFT, 0);
    g.cmb_fmt = make_ctrl(hwnd, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, ID_CMB_FORMAT);
    for (TxtFormat f : all_txt_formats()) {
        std::wstring label = ws(txt_format_name(f));
        if (f == TxtFormat::CHANNELS) label += L"（R/G/B/灰度 四个矩阵）";
        else if (f == TxtFormat::RGB) label += L"（每行一个像素）";
        else if (f == TxtFormat::GRAY) label += L"（灰度矩阵）";
        else if (f == TxtFormat::HEX) label += L"（16 进制）";
        else if (f == TxtFormat::RGBA) label += L"（含透明度）";
        SendMessageW(g.cmb_fmt, CB_ADDSTRING, 0, (LPARAM)label.c_str());
    }
    SendMessageW(g.cmb_fmt, CB_SETCURSEL, 0, 0);
    g.fmt = TxtFormat::CHANNELS;

    g.chk_header = make_ctrl(hwnd, L"BUTTON", L"写注释头", WS_TABSTOP | BS_AUTOCHECKBOX, ID_CHK_HEADER);
    SendMessageW(g.chk_header, BM_SETCHECK, BST_CHECKED, 0);
    g.include_header = true;

    g.btn_fit = make_ctrl(hwnd, L"BUTTON", L"适应窗口", WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_FIT);
    g.btn_one = make_ctrl(hwnd, L"BUTTON", L"1:1", WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_ONE);
    g.btn_zoomin = make_ctrl(hwnd, L"BUTTON", L"放大 +", WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_ZOOMIN);
    g.btn_zoomout = make_ctrl(hwnd, L"BUTTON", L"缩小 -", WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_ZOOMOUT);
    g.btn_gen = make_ctrl(hwnd, L"BUTTON", L"生成测试图案", WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_GEN);
    g.btn_text = make_ctrl(hwnd, L"BUTTON", L"查看矩阵文本", WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_TEXT);
    g.btn_about = make_ctrl(hwnd, L"BUTTON", L"使用说明", WS_TABSTOP | BS_PUSHBUTTON, ID_BTN_ABOUT);

    g.canvas = CreateWindowExW(WS_EX_CLIENTEDGE, kCanvasClass, L"",
                               WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 100, 100, hwnd,
                               (HMENU)ID_CANVAS, g.inst, nullptr);
    apply_font(g.canvas);
}

void load_dropped_file(const std::string& path) {
    if (iends_with(path, ".txt")) import_txt_path(path);
    else open_image_path(path);
}

LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            paint_canvas(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            g.canvas_w = LOWORD(lp);
            g.canvas_h = HIWORD(lp);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
            if (g.dragging && !g.fit_mode) {
                g.pan_x = g.drag_pan_x + (mx - g.drag_from.x);
                g.pan_y = g.drag_pan_y + (my - g.drag_from.y);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            int ix = -1, iy = -1;
            if (!g.img.empty()) canvas_mouse_to_image(mx, my, ix, iy);
            if (ix != g.hover_x || iy != g.hover_y) {
                g.hover_x = ix;
                g.hover_y = iy;
                InvalidateRect(hwnd, nullptr, FALSE);
                update_status();
            }
            // 光标形状提示
            if (!g.img.empty()) {
                bool big = false;
                double sc = 1.0;
                int dw = 0, dh = 0, ox = 0, oy = 0;
                view_metrics(sc, dw, dh, ox, oy);
                big = (dw > g.canvas_w || dh > g.canvas_h);
                SetCursor(LoadCursorW(nullptr, (g.dragging || (big && !g.fit_mode)) ? IDC_SIZEALL : IDC_CROSS));
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (g.img.empty()) return 0;
            int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
            g.dragging = true;
            g.drag_from.x = mx;
            g.drag_from.y = my;
            g.drag_pan_x = g.pan_x;
            g.drag_pan_y = g.pan_y;
            SetCapture(hwnd);
            return 0;
        }
        case WM_LBUTTONUP:
            if (g.dragging) {
                g.dragging = false;
                ReleaseCapture();
            }
            return 0;
        case WM_MOUSEWHEEL: {
            if (g.img.empty()) return 0;
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            double factor = (delta > 0) ? 1.25 : 1.0 / 1.25;
            double sc = 1.0;
            int dw = 0, dh = 0, ox = 0, oy = 0;
            view_metrics(sc, dw, dh, ox, oy);
            set_zoom(sc * factor, false);
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && !g.img.empty()) {
                SetCursor(LoadCursorW(nullptr, IDC_CROSS));
                return TRUE;
            }
            break;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g.main = hwnd;
            create_children(hwnd);
            DragAcceptFiles(hwnd, TRUE);
            layout_main(hwnd);
            update_status();
            return 0;
        case WM_SIZE:
            if (g.status) {
                SendMessageW(g.status, WM_SIZE, 0, 0);
                layout_main(hwnd);
            }
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = (MINMAXINFO*)lp;
            mmi->ptMinTrackSize.x = 900;
            mmi->ptMinTrackSize.y = 560;
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(wp)) {
                case ID_BTN_OPEN: do_open_image(); return 0;
                case ID_BTN_EXPORT: do_export_txt(); return 0;
                case ID_BTN_IMPORT: do_import_txt(); return 0;
                case ID_BTN_SAVEAS: do_save_as_image(); return 0;
                case ID_BTN_FIT:
                    reset_view();
                    InvalidateRect(g.canvas, nullptr, TRUE);
                    update_status();
                    return 0;
                case ID_BTN_ONE: set_zoom(1.0, true); return 0;
                case ID_BTN_ZOOMIN: {
                    double sc = 1.0;
                    int a = 0, b = 0, c = 0, d = 0;
                    view_metrics(sc, a, b, c, d);
                    set_zoom(sc * 1.5, true);
                    return 0;
                }
                case ID_BTN_ZOOMOUT: {
                    double sc = 1.0;
                    int a = 0, b = 0, c = 0, d = 0;
                    view_metrics(sc, a, b, c, d);
                    set_zoom(sc / 1.5, true);
                    return 0;
                }
                case ID_BTN_GEN: do_gen_test(); return 0;
                case ID_BTN_TEXT: do_show_text(); return 0;
                case ID_BTN_ABOUT: do_about(); return 0;
                case ID_CMB_FORMAT:
                    if (HIWORD(wp) == CBN_SELCHANGE) {
                        int sel = (int)SendMessageW(g.cmb_fmt, CB_GETCURSEL, 0, 0);
                        auto all = all_txt_formats();
                        if (sel >= 0 && sel < (int)all.size()) {
                            g.fmt = all[sel];
                            refresh_text_window();
                        }
                    }
                    return 0;
                case ID_CHK_HEADER:
                    g.include_header = (SendMessageW(g.chk_header, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    refresh_text_window();
                    return 0;
                default: break;
            }
            break;
        }
        case WM_DROPFILES: {
            HDROP hd = (HDROP)wp;
            UINT n = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
            if (n > 0) {
                wchar_t buf[4096];
                if (DragQueryFileW(hd, 0, buf, 4096)) load_dropped_file(wide_to_utf8(buf));
            }
            DragFinish(hd);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            release_dib();
            PostQuitMessage(0);
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool register_classes() {
    WNDCLASSEXW wc;
    std::memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hInstance = g.inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainProc;
    wc.lpszClassName = kMainClass;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    if (!RegisterClassExW(&wc)) return false;

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = CanvasProc;
    wc.lpszClassName = kCanvasClass;
    wc.hbrBackground = nullptr;
    if (!RegisterClassExW(&wc)) return false;

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = TextProc;
    wc.lpszClassName = kTextClass;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&wc)) return false;

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = AskProc;
    wc.lpszClassName = kAskClass;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&wc)) return false;

    return true;
}

void make_fonts() {
    NONCLIENTMETRICSW ncm;
    std::memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    LOGFONTW lf;
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        lf = ncm.lfMessageFont;
    } else {
        std::memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -12;
        wcscpy(lf.lfFaceName, L"Segoe UI");
    }
    lf.lfHeight = -14;  // 稍大一点，中文更清楚
    g.font = CreateFontIndirectW(&lf);

    LOGFONTW mf;
    std::memset(&mf, 0, sizeof(mf));
    mf.lfHeight = -14;
    mf.lfWeight = FW_NORMAL;
    mf.lfCharSet = DEFAULT_CHARSET;
    mf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    wcscpy(mf.lfFaceName, L"Consolas");
    g.mono_font = CreateFontIndirectW(&mf);
    if (!g.mono_font) g.mono_font = g.font;
}

} // namespace

int run_gui() {
    if (!gdiplus_startup()) {
        MessageBoxW(nullptr, L"GDI+ 初始化失败，无法处理 PNG/JPEG 等格式。", L"ImageMatrix",
                    MB_OK | MB_ICONERROR);
        return 1;
    }
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    g.inst = GetModuleHandleW(nullptr);
    g.canvas_bg = CreateSolidBrush(RGB(56, 58, 64));
    make_fonts();
    if (!register_classes()) {
        MessageBoxW(nullptr, L"窗口类注册失败。", L"ImageMatrix", MB_OK | MB_ICONERROR);
        gdiplus_shutdown();
        return 1;
    }

    g.main = CreateWindowExW(WS_EX_ACCEPTFILES, kMainClass, L"ImageMatrix —— 图片 ↔ 数值矩阵(R/G/B/灰度)",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1180, 780, nullptr,
                             nullptr, g.inst, nullptr);
    if (!g.main) {
        MessageBoxW(nullptr, L"主窗口创建失败。", L"ImageMatrix", MB_OK | MB_ICONERROR);
        gdiplus_shutdown();
        return 1;
    }
    g.status = CreateStatusWindowW(WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, L"", g.main, ID_STATUS);
    apply_font(g.status);
    SendMessageW(g.main, WM_SIZE, 0, 0);

    ShowWindow(g.main, SW_SHOW);
    UpdateWindow(g.main);
    update_status();

    ACCEL a[4];
    std::memset(a, 0, sizeof(a));
    a[0].fVirt = FVIRTKEY | FCONTROL; a[0].key = 'O'; a[0].cmd = ID_BTN_OPEN;
    a[1].fVirt = FVIRTKEY | FCONTROL; a[1].key = 'S'; a[1].cmd = ID_BTN_EXPORT;
    a[2].fVirt = FVIRTKEY | FCONTROL; a[2].key = 'T'; a[2].cmd = ID_BTN_IMPORT;
    a[3].fVirt = FVIRTKEY | FCONTROL; a[3].key = 'E'; a[3].cmd = ID_BTN_SAVEAS;
    HACCEL hacc = CreateAcceleratorTableW(a, 4);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (hacc && TranslateAcceleratorW(g.main, hacc, &msg)) continue;
        if (g.text_wnd && IsDialogMessageW(g.text_wnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hacc) DestroyAcceleratorTable(hacc);
    release_dib();
    if (g.canvas_bg) DeleteObject(g.canvas_bg);
    if (g.mono_font && g.mono_font != g.font) DeleteObject(g.mono_font);
    if (g.font) DeleteObject(g.font);
    gdiplus_shutdown();
    return 0;
}

} // namespace im
