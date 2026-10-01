/* ImageMatrix 每用户安装器 —— 纯 Win32 实现，无第三方依赖。
 *
 * 程序文件以 RCDATA 资源嵌入本 exe（见 installer.rc），运行时写入
 * %LOCALAPPDATA%\Programs\ImageMatrix，创建开始菜单/桌面快捷方式，
 * 并在 HKCU 注册卸载信息（「设置 - 应用」可见）。
 *
 * 用法: ImageMatrix_Setup.exe [-Silent]   (-Silent 跳过确认与完成弹窗，用于自动化测试)
 */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shlobj.h>
#include <wchar.h>

#define RES_APP    101      /* RCDATA: imagematrix.exe     */
#define RES_CLI    102      /* RCDATA: imagematrix-cli.exe */
#define RES_README 103      /* RCDATA: README.md           */
#define RES_UNINST 104      /* RCDATA: uninstall.exe       */

static const wchar_t *APP_DESC =
    L"ImageMatrix - 图片与数值矩阵(R/G/B/灰度)双向转换";

/* 把资源内容写为文件 */
static BOOL WriteResToFile(int resId, const wchar_t *path)
{
    HRSRC hr = FindResourceW(NULL, MAKEINTRESOURCEW(resId), (LPCWSTR)RT_RCDATA);
    HGLOBAL hg;
    const void *data;
    DWORD size, written;
    HANDLE f;
    BOOL ok;

    if (!hr) return FALSE;
    hg = LoadResource(NULL, hr);
    if (!hg) return FALSE;
    data = LockResource(hg);
    size = SizeofResource(NULL, hr);
    if (!data || !size) return FALSE;

    f = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    ok = WriteFile(f, data, size, &written, NULL) && written == size;
    CloseHandle(f);
    return ok;
}

static DWORD ResSize(int resId)
{
    HRSRC hr = FindResourceW(NULL, MAKEINTRESOURCEW(resId), (LPCWSTR)RT_RCDATA);
    return hr ? SizeofResource(NULL, hr) : 0;
}

/* 安全路径拼接：buf = a\b（截断安全） */
static wchar_t *PathJoin(wchar_t *buf, size_t cch, const wchar_t *a, const wchar_t *b)
{
    _snwprintf(buf, cch - 1, L"%s\\%s", a, b);
    buf[cch - 1] = 0;
    return buf;
}

/* 创建 .lnk 快捷方式（C++ 语义：MinGW 头文件中 COM 接口为带方法的类） */
static HRESULT MakeShortcut(const wchar_t *target, const wchar_t *workdir,
                            const wchar_t *desc, const wchar_t *lnk)
{
    IShellLinkW *sl;
    IPersistFile *pf;
    HRESULT h;

    h = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                         IID_IShellLinkW, (void **)&sl);
    if (FAILED(h)) return h;
    sl->SetPath(target);
    sl->SetWorkingDirectory(workdir);
    sl->SetDescription(desc);
    h = sl->QueryInterface(IID_IPersistFile, (void **)&pf);
    if (SUCCEEDED(h)) {
        h = pf->Save(lnk, TRUE);
        pf->Release();
    }
    sl->Release();
    return h;
}

static void SetStr(HKEY hk, const wchar_t *name, const wchar_t *val)
{
    RegSetValueExW(hk, name, 0, REG_SZ,
                   (const BYTE *)val, (DWORD)((lstrlenW(val) + 1) * sizeof(wchar_t)));
}

