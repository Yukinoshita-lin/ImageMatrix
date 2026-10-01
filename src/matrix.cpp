// matrix.cpp -- 图像 <-> TXT 数值矩阵（R/G/B/灰度）双向转换
//
// TXT 文件结构（注释头可以被读取端识别，用于原样导回）：
//   # ImageMatrix 1.0
//   # width  4
//   # height 3
//   # format CHANNELS      (RGB | RGBA | GRAY | CHANNELS | HEX)
//   # max 255
//   ...矩阵数据...
//
// 读取端非常宽容：支持逗号/空格/分号/制表符/括号分隔、0-255 或 0-1 归一化数值、
// 缺少宽高时自动推断（或由界面/命令行补一个宽度）。
#include "matrix.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <ostream>
#include <sstream>

#include "util.h"

namespace im {

const char* txt_format_name(TxtFormat f) {
    switch (f) {
        case TxtFormat::RGB: return "RGB";
        case TxtFormat::RGBA: return "RGBA";
        case TxtFormat::GRAY: return "GRAY";
        case TxtFormat::CHANNELS: return "CHANNELS";
        case TxtFormat::HEX: return "HEX";
    }
    return "RGB";
}

bool txt_format_from_name(const std::string& s, TxtFormat& out) {
    std::string u = to_lower(trim(s));
    if (u.empty()) return false;
    if (u == "rgb" || u == "rgb_triplet" || u == "triplet" || u == "pixel") { out = TxtFormat::RGB; return true; }
    if (u == "rgba") { out = TxtFormat::RGBA; return true; }
    if (u == "gray" || u == "grey" || u == "y" || u == "luma" || u == "graymatrix") { out = TxtFormat::GRAY; return true; }
    if (u == "channels" || u == "channel" || u == "matrix" || u == "planar" || u == "rgba_gray") { out = TxtFormat::CHANNELS; return true; }
    if (u == "hex" || u == "rgb_hex") { out = TxtFormat::HEX; return true; }
    return false;
}

std::vector<TxtFormat> all_txt_formats() {
    return {TxtFormat::CHANNELS, TxtFormat::RGB, TxtFormat::GRAY, TxtFormat::HEX, TxtFormat::RGBA};
}

// ---------------------------------------------------------------- 写出矩阵
namespace {

struct Writer {
    std::ostream& os;
    std::string buf;
    size_t lines = 0;
    size_t max_lines = 0;
    bool truncated = false;

    Writer(std::ostream& o, size_t ml) : os(o), max_lines(ml) { buf.reserve(1u << 16); }

