// main.cpp -- 程序入口：GUI 模式与命令行模式（命令行用于批处理与自检）
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gui.h"
#include "image.h"
#include "matrix.h"
#include "util.h"

using namespace im;

namespace {

struct Args {
    std::vector<std::string> a;

    bool has(const std::string& k) const {
        for (const auto& s : a)
            if (iequals(s, k)) return true;
        return false;
    }
    std::string get(const std::string& k, const std::string& def = std::string()) const {
        for (size_t i = 0; i + 1 < a.size(); ++i)
            if (iequals(a[i], k)) return a[i + 1];
        return def;
    }
    int geti(const std::string& k, int def) const {
        std::string v = get(k);
        if (v.empty()) return def;
        return std::atoi(v.c_str());
    }
};

void banner() {
    std::printf("ImageMatrix 1.0  -  image <-> numeric matrix (R/G/B/gray) converter\n");
}

void usage() {
    banner();
    std::printf(
        "\nusage:\n"
        "  imagematrix-cli to-txt   -i <image> -o <out.txt> [-f CHANNELS|RGB|GRAY|HEX|RGBA] [--no-header]\n"
        "  imagematrix-cli to-image -i <in.txt> -o <out.png> [--width N] [-q 92]\n"
        "  imagematrix-cli info     -i <image|txt>\n"
        "  imagematrix-cli gen      -o <out.png|out.bmp> [-w 256] [-h 192]\n"
        "  imagematrix-cli selftest [-d <workdir>]\n"
        "  imagematrix-cli gui\n"
        "\nnotes:\n"
        "  * readable image formats: bmp (built-in codec), png/jpg/gif/tif (Windows GDI+)\n"
        "  * writable image formats: png/jpg/bmp/gif/tif\n"
        "  * run imagematrix.exe without arguments to open the GUI\n");
}

int g_fail = 0;
void check(bool ok, const std::string& what, const std::string& detail = std::string()) {
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(),
                detail.empty() ? "" : "  -- ", detail.c_str());
    if (!ok) ++g_fail;
}

bool same_pixels(const Image& a, const Image& b, std::string& why) {
    if (a.w != b.w || a.h != b.h) {
        why = "size differs: " + std::to_string(a.w) + "x" + std::to_string(a.h) + " vs " +
              std::to_string(b.w) + "x" + std::to_string(b.h);
        return false;
    }
    for (size_t i = 0; i < a.px.size(); ++i) {
        if (a.px[i] != b.px[i]) {
            why = "pixel #" + std::to_string(i) + " differs";
            return false;
        }
    }
    why = "all " + std::to_string(a.px.size()) + " pixels identical";
    return true;
}

bool close_pixels(const Image& a, const Image& b, double max_avg, int max_peak, std::string& why) {
    if (a.w != b.w || a.h != b.h) { why = "size differs"; return false; }
    double sum = 0;
    int mx = 0;
    for (int y = 0; y < a.h; ++y) {
        for (int x = 0; x < a.w; ++x) {
            for (int c = 0; c < 3; ++c) {
                int va = c == 0 ? a.r(x, y) : (c == 1 ? a.g(x, y) : a.b(x, y));
                int vb = c == 0 ? b.r(x, y) : (c == 1 ? b.g(x, y) : b.b(x, y));
                int d = va > vb ? va - vb : vb - va;
                sum += d;
                if (d > mx) mx = d;
            }
        }
    }
    double avg = sum / (double)(a.w * a.h * 3);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "avg diff %.2f, max diff %d", avg, mx);
    why = buf;
    return avg <= max_avg && mx <= max_peak;
}

// 去掉 TXT 中以 '#' 开头的注释行（用于测试"无文件头"的容错读取）
bool strip_comments(const std::string& in, const std::string& out, std::string& err) {
    std::ifstream f(std::filesystem::path(utf8_to_wide(in)), std::ios::binary);
    if (!f) { err = "cannot open " + in; return false; }
    std::ofstream g(std::filesystem::path(utf8_to_wide(out)), std::ios::binary);
    if (!g) { err = "cannot create " + out; return false; }
    std::string line;
    while (std::getline(f, line)) {
        std::string t = trim(line);
        if (!t.empty() && t[0] == '#') continue;
        g << line << "\n";
    }
    return true;
}

