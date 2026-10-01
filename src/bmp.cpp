// bmp.cpp -- 自写 BMP 编解码器（纯 C++，不依赖任何第三方库）
#include "bmp.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "util.h"

namespace im {
namespace {

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
int32_t rd32s(const uint8_t* p) { return (int32_t)rd32(p); }

void push16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((uint8_t)(x & 0xFF));
    v.push_back((uint8_t)((x >> 8) & 0xFF));
}
void push32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x & 0xFF));
    v.push_back((uint8_t)((x >> 8) & 0xFF));
    v.push_back((uint8_t)((x >> 16) & 0xFF));
    v.push_back((uint8_t)((x >> 24) & 0xFF));
}

// 按位域掩码取值并线性拉伸到 0..255
uint32_t extract_masked(uint32_t v, uint32_t mask) {
    if (mask == 0) return 0;
    int shift = 0;
    while (((mask >> shift) & 1u) == 0u && shift < 32) ++shift;
    uint32_t m = mask >> shift;
    if (m == 0) return 0;
    uint32_t val = (v & mask) >> shift;
    return (val * 255u + m / 2u) / m;
}

bool read_all(const std::string& path, std::vector<uint8_t>& buf, std::string& err) {
    std::ifstream f(std::filesystem::path(utf8_to_wide(path)), std::ios::binary);
    if (!f) { err = "无法打开文件（可能不存在或无读取权限）"; return false; }
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    if (n <= 0) { err = "文件为空"; return false; }
    f.seekg(0, std::ios::beg);
    buf.resize((size_t)n);
    f.read((char*)buf.data(), n);
    if (!f) { err = "读取文件失败"; return false; }
    return true;
}

} // namespace

bool bmp_load(const std::string& path, Image& out, std::string& err) {
    std::vector<uint8_t> buf;
    if (!read_all(path, buf, err)) return false;

    if (buf.size() < 54) { err = "文件过小，不是有效 BMP"; return false; }
    if (buf[0] != 'B' || buf[1] != 'M') { err = "缺少 BMP 文件头标识 'BM'"; return false; }

    const uint32_t off_bits = rd32(&buf[10]);
    const uint32_t hdr_size = rd32(&buf[14]);
    if (hdr_size < 40) { err = "不支持的 BMP 头（BITMAPCOREHEADER）"; return false; }
    if (14u + hdr_size > buf.size()) { err = "BMP 头长度越界"; return false; }

    const int32_t W32 = rd32s(&buf[18]);
    const int32_t H32 = rd32s(&buf[22]);
    const uint16_t planes = rd16(&buf[26]);
    const uint16_t bpp = rd16(&buf[28]);
    const uint32_t comp = rd32(&buf[30]);
    const uint32_t clr_used = rd32(&buf[46]);

    if (planes != 1) { err = "BMP planes != 1"; return false; }
    if (W32 <= 0 || H32 == 0) { err = "BMP 尺寸非法"; return false; }
    const bool top_down = (H32 < 0);
    const int W = (int)W32;
    const int H = top_down ? (int)(-H32) : (int)H32;
    if ((int64_t)W * H > 400000000LL) { err = "BMP 尺寸过大"; return false; }

    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) {
        err = "不支持的 BMP 位深";
        return false;
    }

    // 压缩方式：仅支持 BI_RGB(0) 与 BI_BITFIELDS(3)/BI_ALPHABITFIELDS(6)
    const bool bitfields = (bpp == 16 || bpp == 32) && (comp == 3 || comp == 6);
    if (comp != 0 && !bitfields) {
        err = "不支持的 BMP 压缩方式（RLE 等），请改用 GDI+ 通路";
        return false;
    }

    uint32_t mask_r = 0, mask_g = 0, mask_b = 0, mask_a = 0;
    if (bitfields) {
        const uint8_t* p = &buf[14 + 40];  // BI_BITFIELDS 时掩码紧跟在 40 字节头之后
        if (14 + 40 + 12 > buf.size()) { err = "BMP 位域掩码越界"; return false; }
        mask_r = rd32(p);
        mask_g = rd32(p + 4);
        mask_b = rd32(p + 8);
        if (comp == 6) {
            if (14 + 40 + 16 > buf.size()) { err = "BMP alpha 掩码越界"; return false; }
            mask_a = rd32(p + 12);
        }
    } else if (bpp == 16) {
        mask_r = 0x7C00; mask_g = 0x03E0; mask_b = 0x001F;  // 默认 X1R5G5B5
    } else if (bpp == 32) {
        mask_r = 0x00FF0000; mask_g = 0x0000FF00; mask_b = 0x000000FF;
    }

    // 调色板
    std::vector<uint32_t> palette;
    if (bpp <= 8) {
        uint32_t entries = clr_used ? clr_used : (1u << bpp);
        if (entries > 256) entries = 256;
        size_t pal_off = 14 + hdr_size;
        if (pal_off + (size_t)entries * 4 > buf.size()) { err = "BMP 调色板越界"; return false; }
        palette.resize(entries);
        for (uint32_t i = 0; i < entries; ++i) {
            const uint8_t* p = &buf[pal_off + (size_t)i * 4];
            palette[i] = Image::pack(p[2], p[1], p[0], 255);
        }
    }

    const int64_t stride64 = (((int64_t)bpp * W + 31) / 32) * 4;
    if (stride64 <= 0) { err = "BMP 行宽非法"; return false; }
    const int64_t need = (int64_t)off_bits + stride64 * H;
    if (off_bits > buf.size() || need > (int64_t)buf.size()) {
        err = "BMP 像素数据不完整";
        return false;
    }

    out.reset(W, H);
    const uint8_t* base = buf.data() + off_bits;
    bool alpha_all_zero = (bpp == 32);

    for (int y = 0; y < H; ++y) {
        int src_row = top_down ? y : (H - 1 - y);
        const uint8_t* row = base + (int64_t)src_row * stride64;
        uint32_t* dst = out.row(y);
        for (int x = 0; x < W; ++x) {
            uint32_t c = 0xFF000000u;
            switch (bpp) {
                case 1: {
                    uint8_t byte = row[x >> 3];
                    int bit = 7 - (x & 7);
                    uint32_t idx = (byte >> bit) & 1u;
                    c = (idx < palette.size()) ? palette[idx] : 0xFF000000u;
                    break;
                }
                case 4: {
                    uint8_t byte = row[x >> 1];
                    uint32_t idx = (x & 1) ? (byte & 0x0Fu) : (byte >> 4);
                    c = (idx < palette.size()) ? palette[idx] : 0xFF000000u;
                    break;
                }
                case 8: {
                    uint32_t idx = row[x];
                    c = (idx < palette.size()) ? palette[idx] : 0xFF000000u;
                    break;
                }
                case 16: {
                    uint32_t v = rd16(row + (size_t)x * 2);
                    c = Image::pack((int)extract_masked(v, mask_r), (int)extract_masked(v, mask_g),
                                    (int)extract_masked(v, mask_b), 255);
                    break;
                }
                case 24: {
                    const uint8_t* p = row + (size_t)x * 3;
                    c = Image::pack(p[2], p[1], p[0], 255);
                    break;
                }
                case 32: {
                    uint32_t v = rd32(row + (size_t)x * 4);
                    int a = mask_a ? (int)extract_masked(v, mask_a) : (int)((v >> 24) & 0xFFu);
                    int r = (int)extract_masked(v, mask_r ? mask_r : 0x00FF0000u);
                    int g = (int)extract_masked(v, mask_g ? mask_g : 0x0000FF00u);
                    int b = (int)extract_masked(v, mask_b ? mask_b : 0x000000FFu);
                    if (a != 0) alpha_all_zero = false;
                    c = Image::pack(r, g, b, a);
                    break;
                }
                default: break;
            }
            dst[x] = c;
        }
    }

    // 32 位 BI_RGB 的 alpha 字节常常全 0（表示"未使用"），此时按不透明处理
    if (bpp == 32 && comp == 0 && alpha_all_zero) {
        for (auto& v : out.px) v |= 0xFF000000u;
    }
    return true;
}

