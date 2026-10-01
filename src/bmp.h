// bmp.h -- 自写的 BMP 编解码（不依赖任何第三方库）
#pragma once

#include "image.h"

namespace im {

// 支持：1/4/8 位调色板、16 位(555/565/位域)、24 位、32 位，BI_RGB / BI_BITFIELDS，
// 自下而上与自上而下两种行序。
bool bmp_load(const std::string& path, Image& out, std::string& err);

// 写出：不透明时写 24 位，含透明通道时写 32 位（BI_RGB，自下而上）。
bool bmp_save(const std::string& path, const Image& img, std::string& err);

} // namespace im