int cmd_to_txt(const Args& ar) {
    std::string in = ar.get("-i"), out = ar.get("-o");
    if (in.empty() || out.empty()) { usage(); return 2; }
    Image img;
    std::string err;
    if (!load_image(in, img, err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    ExportOptions opt;
    opt.include_header = !ar.has("--no-header");
    std::string fname = ar.get("-f", "CHANNELS");
    if (!txt_format_from_name(fname, opt.fmt)) {
        std::printf("ERROR: unknown format '%s'\n", fname.c_str());
        return 2;
    }
    if (!export_matrix_txt(out, img, opt, err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    std::error_code ec;
    auto sz = std::filesystem::file_size(std::filesystem::path(utf8_to_wide(out)), ec);
    std::printf("OK  %s (%s %dx%d) -> %s [%s, %s]\n", file_name_of(in).c_str(),
                format_name(format_from_path(in)), img.w, img.h, file_name_of(out).c_str(),
                txt_format_name(opt.fmt), ec ? "?" : human_bytes(sz).c_str());
    return 0;
}

int cmd_to_image(const Args& ar) {
    std::string in = ar.get("-i"), out = ar.get("-o");
    if (in.empty() || out.empty()) { usage(); return 2; }
    Image img;
    ImportInfo info;
    std::string err;
    int forced_w = ar.geti("--width", 0);
    if (!import_matrix_txt(in, forced_w, img, info, err)) {
        std::printf("ERROR: %s\n", err.c_str());
        if (info.needs_width) std::printf("HINT: pass --width N\n");
        return 1;
    }
    int q = ar.geti("-q", 92);
    if (!save_image(out, img, err, q)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    std::printf("OK  %s [%s %dx%d, %llu values] -> %s\n", file_name_of(in).c_str(),
                info.format_name.c_str(), img.w, img.h, info.values, file_name_of(out).c_str());
    if (!info.note.empty()) std::printf("    note: %s\n", info.note.c_str());
    return 0;
}

int cmd_info(const Args& ar) {
    std::string in = ar.get("-i");
    if (in.empty()) { usage(); return 2; }
    std::string err;
    if (iends_with(in, ".txt")) {
        Image img;
        ImportInfo info;
        if (!import_matrix_txt(in, ar.geti("--width", 0), img, info, err) && !info.needs_width) {
            std::printf("ERROR: %s\n", err.c_str());
            return 1;
        }
        std::printf("TXT  %s\n  format=%s  %dx%d  values=%llu%s\n", file_name_of(in).c_str(),
                    info.format_name.c_str(), info.width, info.height, info.values,
                    info.needs_width ? "  (needs --width)" : "");
        if (!info.note.empty()) std::printf("  note: %s\n", info.note.c_str());
        if (!img.empty()) {
            std::printf("  sample: px(0,0)=R%d G%d B%d gray=%d\n", img.r(0, 0), img.g(0, 0),
                        img.b(0, 0), img.gray(0, 0));
        }
        return 0;
    }
    Image img;
    if (!load_image(in, img, err)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    std::printf("IMG  %s\n  type=%s  size=%dx%d  pixels=%s  alpha=%s\n", file_name_of(in).c_str(),
                format_name(format_from_path(in)), img.w, img.h,
                with_thousands(img.px.size()).c_str(), img.has_alpha() ? "yes" : "no");
    std::printf("  sample: px(0,0)=R%d G%d B%d gray=%d | px(%d,%d)=R%d G%d B%d gray=%d\n",
                img.r(0, 0), img.g(0, 0), img.b(0, 0), img.gray(0, 0), img.w - 1, img.h - 1,
                img.r(img.w - 1, img.h - 1), img.g(img.w - 1, img.h - 1), img.b(img.w - 1, img.h - 1),
                img.gray(img.w - 1, img.h - 1));
    return 0;
}

int cmd_gen(const Args& ar) {
    std::string out = ar.get("-o", "test_pattern.png");
    int w = ar.geti("-w", 320), h = ar.geti("-h", 240);
    Image img = make_test_pattern(w, h);
    std::string err;
    if (!save_image(out, img, err, 95)) {
        std::printf("ERROR: %s\n", err.c_str());
        return 1;
    }
    std::printf("OK  generated test pattern %dx%d -> %s\n", w, h, out.c_str());
    return 0;
}

int cmd_selftest(const Args& ar) {
    std::string dir = ar.get("-d", "selftest_out");
    std::filesystem::create_directories(std::filesystem::path(utf8_to_wide(dir)));
    std::string err;

    banner();
    std::printf("\n== selftest (workdir: %s) ==\n", dir.c_str());

    Image src = make_test_pattern(64, 48);
    check(!src.empty(), "generate test pattern 64x48");
    if (src.empty()) return 1;
    auto P = [&](const std::string& n) { return dir + "\\" + n; };

    // ---- 图片编解码 ----
    {
        Image back;
        err.clear();
        check(save_image(P("t.bmp"), src, err, 92) && load_image(P("t.bmp"), back, err) &&
                  same_pixels(src, back, err),
              "BMP round-trip (built-in codec)", err);
    }
    {
        Image back;
        err.clear();
        check(save_image(P("t.png"), src, err, 92) && load_image(P("t.png"), back, err) &&
                  same_pixels(src, back, err),
              "PNG round-trip (GDI+)", err);
    }
    {
        Image back;
        err.clear();
        check(save_image(P("t.jpg"), src, err, 95) && load_image(P("t.jpg"), back, err) &&
                  close_pixels(src, back, 8.0, 160, err),
              "JPEG round-trip (lossy, tolerance)", err);
    }
    {
        Image back;
        err.clear();
        check(save_image(P("t.tif"), src, err, 92) && load_image(P("t.tif"), back, err) &&
                  same_pixels(src, back, err),
              "TIFF round-trip (GDI+)", err);
    }

    // ---- 各 TXT 格式往返 ----
    const TxtFormat fmts[] = {TxtFormat::RGB, TxtFormat::RGBA, TxtFormat::GRAY,
                              TxtFormat::CHANNELS, TxtFormat::HEX};
    for (TxtFormat f : fmts) {
        ExportOptions opt;
        opt.fmt = f;
        std::string txt = P(std::string("m_") + txt_format_name(f) + ".txt");
        Image back;
        ImportInfo info;
        err.clear();
        bool ok = export_matrix_txt(txt, src, opt, err);
        if (ok) ok = import_matrix_txt(txt, 0, back, info, err);
        if (ok) {
            if (f == TxtFormat::GRAY) {
                bool eq = (back.w == src.w && back.h == src.h);
                for (int y = 0; eq && y < src.h; ++y)
                    for (int x = 0; eq && x < src.w; ++x)
                        if (back.r(x, y) != src.gray(x, y) || back.g(x, y) != src.gray(x, y) ||
                            back.b(x, y) != src.gray(x, y))
                            eq = false;
                err = eq ? "gray value of every pixel matches" : "gray mismatch";
                check(eq, std::string("TXT round-trip ") + txt_format_name(f) +
                              " (gray values verified)", err);
            } else {
                check(same_pixels(src, back, err),
                      std::string("TXT round-trip ") + txt_format_name(f) + " (pixel exact)", err);
            }
        } else {
            check(false, std::string("TXT round-trip ") + txt_format_name(f), err);
        }
    }

    // ---- 无文件头 / 缺宽度的容错 ----
    {
        // 每行一个像素的 RGB 文本：去掉注释头后信息不足，必须询问宽度
        std::string nohdr = P("m_rgb_noheader.txt");
        err.clear();
        check(strip_comments(P("m_RGB.txt"), nohdr, err), "strip header comments", err);
        Image back;
        ImportInfo info;
        err.clear();
        bool needs = !import_matrix_txt(nohdr, 0, back, info, err) && info.needs_width;
        check(needs, "headerless RGB pixels -> asks for width", err);
        Image back2;
        ImportInfo info2;
        err.clear();
        bool ok = import_matrix_txt(nohdr, src.w, back2, info2, err) && same_pixels(src, back2, err);
        check(ok, "headerless RGB pixels + width 64 -> pixel exact", err);
    }
    {
        // 无文件头的 CHANNELS：4 段等宽矩阵无法与"高的灰度图"区分，按灰度矩阵整块读入
        std::string nohdr = P("m_chan_noheader.txt");
        err.clear();
        check(strip_comments(P("m_CHANNELS.txt"), nohdr, err), "strip header comments (channels)", err);
        Image back;
        ImportInfo info;
        err.clear();
        bool ok = import_matrix_txt(nohdr, 0, back, info, err) && back.w == src.w &&
                  back.h == src.h * 4 && back.gray(0, 0) == src.r(0, 0) &&
                  back.gray(0, src.h) == src.g(0, 0) && back.gray(0, src.h * 2) == src.b(0, 0);
        check(ok, "headerless CHANNELS data -> one tall gray matrix (documented)", err);
    }
    {
        // 灰度矩阵无文件头：按"行 x 列"自动推断宽高
        std::string nohdr = P("m_gray_noheader.txt");
        err.clear();
        check(strip_comments(P("m_GRAY.txt"), nohdr, err), "strip header (gray)", err);
        Image back;
        ImportInfo info;
        err.clear();
        bool ok = import_matrix_txt(nohdr, 0, back, info, err) && back.w == src.w && back.h == src.h;
        check(ok, "headerless GRAY matrix -> size inferred from rows/cols", err);
    }
    {
        // 0-1 归一化数值
        std::string s = P("m_norm.txt");
        {
            std::ofstream g(std::filesystem::path(utf8_to_wide(s)));
            g << "# width 2\n# height 2\n# format RGB\n# max 1\n";
            g << "0, 0.5, 1\n1, 0, 0.5\n0.25, 0.25, 0.25\n1, 1, 1\n";
        }
        Image back;
        ImportInfo info;
        err.clear();
        bool ok = import_matrix_txt(s, 0, back, info, err) && back.w == 2 && back.h == 2 &&
                  back.r(0, 0) == 0 && back.g(0, 0) == 128 && back.b(0, 0) == 255 &&
                  back.r(1, 1) == 255;
        check(ok, "normalized 0-1 values scaled to 0-255", err);
    }
    {
        // 手写矩阵文本：没有文件头时先询问宽度，给出宽度后按灰度矩阵解析
        std::string s = P("m_handwritten.txt");
        {
            std::ofstream g(std::filesystem::path(utf8_to_wide(s)));
            g << "0 10 20 30\n40 50 60 70\n80 90 100 110\n";
        }
        Image back;
        ImportInfo info;
        err.clear();
        bool needs = !import_matrix_txt(s, 0, back, info, err) && info.needs_width;
        check(needs, "hand written matrix (no header) -> asks for width", err);
        Image back2;
        ImportInfo info2;
        err.clear();
        bool ok = import_matrix_txt(s, 4, back2, info2, err) && back2.w == 4 && back2.h == 3 &&
                  back2.r(1, 0) == 10 && back2.gray(3, 2) == 110;
        check(ok, "hand written matrix + width 4 -> 4x3 gray", err);
    }

    std::printf("\n== %s: %d failure(s) ==\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
    return g_fail ? 1 : 0;
}

} // namespace

static int run_cli(const std::vector<std::string>& args) {
    Args ar;
    ar.a = args;
    if (args.size() < 2) { usage(); return 2; }
    const std::string cmd = to_lower(args[1]);
    if (cmd == "to-txt") return cmd_to_txt(ar);
    if (cmd == "to-image") return cmd_to_image(ar);
    if (cmd == "info") return cmd_info(ar);
    if (cmd == "gen") return cmd_gen(ar);
    if (cmd == "selftest") return cmd_selftest(ar);
    if (cmd == "gui") return run_gui();
    if (cmd == "help" || cmd == "-h" || cmd == "--help") { usage(); return 0; }
    usage();
    return 2;
}

#ifdef IMAGEMATRIX_GUI
// ---------------- GUI 子系统入口（Windows 图形程序） ----------------
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    ::SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> args = get_args_utf8();
    const bool cli_mode = args.size() > 1 && !iequals(args[1], "gui") && args[1] != "--gui";
    if (cli_mode) {
        // GUI 子系统没有控制台，附加到调用者的控制台后仍可输出
        if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
            (void)std::freopen("CONOUT$", "w", stdout);
            (void)std::freopen("CONOUT$", "w", stderr);
        }
        return run_cli(args);
    }
    return run_gui();
}
#else
// ---------------- 控制台子系统入口 ----------------
int main() {
    ::SetConsoleOutputCP(CP_UTF8);
    return run_cli(get_args_utf8());
}
#endif
