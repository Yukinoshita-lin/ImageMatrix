/* ImageMatrix 卸载器 —— 纯 Win32 实现。
 *
 * 由安装器释放到 %LOCALAPPDATA%\Programs\ImageMatrix\uninstall.exe。
 * 「设置 - 应用」与开始菜单的卸载入口都指向本程序。
 *
 * 用法: uninstall.exe [-Force]   (-Force 跳过确认与结果弹窗，用于自动化测试)
 */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <wchar.h>

/* SHFileOperation 删除文件/目录（pFrom 需双零结尾） */
static void DelPath(const wchar_t *p)
{
    wchar_t buf[MAX_PATH + 1];
    SHFILEOPSTRUCTW op;

    lstrcpynW(buf, p, MAX_PATH);
    buf[lstrlenW(buf) + 1] = 0;
    ZeroMemory(&op, sizeof(op));
    op.wFunc = FO_DELETE;
    op.pFrom = buf;
    op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    SHFileOperationW(&op);
}

static wchar_t *PathJoin(wchar_t *buf, size_t cch, const wchar_t *a, const wchar_t *b)
{
    _snwprintf(buf, cch - 1, L"%s\\%s", a, b);
    buf[cch - 1] = 0;
    return buf;
}

int wmain(int argc, wchar_t **argv)
{
    int force = 0, i, n;
    wchar_t dir[MAX_PATH], p1[MAX_PATH], appdata[MAX_PATH], desktop[MAX_PATH];
    wchar_t killcmd[64], cmd[MAX_PATH + 64];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;

    for (i = 1; i < argc; i++)
        if (!wcscmp(argv[i], L"-Force") || !wcscmp(argv[i], L"/Force"))
            force = 1;

    /* 安装目录 = uninstall.exe 所在目录 */
    GetModuleFileNameW(NULL, dir, MAX_PATH);
    n = lstrlenW(dir);
    while (n && dir[n - 1] != L'\\') n--;
    dir[n ? n - 1 : 0] = 0;

    if (!force && MessageBoxW(NULL, L"确定要卸载 ImageMatrix 吗？",
                              L"卸载 ImageMatrix",
                              MB_YESNO | MB_ICONQUESTION) != IDYES)
        return 0;

    /* 结束正在运行的程序（隐藏控制台） */
    lstrcpyW(killcmd, L"taskkill /f /im imagematrix.exe");
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (CreateProcessW(NULL, killcmd, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                       NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    /* 开始菜单文件夹 + 桌面快捷方式 */
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata))) {
        DelPath(PathJoin(p1, MAX_PATH, appdata,
                         L"Microsoft\\Windows\\Start Menu\\Programs\\ImageMatrix"));
    }
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop))) {
        DelPath(PathJoin(p1, MAX_PATH, desktop, L"ImageMatrix.lnk"));
    }

    /* 卸载注册项 */
    SHDeleteKeyW(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ImageMatrix");

    /* 程序目录（uninstall.exe 自身被占用，交给延时 rd 收尾） */
    DelPath(dir);
    _snwprintf(cmd, sizeof(cmd) / sizeof(wchar_t) - 1,
               L"cmd /c ping -n 4 127.0.0.1 > nul & rd /s /q \"%s\"", dir);
    cmd[sizeof(cmd) / sizeof(wchar_t) - 1] = 0;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                       NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }

    if (!force)
        MessageBoxW(NULL, L"ImageMatrix 已卸载。", L"卸载 ImageMatrix",
                    MB_OK | MB_ICONINFORMATION);
    return 0;
}