int wmain(int argc, wchar_t **argv)
{
    int silent = 0, i, failed = 0;
    wchar_t local[MAX_PATH], appdata[MAX_PATH], desktop[MAX_PATH];
    wchar_t dir[MAX_PATH], p1[MAX_PATH], p2[MAX_PATH], p3[MAX_PATH];
    wchar_t uninst[MAX_PATH], err[512];
    HKEY hk;
    DWORD kb, disp;

    err[0] = 0;
    for (i = 1; i < argc; i++)
        if (!wcscmp(argv[i], L"-Silent") || !wcscmp(argv[i], L"/Silent"))
            silent = 1;

    if (!silent && MessageBoxW(NULL,
            L"将把 ImageMatrix 安装到当前用户目录（无需管理员权限），\n"
            L"并创建开始菜单 / 桌面快捷方式。\n\n是否继续？",
            L"ImageMatrix 安装程序", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return 0;

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)))
        return 1;

    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local))) {
        lstrcpyW(err, L"无法定位用户数据目录。");
        goto fail;
    }
    PathJoin(dir, MAX_PATH, local, L"Programs\\ImageMatrix");

    CreateDirectoryW(PathJoin(p1, MAX_PATH, local, L"Programs"), NULL);
    CreateDirectoryW(dir, NULL);

    /* 1. 释放程序文件 */
    if (!WriteResToFile(RES_APP,    PathJoin(p1, MAX_PATH, dir, L"imagematrix.exe")) ||
        !WriteResToFile(RES_CLI,    PathJoin(p2, MAX_PATH, dir, L"imagematrix-cli.exe")) ||
        !WriteResToFile(RES_README, PathJoin(p3, MAX_PATH, dir, L"README.md")) ||
        !WriteResToFile(RES_UNINST, PathJoin(p1, MAX_PATH, dir, L"uninstall.exe"))) {
        _snwprintf(err, 511, L"写入文件失败（错误码 %lu）。程序可能正在运行，请先关闭后重试。",
                   GetLastError());
        goto fail;
    }

    /* 2. 开始菜单快捷方式 */
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
        lstrcpyW(err, L"无法定位开始菜单目录。");
        goto fail;
    }
    PathJoin(p2, MAX_PATH, appdata, L"Microsoft\\Windows\\Start Menu\\Programs\\ImageMatrix");
    CreateDirectoryW(p2, NULL);
    if (FAILED(MakeShortcut(PathJoin(p1, MAX_PATH, dir, L"imagematrix.exe"), dir,
                            APP_DESC,
                            PathJoin(p3, MAX_PATH, p2, L"ImageMatrix.lnk"))) ||
        FAILED(MakeShortcut(PathJoin(p1, MAX_PATH, dir, L"uninstall.exe"), dir,
                            L"卸载 ImageMatrix",
                            PathJoin(p3, MAX_PATH, p2, L"卸载 ImageMatrix.lnk")))) {
        _snwprintf(err, 511, L"创建开始菜单快捷方式失败（0x%08lX）。", GetLastError());
        goto fail;
    }

    /* 3. 桌面快捷方式 */
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop))) {
        lstrcpyW(err, L"无法定位桌面目录。");
        goto fail;
    }
    if (FAILED(MakeShortcut(PathJoin(p1, MAX_PATH, dir, L"imagematrix.exe"), dir,
                            APP_DESC,
                            PathJoin(p3, MAX_PATH, desktop, L"ImageMatrix.lnk")))) {
        _snwprintf(err, 511, L"创建桌面快捷方式失败（0x%08lX）。", GetLastError());
        goto fail;
    }

    /* 4. 注册卸载信息（HKCU，设置 -> 应用 可见） */
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ImageMatrix",
                        0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL) != ERROR_SUCCESS) {
        lstrcpyW(err, L"注册卸载信息失败。");
        goto fail;
    }
    kb = (ResSize(RES_APP) + ResSize(RES_CLI) + ResSize(RES_README) +
          ResSize(RES_UNINST)) / 1024;
    SetStr(hk, L"DisplayName",     L"ImageMatrix");
    SetStr(hk, L"DisplayVersion",  L"1.0.0.0");
    SetStr(hk, L"Publisher",       L"ImageMatrix");
    SetStr(hk, L"InstallLocation", dir);
    SetStr(hk, L"DisplayIcon",     PathJoin(p1, MAX_PATH, dir, L"imagematrix.exe"));
    _snwprintf(uninst, MAX_PATH - 1, L"\"%s\"", PathJoin(p2, MAX_PATH, dir, L"uninstall.exe"));
    SetStr(hk, L"UninstallString", uninst);
    SetStr(hk, L"HelpLink",        L"https://github.com/Yukinoshita-lin/ImageMatrix");
    disp = 1;
    RegSetValueExW(hk, L"NoModify",      0, REG_DWORD, (const BYTE *)&disp, sizeof(disp));
    RegSetValueExW(hk, L"NoRepair",      0, REG_DWORD, (const BYTE *)&disp, sizeof(disp));
    RegSetValueExW(hk, L"EstimatedSize", 0, REG_DWORD, (const BYTE *)&kb,    sizeof(kb));
    RegCloseKey(hk);

    CoUninitialize();
    if (!silent)
        MessageBoxW(NULL,
                    L"ImageMatrix 安装完成！\n\n"
                    L"安装位置与快捷方式：\n"
                    L"  - 开始菜单 ImageMatrix 文件夹\n"
                    L"  - 桌面快捷方式\n\n"
                    L"如需卸载：设置 - 应用 - 安装的应用 - ImageMatrix",
                    L"ImageMatrix 安装程序", MB_OK | MB_ICONINFORMATION);
    return 0;

fail:
    CoUninitialize();
    if (!silent)
        MessageBoxW(NULL, err, L"ImageMatrix 安装程序", MB_OK | MB_ICONERROR);
    return 1;
}
