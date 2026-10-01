// util.h -- 通用小工具（UTF-8 / UTF-16 转换、字符串处理、路径处理）
// ImageMatrix : 图片 <-> 数值矩阵(TXT) 转换工具
// 全部由 C/C++ 编写，GUI 使用 Win32 API，图片编解码使用自写 BMP 编解码 + Windows 自带 GDI+。
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <shellapi.h>  // CommandLineToArgvW

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace im {

// ---------- 编码转换 ----------
inline std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

inline std::string wide_to_utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s((size_t)n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// 从当前进程命令行取得参数（宽字符 -> UTF-8），保证中文路径不乱码
inline std::vector<std::string> get_args_utf8() {
    std::vector<std::string> out;
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv) return out;
    for (int i = 0; i < argc; ++i) out.push_back(wide_to_utf8(argv[i]));
    ::LocalFree(argv);
    return out;
}

// ---------- 字符串 ----------
inline std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return s.substr(a, b - a);
}

inline std::wstring trim_w(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && s[a] <= L' ') ++a;
    while (b > a && s[b - 1] <= L' ') --b;
    return s.substr(a, b - a);
}

inline bool iends_with(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return to_lower(s).compare(s.size() - suffix.size(), suffix.size(), to_lower(suffix)) == 0;
}

inline bool iequals(const std::string& a, const std::string& b) {
    return to_lower(a) == to_lower(b);
}

// ---------- 路径 ----------
inline std::string file_name_of(const std::string& path) {
    size_t p = path.find_last_of("\\/");
    return (p == std::string::npos) ? path : path.substr(p + 1);
}

inline std::string dir_name_of(const std::string& path) {
    size_t p = path.find_last_of("\\/");
    return (p == std::string::npos) ? std::string() : path.substr(0, p);
}

inline std::string strip_ext(const std::string& path) {
    size_t p = path.find_last_of('.');
    size_t s = path.find_last_of("\\/");
    if (p == std::string::npos || (s != std::string::npos && p < s)) return path;
    return path.substr(0, p);
}

inline std::string ext_of(const std::string& path) {
    size_t p = path.find_last_of('.');
    size_t s = path.find_last_of("\\/");
    if (p == std::string::npos || (s != std::string::npos && p < s)) return std::string();
    return path.substr(p);
}

// 人类可读的字节数
inline std::string human_bytes(unsigned long long n) {
    char buf[64];
    const char* u[] = {"B", "KB", "MB", "GB", "TB"};
    double v = (double)n;
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
    if (i == 0) std::snprintf(buf, sizeof(buf), "%llu %s", n, u[i]);
    else        std::snprintf(buf, sizeof(buf), "%.1f %s", v, u[i]);
    return buf;
}

// 千分位分隔，便于阅读大数字
inline std::string with_thousands(unsigned long long n) {
    std::string s = std::to_string(n);
    std::string o;
    int cnt = 0;
    for (size_t i = s.size(); i-- > 0;) {
        o.push_back(s[i]);
        if (++cnt % 3 == 0 && i != 0) o.push_back(',');
    }
    std::reverse(o.begin(), o.end());
    return o;
}

} // namespace im
