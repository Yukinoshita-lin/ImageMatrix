// matrix.h -- 图像 <-> TXT 数值矩阵 的双向转换
#pragma once

#include <string>
#include <vector>

#include "image.h"

namespace im {

// TXT 中矩阵的排布方式
enum class TxtFormat {
    RGB = 0,   // 每行一个像素：R, G, B
    RGBA,      // 每行一个像素：R, G, B, A
    GRAY,      // 灰度矩阵：每行对应图像一行
    CHANNELS,  // 分通道矩阵：R 矩阵 + G 矩阵 + B 矩阵 + GRAY 矩阵
    HEX,       // 每行一个像素：16 进制 RRGGBB
};

const char* txt_format_name(TxtFormat f);              // "RGB" / "GRAY" / ...
bool txt_format_from_name(const std::string& s, TxtFormat& out);
std::vector<TxtFormat> all_txt_formats();

struct ExportOptions {
    TxtFormat fmt = TxtFormat::CHANNELS;
    bool include_header = true;   // 写入 # width / # height / # format 等注释头（便于原样导回）
};

// 导出：把图片写成 TXT 矩阵
bool export_matrix_txt(const std::string& path, const Image& img, const ExportOptions& opt,
                       std::string& err);

// 生成矩阵文本（用于界面预览 / 内存自检）；max_lines>0 时截断
std::string build_matrix_text(const Image& img, const ExportOptions& opt, size_t max_lines = 0);

struct ImportInfo {
    int width = 0;
    int height = 0;
    std::string format_name;     // 实际识别到的格式
    bool needs_width = false;    // TXT 中缺少宽高，需要调用者提供宽度
    bool truncated = false;      // 数据量超出宽*高，已截断
    std::string note;            // 附加说明
    unsigned long long values = 0;
};

// 导入：读取 TXT 矩阵还原成图片。
// forced_width > 0 时用于补齐 TXT 中缺失的宽度信息。
bool import_matrix_txt(const std::string& path, int forced_width, Image& out, ImportInfo& info,
                       std::string& err);

} // namespace im
