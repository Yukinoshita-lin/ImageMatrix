// gdiplus_io.cpp -- 通过 Windows 自带 GDI+ 读写 PNG / JPEG / GIF / TIFF
// 仅调用系统组件（gdiplus.dll），不引入任何第三方库；代码全部为 C++。
#include "gdiplus_io.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>

#include <gdiplus.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "util.h"

namespace im {
namespace {

ULONG_PTR g_token = 0;
bool g_started = false;

std::string status_text(Gdiplus::Status st) {
    switch (st) {
        case Gdiplus::Ok: return "Ok";
        case Gdiplus::GenericError: return "GenericError";
        case Gdiplus::InvalidParameter: return "InvalidParameter";
        case Gdiplus::OutOfMemory: return "OutOfMemory";
        case Gdiplus::ObjectBusy: return "ObjectBusy";
        case Gdiplus::InsufficientBuffer: return "InsufficientBuffer";
        case Gdiplus::Win32Error: return "Win32Error";
        case Gdiplus::FileNotFound: return "FileNotFound";
        case Gdiplus::UnknownImageFormat: return "UnknownImageFormat";
        case Gdiplus::NotImplemented: return "NotImplemented";
        default: break;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Status(%d)", (int)st);
    return buf;
}

const wchar_t* mime_for_ext(const std::string& ext) {
    if (ext == ".png") return L"image/png";
    if (ext == ".jpg" || ext == ".jpeg" || ext == ".jpe" || ext == ".jfif") return L"image/jpeg";
    if (ext == ".bmp" || ext == ".dib") return L"image/bmp";
    if (ext == ".gif") return L"image/gif";
    if (ext == ".tif" || ext == ".tiff") return L"image/tiff";
    return nullptr;
}

bool find_encoder(const wchar_t* mime, CLSID& clsid) {
    UINT num = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0) return false;
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo* info = (Gdiplus::ImageCodecInfo*)buf.data();
    if (Gdiplus::GetImageEncoders(num, size, info) != Gdiplus::Ok) return false;
    for (UINT i = 0; i < num; ++i) {
        if (info[i].MimeType && _wcsicmp(info[i].MimeType, mime) == 0) {
            clsid = info[i].Clsid;
            return true;
        }
    }
    return false;
}

// 个别编码器（如 GIF）不接受 32bppARGB，退化为 24bpp 再存一次
bool save_direct(Gdiplus::Bitmap& bmp, const CLSID& clsid, const wchar_t* path,
                 Gdiplus::EncoderParameters* params) {
    return bmp.Save(path, &clsid, params) == Gdiplus::Ok;
}

bool save_via_24bpp(Gdiplus::Bitmap& src, const CLSID& clsid, const wchar_t* path,
                    Gdiplus::EncoderParameters* params) {
    const UINT w = src.GetWidth(), h = src.GetHeight();
    if (w == 0 || h == 0) return false;
    Gdiplus::Bitmap dst(w, h, PixelFormat24bppRGB);
    if (dst.GetLastStatus() != Gdiplus::Ok) return false;
    {
        Gdiplus::Graphics g(&dst);
        if (g.GetLastStatus() != Gdiplus::Ok) return false;
        g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        if (g.DrawImage(&src, Gdiplus::Rect(0, 0, (INT)w, (INT)h), 0, 0, (INT)w, (INT)h,
                        Gdiplus::UnitPixel) != Gdiplus::Ok)
            return false;
    }
    return dst.Save(path, &clsid, params) == Gdiplus::Ok;
}

} // namespace

bool gdiplus_startup() {
    if (g_started) return true;
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&g_token, &input, nullptr) != Gdiplus::Ok) return false;
    g_started = true;
    return true;
}

void gdiplus_shutdown() {
    if (!g_started) return;
    Gdiplus::GdiplusShutdown(g_token);
    g_started = false;
    g_token = 0;
}

