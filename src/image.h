// image.h -- 核心图像类型与编解码入口
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace im {

// 内部像素格式：每像素 32 位，值为 0xAARRGGBB（内存字节序为 B,G,R,A，
// 与 Windows DIB / GDI+ PixelFormat32bppARGB 完全一致，因此显示时零拷贝）。
struct Image {
    int w = 0;
    int h = 0;
    std::vector<uint32_t> px;

    bool empty() const { return w <= 0 || h <= 0 || px.size() != (size_t)w * (size_t)h; }

    void reset(int nw, int nh) {
        w = nw > 0 ? nw : 0;
        h = nh > 0 ? nh : 0;
        px.assign((size_t)w * (size_t)h, 0xFF000000u);
    }

    void fill(uint32_t c) {
        for (auto& v : px) v = c;
    }

    uint32_t& at(int x, int y) { return px[(size_t)y * (size_t)w + (size_t)x]; }
    uint32_t  at(int x, int y) const { return px[(size_t)y * (size_t)w + (size_t)x]; }

    uint32_t* row(int y) { return px.data() + (size_t)y * (size_t)w; }
    const uint32_t* row(int y) const { return px.data() + (size_t)y * (size_t)w; }

    int r(int x, int y) const { return (int)((at(x, y) >> 16) & 0xFFu); }
    int g(int x, int y) const { return (int)((at(x, y) >> 8) & 0xFFu); }
    int b(int x, int y) const { return (int)(at(x, y) & 0xFFu); }
    int a(int x, int y) const { return (int)((at(x, y) >> 24) & 0xFFu); }

    // 灰度值：ITU-R BT.601 亮度公式（四舍五入）
    int gray(int x, int y) const { return gray_of(r(x, y), g(x, y), b(x, y)); }

    static int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

    static int gray_of(int r, int g, int b) {
        return clamp255((299 * r + 587 * g + 114 * b + 500) / 1000);
    }

    static uint32_t pack(int r, int g, int b, int a = 255) {
        return ((uint32_t)clamp255(a) << 24) | ((uint32_t)clamp255(r) << 16) |
               ((uint32_t)clamp255(g) << 8) | (uint32_t)clamp255(b);
    }

    // 是否存在真正的透明像素（全 255 视为不透明）
    bool has_alpha() const {
        for (auto v : px)
            if (((v >> 24) & 0xFFu) != 0xFFu) return true;
        return false;
    }

    // 按整数倍最近邻放大/缩小（GUI 放大时显示更"像素化"，便于观察矩阵）
    Image scaled_nearest(int nw, int nh) const;
};

enum class FileFormat { Unknown, BMP, PNG, JPEG, GIF, TIFF };

FileFormat format_from_path(const std::string& path);
const char* format_name(FileFormat f);

// 读取图片（BMP 走自写解码器，其它格式走 GDI+）
bool load_image(const std::string& path, Image& out, std::string& err);

// 保存图片（.bmp 走自写编码器，其它格式走 GDI+）
bool save_image(const std::string& path, const Image& img, std::string& err,
                int jpeg_quality = 92);

// 生成测试图案（彩色渐变 + 灰度阶梯 + 彩条 + 圆），用于无素材时验证程序
Image make_test_pattern(int w, int h);

} // namespace im
