// image.cpp -- 核心图像实现：格式识别、统一的读写入口、测试图案生成
#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "bmp.h"
#include "gdiplus_io.h"
#include "util.h"

namespace im {

Image Image::scaled_nearest(int nw, int nh) const {
    Image o;
    if (empty() || nw <= 0 || nh <= 0) return o;
    o.reset(nw, nh);
    for (int y = 0; y < nh; ++y) {
        int sy = (int)((int64_t)y * h / nh);
        if (sy >= h) sy = h - 1;
        for (int x = 0; x < nw; ++x) {
            int sx = (int)((int64_t)x * w / nw);
            if (sx >= w) sx = w - 1;
            o.at(x, y) = at(sx, sy);
        }
    }
    return o;
}

FileFormat format_from_path(const std::string& path) {
    std::string e = to_lower(ext_of(path));
    if (e == ".bmp" || e == ".dib") return FileFormat::BMP;
    if (e == ".png") return FileFormat::PNG;
    if (e == ".jpg" || e == ".jpeg" || e == ".jpe" || e == ".jfif") return FileFormat::JPEG;
    if (e == ".gif") return FileFormat::GIF;
    if (e == ".tif" || e == ".tiff") return FileFormat::TIFF;
    return FileFormat::Unknown;
}

const char* format_name(FileFormat f) {
    switch (f) {
        case FileFormat::BMP: return "BMP";
        case FileFormat::PNG: return "PNG";
        case FileFormat::JPEG: return "JPEG";
        case FileFormat::GIF: return "GIF";
        case FileFormat::TIFF: return "TIFF";
        default: return "UNKNOWN";
    }
}

bool load_image(const std::string& path, Image& out, std::string& err) {
    if (path.empty()) { err = "文件名为空"; return false; }

    if (format_from_path(path) == FileFormat::BMP) {
        // BMP 先走自写解码器（不依赖系统组件），失败再交给 GDI+ 兜底
        std::string e1;
        if (bmp_load(path, out, e1)) return true;
        std::string e2;
        if (gdiplus_load(path, out, e2)) return true;
        err = "BMP 解码失败：" + e1;
        return false;
    }
    return gdiplus_load(path, out, err);
}

bool save_image(const std::string& path, const Image& img, std::string& err, int jpeg_quality) {
    if (img.empty()) { err = "没有可保存的图像数据"; return false; }
    if (path.empty()) { err = "文件名为空"; return false; }

    if (format_from_path(path) == FileFormat::BMP) {
        std::string e1;
        if (bmp_save(path, img, e1)) return true;
        std::string e2;
        if (gdiplus_save(path, img, e2, jpeg_quality)) return true;
        err = "BMP 编码失败：" + e1;
        return false;
    }
    return gdiplus_save(path, img, err, jpeg_quality);
}

// 测试图案：上部彩色渐变，中部灰阶阶梯，下部标准彩条 + 圆 + 方块
Image make_test_pattern(int w, int h) {
    Image im;
    if (w <= 0 || h <= 0) return im;
    im.reset(w, h);

    const int band = h / 3;
    const int gray_steps = 16;
    const uint32_t bars[8] = {
        0xFFFFFFFFu, 0xFFFFFF00u, 0xFF00FFFFu, 0xFF00FF00u,
        0xFFFF00FFu, 0xFFFF0000u, 0xFF0000FFu, 0xFF000000u,
    };

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint32_t c;
            if (y < band) {
                // 横向 R 渐变 + 纵向 G 渐变
                int r = (w > 1) ? x * 255 / (w - 1) : 0;
                int g = (band > 1) ? y * 255 / (band - 1) : 0;
                c = Image::pack(r, g, 128);
            } else if (y < band * 2) {
                // 灰阶阶梯
                int step = (w > 0) ? (x * gray_steps / w) : 0;
                int v = (gray_steps > 1) ? step * 255 / (gray_steps - 1) : 0;
                c = Image::pack(v, v, v);
            } else {
                int idx = (w > 0) ? (x * 8 / w) : 0;
                if (idx > 7) idx = 7;
                c = bars[idx];
            }
            im.at(x, y) = c;
        }
    }

    // 圆心处画一个方块和一个圆环，方便肉眼确认分辨率与灰度
    int cx = w / 2, cy = h / 2;
    int rad = (w < h ? w : h) / 6;
    if (rad < 2) rad = 2;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int dx = x - cx, dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if (d2 <= rad * rad) {
                int d = (int)std::lround(std::sqrt((double)d2));
                if (d >= rad - 2 || (x % 8 == 0)) im.at(x, y) = Image::pack(255, 255, 255);
            }
        }
    }
    return im;
}

} // namespace im