bool gdiplus_load(const std::string& path, Image& out, std::string& err) {
    if (!gdiplus_startup()) { err = "GDI+ 初始化失败"; return false; }
    std::wstring wp = utf8_to_wide(path);
    if (wp.empty()) { err = "文件名转换失败"; return false; }

    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(wp.c_str(), FALSE);
    if (!bmp) { err = "无法打开图片文件"; return false; }

    Gdiplus::Status st = bmp->GetLastStatus();
    if (st != Gdiplus::Ok) {
        delete bmp;
        err = "图片解码失败：" + status_text(st) + "（文件可能损坏或格式不受支持）";
        return false;
    }

    const UINT W = bmp->GetWidth(), H = bmp->GetHeight();
    if (W == 0 || H == 0) {
        delete bmp;
        err = "图片尺寸为 0";
        return false;
    }
    if ((unsigned long long)W * H > 400000000ull) {
        delete bmp;
        err = "图片像素过多（超过 4 亿），已拒绝加载";
        return false;
    }

    Gdiplus::Rect rect(0, 0, (INT)W, (INT)H);
    Gdiplus::BitmapData bd;
    std::memset(&bd, 0, sizeof(bd));
    st = bmp->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd);
    if (st != Gdiplus::Ok) {
        delete bmp;
        err = "锁定像素数据失败：" + status_text(st);
        return false;
    }

    out.reset((int)W, (int)H);
    const uint8_t* base = (const uint8_t*)bd.Scan0;
    const ptrdiff_t stride = (ptrdiff_t)bd.Stride;
    for (UINT y = 0; y < H; ++y) {
        const uint8_t* src = (stride >= 0) ? (base + (ptrdiff_t)y * stride)
                                          : (base + (ptrdiff_t)(H - 1 - y) * (-stride));
        std::memcpy(out.row((int)y), src, (size_t)W * 4);
    }
    bmp->UnlockBits(&bd);
    delete bmp;
    return true;
}

bool gdiplus_save(const std::string& path, const Image& img, std::string& err, int jpeg_quality) {
    if (!gdiplus_startup()) { err = "GDI+ 初始化失败"; return false; }
    if (img.empty()) { err = "空图像"; return false; }

    const std::string ext = to_lower(ext_of(path));
    const wchar_t* mime = mime_for_ext(ext);
    if (!mime) {
        err = "不支持的输出格式 \"" + ext + "\"（可用：.png .jpg .bmp .gif .tif）";
        return false;
    }
    CLSID clsid;
    if (!find_encoder(mime, clsid)) {
        err = "系统中找不到该格式的编码器";
        return false;
    }
    std::wstring wp = utf8_to_wide(path);

    // JPEG 质量参数
    Gdiplus::EncoderParameters params;
    std::memset(&params, 0, sizeof(params));
    ULONG quality = (ULONG)(jpeg_quality < 1 ? 1 : (jpeg_quality > 100 ? 100 : jpeg_quality));
    if (wcscmp(mime, L"image/jpeg") == 0) {
        CLSID qid;
        if (SUCCEEDED(CLSIDFromString(L"{1d5be4b5-fa4a-452d-9cdd-5db35105e7eb}", &qid))) {
            params.Count = 1;
            params.Parameter[0].Guid = qid;
            params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
            params.Parameter[0].NumberOfValues = 1;
            params.Parameter[0].Value = &quality;
        }
    }
    Gdiplus::EncoderParameters* pparams = params.Count ? &params : nullptr;

    bool ok = false;
    {
        Gdiplus::Bitmap bmp(img.w, img.h, img.w * 4, PixelFormat32bppARGB,
                            (BYTE*)img.px.data());
        if (bmp.GetLastStatus() != Gdiplus::Ok) {
            err = "创建位图对象失败：" + status_text(bmp.GetLastStatus());
            return false;
        }
        ok = save_direct(bmp, clsid, wp.c_str(), pparams);
        if (!ok) ok = save_via_24bpp(bmp, clsid, wp.c_str(), pparams);  // GIF 等格式兜底
    }
    if (!ok) {
        err = "保存失败：编码器拒绝了该图像（路径不可写或格式限制）";
        return false;
    }
    return true;
}

std::vector<std::string> gdiplus_writable_exts() {
    std::vector<std::string> v;
    if (!gdiplus_startup()) return v;
    UINT num = 0, size = 0;
    if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0) return v;
    std::vector<BYTE> buf(size);
    Gdiplus::ImageCodecInfo* info = (Gdiplus::ImageCodecInfo*)buf.data();
    if (Gdiplus::GetImageEncoders(num, size, info) != Gdiplus::Ok) return v;
    for (UINT i = 0; i < num; ++i) {
        if (!info[i].FilenameExtension) continue;
        std::wstring exts = info[i].FilenameExtension;  // 形如 "*.BMP;*.DIB"
        size_t pos = 0;
        while (pos < exts.size()) {
            size_t semi = exts.find(L';', pos);
            std::wstring one = exts.substr(pos, semi == std::wstring::npos ? std::wstring::npos : semi - pos);
            pos = (semi == std::wstring::npos) ? exts.size() : semi + 1;
            one = trim_w(one);
            if (one.size() > 1 && one[0] == L'*') v.push_back(to_lower(wide_to_utf8(one.substr(1))));
        }
    }
    return v;
}

} // namespace im