bool bmp_save(const std::string& path, const Image& img, std::string& err) {
    if (img.empty()) { err = "空图像"; return false; }

    const bool with_alpha = img.has_alpha();
    const uint16_t bpp = with_alpha ? 32 : 24;
    const int64_t stride = (((int64_t)bpp * img.w + 31) / 32) * 4;
    const uint32_t data_size = (uint32_t)(stride * img.h);
    const uint32_t off_bits = 14 + 40;

    std::vector<uint8_t> hdr;
    hdr.reserve(54);
    hdr.push_back('B');
    hdr.push_back('M');
    push32(hdr, off_bits + data_size);  // bfSize
    push16(hdr, 0);
    push16(hdr, 0);
    push32(hdr, off_bits);
    push32(hdr, 40);                    // biSize
    push32(hdr, (uint32_t)img.w);
    push32(hdr, (uint32_t)img.h);       // 正值 = 自下而上
    push16(hdr, 1);
    push16(hdr, bpp);
    push32(hdr, 0);                     // BI_RGB
    push32(hdr, data_size);
    push32(hdr, 2835);                  // 72 DPI
    push32(hdr, 2835);
    push32(hdr, 0);
    push32(hdr, 0);

    std::ofstream f(std::filesystem::path(utf8_to_wide(path)), std::ios::binary);
    if (!f) { err = "无法创建文件（路径不存在或无写入权限）"; return false; }
    f.write((const char*)hdr.data(), (std::streamsize)hdr.size());

    std::vector<uint8_t> row((size_t)stride, 0);
    for (int y = img.h - 1; y >= 0; --y) {
        const uint32_t* src = img.row(y);
        if (bpp == 24) {
            for (int x = 0; x < img.w; ++x) {
                row[(size_t)x * 3 + 0] = (uint8_t)(src[x] & 0xFFu);
                row[(size_t)x * 3 + 1] = (uint8_t)((src[x] >> 8) & 0xFFu);
                row[(size_t)x * 3 + 2] = (uint8_t)((src[x] >> 16) & 0xFFu);
            }
        } else {
            for (int x = 0; x < img.w; ++x) {
                uint32_t v = src[x];
                row[(size_t)x * 4 + 0] = (uint8_t)(v & 0xFFu);
                row[(size_t)x * 4 + 1] = (uint8_t)((v >> 8) & 0xFFu);
                row[(size_t)x * 4 + 2] = (uint8_t)((v >> 16) & 0xFFu);
                row[(size_t)x * 4 + 3] = (uint8_t)((v >> 24) & 0xFFu);
            }
        }
        f.write((const char*)row.data(), (std::streamsize)row.size());
    }
    f.flush();
    if (!f) { err = "写入文件失败（磁盘空间不足？）"; return false; }
    return true;
}

} // namespace im