    bool room() {
        if (max_lines == 0 || lines < max_lines) return true;
        truncated = true;
        return false;
    }
    void end_line() {
        buf.push_back('\n');
        ++lines;
        if (buf.size() >= (1u << 16)) flush();
    }
    void flush() {
        if (!buf.empty()) {
            os.write(buf.data(), (std::streamsize)buf.size());
            buf.clear();
        }
    }
};

void put_int(Writer& w, int v) {
    char b[16];
    int n = std::snprintf(b, sizeof(b), "%d", v);
    w.buf.append(b, (size_t)n);
}

void put_hex6(Writer& w, uint32_t c) {
    char b[16];
    int n = std::snprintf(b, sizeof(b), "%06X", (unsigned)(c & 0xFFFFFFu));
    w.buf.append(b, (size_t)n);
}

const char* header_comment(TxtFormat f) {
    switch (f) {
        case TxtFormat::RGB: return "# 每行一个像素：R, G, B（0-255）";
        case TxtFormat::RGBA: return "# 每行一个像素：R, G, B, A（0-255）";
        case TxtFormat::GRAY: return "# 灰度矩阵：每行对应图像的一行，数值为灰度值 Y（0-255）";
        case TxtFormat::CHANNELS: return "# 分通道矩阵：R 矩阵 -> G 矩阵 -> B 矩阵 -> GRAY 矩阵，每个矩阵 h 行 w 列";
        case TxtFormat::HEX: return "# 每行一个像素：16 进制 RRGGBB";
    }
    return "";
}

void write_matrix(std::ostream& os, const Image& img, const ExportOptions& opt, size_t max_lines) {
    Writer w(os, max_lines);
    const int W = img.w, H = img.h;

    if (opt.include_header) {
        w.buf += "# ImageMatrix 1.0 - 图像数值矩阵 (image -> matrix)\n";
        w.buf += "# width " + std::to_string(W) + "\n";
        w.buf += "# height " + std::to_string(H) + "\n";
        w.buf += std::string("# format ") + txt_format_name(opt.fmt) + "\n";
        w.buf += "# max 255\n";
        w.buf += "# gray = round(0.299*R + 0.587*G + 0.114*B)\n";
        w.buf += std::string(header_comment(opt.fmt)) + "\n";
        w.buf += "\n";
        w.lines += 8;
    }

    switch (opt.fmt) {
        case TxtFormat::RGB:
        case TxtFormat::RGBA: {
            const bool with_a = (opt.fmt == TxtFormat::RGBA);
            for (int y = 0; y < H && !w.truncated; ++y) {
                for (int x = 0; x < W; ++x) {
                    if (!w.room()) break;
                    uint32_t c = img.at(x, y);
                    put_int(w, (int)((c >> 16) & 0xFFu));
                    w.buf += ", ";
                    put_int(w, (int)((c >> 8) & 0xFFu));
                    w.buf += ", ";
                    put_int(w, (int)(c & 0xFFu));
                    if (with_a) {
                        w.buf += ", ";
                        put_int(w, (int)((c >> 24) & 0xFFu));
                    }
                    w.end_line();
                }
            }
            break;
        }
        case TxtFormat::HEX: {
            for (int y = 0; y < H && !w.truncated; ++y) {
                for (int x = 0; x < W; ++x) {
                    if (!w.room()) break;
                    put_hex6(w, img.at(x, y));
                    w.end_line();
                }
            }
            break;
        }
        case TxtFormat::GRAY: {
            for (int y = 0; y < H; ++y) {
                if (!w.room()) break;
                for (int x = 0; x < W; ++x) {
                    put_int(w, img.gray(x, y));
                    if (x + 1 < W) w.buf += ' ';
                }
                w.end_line();
            }
            break;
        }
        case TxtFormat::CHANNELS: {
            struct Ch {
                const char* name;
                const char* note;
            };
            const Ch chans[4] = {{"R", "红色通道"}, {"G", "绿色通道"}, {"B", "蓝色通道"}, {"GRAY", "灰度值 Y"}};
            for (int ci = 0; ci < 4; ++ci) {
                if (!w.room()) break;
                w.buf += "\n# channel ";
                w.buf += chans[ci].name;
                w.buf += "  (";
                w.buf += chans[ci].note;
                w.buf += ")\n";
                ++w.lines;
                for (int y = 0; y < H; ++y) {
                    if (!w.room()) break;
                    for (int x = 0; x < W; ++x) {
                        int v;
                        uint32_t c = img.at(x, y);
                        switch (ci) {
                            case 0: v = (int)((c >> 16) & 0xFFu); break;
                            case 1: v = (int)((c >> 8) & 0xFFu); break;
                            case 2: v = (int)(c & 0xFFu); break;
                            default: v = img.gray(x, y); break;
                        }
                        put_int(w, v);
                        if (x + 1 < W) w.buf += ' ';
                    }
                    w.end_line();
                }
            }
            break;
        }
    }

    if (w.truncated) {
        w.buf += "\n# ... (预览已截断，完整内容请导出 TXT 文件)\n";
        w.lines += 2;
        w.flush();
    }
    w.flush();
}

} // namespace

bool export_matrix_txt(const std::string& path, const Image& img, const ExportOptions& opt,
                       std::string& err) {
    if (img.empty()) { err = "没有图像数据，无法导出"; return false; }
    std::ofstream f(std::filesystem::path(utf8_to_wide(path)));  // 文本模式：Windows 下写出 CRLF
    if (!f) { err = "无法创建 TXT 文件（目录不存在或无写入权限）"; return false; }
    write_matrix(f, img, opt, 0);
    f.flush();
    if (!f) { err = "写入 TXT 失败（磁盘空间不足？）"; return false; }
    return true;
}

std::string build_matrix_text(const Image& img, const ExportOptions& opt, size_t max_lines) {
    std::ostringstream os;
    if (!img.empty()) write_matrix(os, img, opt, max_lines);
    return os.str();
}

// ---------------------------------------------------------------- 读取矩阵
namespace {

void tokenize(const std::string& line, std::vector<std::string>& out) {
    out.clear();
    std::string cur;
    for (size_t i = 0; i < line.size(); ++i) {
        char ch = line[i];
        const bool sep = (ch == ',' || ch == ';' || ch == '|' || ch == '(' || ch == ')' ||
                          ch == '[' || ch == ']' || ch == '{' || ch == '}' || ch == '=' ||
                          ch == '\t' || ch == ' ');
        if (sep) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) out.push_back(cur);
}

bool parse_double(const std::string& tok, double& v) {
    if (tok.empty()) return false;
    const char* s = tok.c_str();
    char* end = nullptr;
    double d = std::strtod(s, &end);
    if (end == s) return false;
    while (*end == ':' || *end == '\t' || *end == ' ') ++end;
    if (*end != '\0') return false;
    v = d;
    return true;
}

// 返回 6 / 8 位十六进制颜色长度，否则 0；无 # / 0x 前缀时要求至少一个字母，避免误判十进制数
int hex_color_len(const std::string& tok) {
    std::string t = tok;
    bool prefixed = false;
    if (!t.empty() && t[0] == '#') { t.erase(0, 1); prefixed = true; }
    else if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) { t.erase(0, 2); prefixed = true; }
    if (t.size() != 6 && t.size() != 8) return 0;
    int letters = 0;
    for (char c : t) {
        if (!std::isxdigit((unsigned char)c)) return 0;
        if (std::isalpha((unsigned char)c)) ++letters;
    }
    if (!prefixed && letters == 0) return 0;
    return (int)t.size();
}

uint32_t to_hex_color(const std::string& tok) {
    std::string t = tok;
    if (!t.empty() && t[0] == '#') t.erase(0, 1);
    else if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t.erase(0, 2);
    unsigned long v = std::strtoul(t.c_str(), nullptr, 16);
    if (t.size() <= 6) return 0xFF000000u | (uint32_t)(v & 0xFFFFFFu);  // RRGGBB
    return (uint32_t)v;                                                // AARRGGBB
}

std::string norm_channel(const std::string& s) {
    std::string k = to_lower(trim(s));
    if (k == "r" || k == "red") return "R";
    if (k == "g" || k == "green") return "G";
    if (k == "b" || k == "blue") return "B";
    if (k == "a" || k == "alpha") return "A";
    if (k == "gray" || k == "grey" || k == "y" || k == "luma" || k == "luminance") return "GRAY";
    return std::string();
}

struct DLine {
    std::vector<double> vals;
    std::vector<uint32_t> px;
    std::string channel;
};

} // namespace

