// gdiplus_io.h -- 借助 Windows 自带 GDI+ 读写 PNG / JPEG / GIF / TIFF（纯 C++ 调用系统组件，无第三方库）
#pragma once

#include <string>
#include <vector>

#include "image.h"

namespace im {

bool gdiplus_startup();
void gdiplus_shutdown();

bool gdiplus_load(const std::string& path, Image& out, std::string& err);
bool gdiplus_save(const std::string& path, const Image& img, std::string& err, int jpeg_quality);

// 系统可写出的图片格式后缀（如 ".png"）
std::vector<std::string> gdiplus_writable_exts();

} // namespace im