bool import_matrix_txt(const std::string& path, int forced_width, Image& out, ImportInfo& info,
                       std::string& err) {
    out = Image();
    info = ImportInfo();

    // ---- 读入整个文件 ----
    std::ifstream f(std::filesystem::path(utf8_to_wide(path)), std::ios::binary);
    if (!f) { err = "无法打开 TXT 文件（不存在或无读取权限）"; return false; }
    f.seekg(0, std::ios::end);
    std::streamoff sz = f.tellg();
    if (sz <= 0) { err = "TXT 文件为空"; return false; }
    if (sz > (std::streamoff)(1024.0 * 1024 * 1024)) { err = "TXT 文件超过 1GB，已拒绝读取"; return false; }
    f.seekg(0, std::ios::beg);
    std::string text((size_t)sz, '\0');
    f.read(&text[0], sz);
    if (!f) { err = "读取 TXT 文件失败"; return false; }
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        text.erase(0, 3);  // 去掉 UTF-8 BOM

    // ---- 逐行解析 ----
    int hdr_w = 0, hdr_h = 0;
    double hdr_max = 0.0;
    std::string hdr_fmt;
    bool have_fmt = false;
    TxtFormat declared{};
    std::vector<DLine> dl;
    std::string cur_channel;
    std::map<std::string, std::vector<double>> ch_vals;
    std::map<std::string, std::vector<int>> ch_toks;

    // 切成行（同时处理 CRLF）
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : text) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else if (c != '\r') cur.push_back(c);
        }
        if (!cur.empty()) lines.push_back(cur);
    }
    text.clear();

    // ---- 第一遍：先读注释头。有了声明格式，第二遍才能正确解析形如 "000000"
    //      （6 位纯数字）的 16 进制像素，不会把它当成十进制数。 ----
    for (const std::string& raw0 : lines) {
        std::string line = trim(raw0);
        if (line.empty() || line[0] != '#') continue;
        if (hex_color_len(line) > 0) continue;  // 形如 "#RRGGBB" 的像素行
        std::string rest = trim(line.substr(1));
        std::string key, val;
        size_t sp = rest.find_first_of(" \t:=");
        if (sp == std::string::npos) {
            key = to_lower(rest);
        } else {
            key = to_lower(trim(rest.substr(0, sp)));
            val = trim(rest.substr(sp + 1));
            if (!val.empty() && (val[0] == ':' || val[0] == '=')) val = trim(val.substr(1));
        }
        if (key == "width" || key == "w" || key == "cols" || key == "columns" || key == "列") {
            hdr_w = std::atoi(val.c_str());
        } else if (key == "height" || key == "h" || key == "rows" || key == "行") {
            hdr_h = std::atoi(val.c_str());
        } else if (key == "format" || key == "type" || key == "mode") {
            hdr_fmt = val;
        } else if (key == "max" || key == "maxval" || key == "scale") {
            hdr_max = std::atof(val.c_str());
        }
    }
    if (!hdr_fmt.empty()) have_fmt = txt_format_from_name(hdr_fmt, declared);
    const bool declared_hex = have_fmt && declared == TxtFormat::HEX;

    // ---- 第二遍：解析数据行与通道标记 ----
    auto register_line = [&](DLine& d) {
        if (!d.channel.empty() && !d.vals.empty()) {
            if (ch_toks.find(d.channel) == ch_toks.end()) {
                ch_toks[d.channel] = {};
                ch_vals[d.channel] = {};
            }
            ch_toks[d.channel].push_back((int)d.vals.size());
            ch_vals[d.channel].insert(ch_vals[d.channel].end(), d.vals.begin(), d.vals.end());
        }
        dl.push_back(d);
    };

    for (const std::string& raw0 : lines) {
        std::string line = trim(raw0);
        if (line.empty()) continue;

        if (line[0] == '#') {
            // "#RRGGBB" 形式的十六进制像素
            if (hex_color_len(line) > 0) {
                DLine d;
                d.channel = cur_channel;
                d.px.push_back(to_hex_color(line));
                dl.push_back(d);
                continue;
            }
            std::string rest = trim(line.substr(1));
            std::string key, val;
            size_t sp = rest.find_first_of(" \t:=");
            if (sp != std::string::npos) {
                key = to_lower(trim(rest.substr(0, sp)));
                val = trim(rest.substr(sp + 1));
                if (!val.empty() && (val[0] == ':' || val[0] == '=')) val = trim(val.substr(1));
            } else {
                key = to_lower(rest);
            }
            if (key == "channel" || key == "ch") {
                // 只取第一个词：形如 "# channel R  (红色通道)" 也能识别
                size_t sp2 = val.find_first_of(" \t(（,;");
                std::string first = (sp2 == std::string::npos) ? val : val.substr(0, sp2);
                std::string nm = norm_channel(first);
                if (!nm.empty()) cur_channel = nm;
            }
            continue;
        }

        // ---- 数据行 ----
        std::vector<std::string> toks;
        tokenize(line, toks);
        if (toks.empty()) continue;

        DLine d;
        d.channel = cur_channel;
        bool all_hex = !toks.empty();
        if (!declared_hex) {
            for (const auto& t : toks) {
                if (hex_color_len(t) == 0) { all_hex = false; break; }
            }
        }
        if (all_hex) {
            for (const auto& t : toks) d.px.push_back(to_hex_color(t));
        } else {
            for (const auto& t : toks) {
                double v;
                if (parse_double(t, v)) d.vals.push_back(v);
            }
            if (d.vals.empty()) continue;
        }
        register_line(d);
    }

    // ---- 统计 ----
    std::vector<double> vals;   // 全部数值（按出现顺序）
    std::vector<uint32_t> hpx;  // 全部 16 进制像素
    size_t n_lines = 0;
    int toks_first = -1;
    bool uniform = true;
    for (const auto& d : dl) {
        int c = d.px.empty() ? (int)d.vals.size() : (int)d.px.size();
        if (c <= 0) continue;
        if (toks_first < 0) toks_first = c;
        else if (c != toks_first) uniform = false;
        ++n_lines;
        if (!d.px.empty()) hpx.insert(hpx.end(), d.px.begin(), d.px.end());
        else vals.insert(vals.end(), d.vals.begin(), d.vals.end());
    }
    if (vals.empty() && hpx.empty()) {
        err = "TXT 中找不到可用的数值数据（是否选错了文件？）";
        return false;
    }

    // ---- 确定格式 ----
    TxtFormat fmt = TxtFormat::GRAY;
    if (have_fmt) {
        fmt = declared;
    } else if (!hpx.empty() && vals.empty()) {
        fmt = TxtFormat::HEX;
    } else if (!ch_vals.empty()) {
        fmt = TxtFormat::CHANNELS;
    } else if (uniform && toks_first == 4) {
        fmt = TxtFormat::RGBA;   // 每行一个像素（4 个值）
    } else if (uniform && toks_first == 3) {
        fmt = TxtFormat::RGB;    // 每行一个像素（3 个值）
    } else {
        fmt = TxtFormat::GRAY;   // 每行 w 个值的矩阵
    }

    // ---- 确定宽高 ----
    int W = hdr_w > 0 ? hdr_w : 0;
    int H = hdr_h > 0 ? hdr_h : 0;
    if (W <= 0 && forced_width > 0) W = forced_width;

    auto channel_grid = [&](const std::string& name, int& cw, int& chh) -> bool {
        auto it = ch_toks.find(name);
        if (it == ch_toks.end() || it->second.empty()) return false;
        int first = it->second[0];
        if (first <= 0) return false;
        for (int c : it->second)
            if (c != first) return false;
        cw = first;
        chh = (int)it->second.size();
        return true;
    };

    // 无文件头时：每行恰好 W 个值、共 n 行 —— 这就是一个宽 W 的灰度矩阵
    //（用来消除"每行 3/4 个值"到底是 RGB 像素还是窄灰度矩阵的歧义）
    if (!have_fmt && (fmt == TxtFormat::RGB || fmt == TxtFormat::RGBA) && W > 0 && uniform &&
        toks_first == W && n_lines > 1) {
        fmt = TxtFormat::GRAY;
    }

    if (W <= 0) {
        int cw = 0, chh = 0;
        if (fmt == TxtFormat::CHANNELS && channel_grid("R", cw, chh)) {
            W = cw;
            if (H <= 0) H = chh;
        } else if (fmt == TxtFormat::GRAY && uniform && toks_first > 0 && n_lines > 0 && H <= 0) {
            W = toks_first;
            H = (int)n_lines;
        } else {
            info.needs_width = true;
            info.format_name = txt_format_name(fmt);
            info.values = vals.size() + hpx.size();
            err = "TXT 中缺少宽度信息，请指定图片宽度";
            return false;
        }
    }

    const int mult = (fmt == TxtFormat::RGB) ? 3 : (fmt == TxtFormat::RGBA ? 4 : 1);
    unsigned long long total = (fmt == TxtFormat::HEX) ? (unsigned long long)hpx.size()
                                                      : (unsigned long long)vals.size();
    unsigned long long pixels = total / (unsigned long long)mult;

    std::string note;
    if (H <= 0) {
        if (fmt == TxtFormat::GRAY && uniform && toks_first == W && n_lines > 0) {
            H = (int)n_lines;
        } else {
            H = (int)((pixels + (unsigned long long)W - 1) / (unsigned long long)W);
            if (H <= 0) H = 1;
            note += "缺少高度信息，按宽度 " + std::to_string(W) + " 推算为 " + std::to_string(H) + " 行";
        }
    }
    if (!have_fmt && fmt == TxtFormat::GRAY && uniform && toks_first == W) {
        note += (note.empty() ? "" : "；") + std::string("按灰度矩阵解析（每行 ") + std::to_string(W) + " 个值）";
    }
    if (W <= 0 || H <= 0) { err = "推算出的图片尺寸非法"; return false; }
    if ((unsigned long long)W * H > 400000000ull) {
        err = "推算出的图片过大（超过 4 亿像素），请检查宽度设置";
        return false;
    }

    // 数值归一化：max=1 或存在小数时视为 0-1 归一化数据
    double src_max = 255.0;
    if (hdr_max > 0.0) {
        src_max = hdr_max;
    } else {
        for (double v : vals) {
            if (v != std::floor(v)) { src_max = 1.0; break; }
        }
    }
    if (src_max <= 0.0) src_max = 255.0;
    const bool identity = (src_max == 255.0);
    auto scale_val = [&](double v) -> int {
        double s = identity ? v : v * 255.0 / src_max;
        long r = std::lround(s);
        return Image::clamp255((int)r);
    };

    // ---- 生成图像 ----
    const unsigned long long need = (unsigned long long)W * (unsigned long long)H;
    out.reset(W, H);

    auto fill_from = [&](const std::vector<double>& src, int mult_used) {
        unsigned long long n = (unsigned long long)src.size() / (unsigned long long)mult_used;
        if (n > need) { n = need; info.truncated = true; }
        if (n < need) note += (note.empty() ? "" : "；") + std::string("数据不足，缺失像素补 0");
        for (unsigned long long i = 0; i < need; ++i) {
            int r = 0, g = 0, b = 0, a = 255;
            if (i < n) {
                const double* p = &src[(size_t)i * mult_used];
                if (mult_used == 1) { r = g = b = scale_val(p[0]); }
                else if (mult_used == 3) { r = scale_val(p[0]); g = scale_val(p[1]); b = scale_val(p[2]); }
                else { r = scale_val(p[0]); g = scale_val(p[1]); b = scale_val(p[2]); a = scale_val(p[3]); }
            }
            out.px[(size_t)i] = Image::pack(r, g, b, a);
        }
    };

    if (fmt == TxtFormat::CHANNELS) {
        auto getch = [&](const std::string& nm) -> const std::vector<double>* {
            auto it = ch_vals.find(nm);
            return (it == ch_vals.end() || it->second.empty()) ? nullptr : &it->second;
        };
        const std::vector<double>* cr = getch("R");
        const std::vector<double>* cg = getch("G");
        const std::vector<double>* cb = getch("B");
        if (!cr) {
            err = "CHANNELS 格式缺少 R 通道矩阵（请检查 \"# channel R\" 标记）";
            return false;
        }
        if (!cg || !cb) note += (note.empty() ? "" : "；") + std::string("缺少 G 或 B 通道，缺失通道补 0");
        total = cr->size();
        if (total > need) { total = need; info.truncated = true; }
        if (total < need) note += (note.empty() ? "" : "；") + std::string("数据不足，缺失像素补 0");
        for (unsigned long long i = 0; i < need; ++i) {
            int r = 0, g = 0, b = 0;
            if (i < cr->size()) r = scale_val((*cr)[(size_t)i]);
            if (cg && i < cg->size()) g = scale_val((*cg)[(size_t)i]);
            if (cb && i < cb->size()) b = scale_val((*cb)[(size_t)i]);
            out.px[(size_t)i] = Image::pack(r, g, b, 255);
        }
    } else if (fmt == TxtFormat::HEX) {
        unsigned long long n = hpx.size();
        if (n > need) { n = need; info.truncated = true; }
        if (n < need) note += (note.empty() ? "" : "；") + std::string("数据不足，缺失像素补 0");
        for (unsigned long long i = 0; i < need; ++i)
            out.px[(size_t)i] = (i < n) ? hpx[(size_t)i] : 0xFF000000u;
    } else {
        fill_from(vals, mult);
    }

    info.width = W;
    info.height = H;
    info.format_name = txt_format_name(fmt);
    info.values = (fmt == TxtFormat::CHANNELS) ? total : (vals.empty() ? hpx.size() : vals.size());
    if (!have_fmt) note += (note.empty() ? "" : "；") + std::string("TXT 未声明格式，已自动识别为 ") + info.format_name;
    info.note = note;
    return true;
}

} // namespace im
