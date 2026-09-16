#ifndef UNICODE
#  define UNICODE
#endif

#include <windows.h>
#include <tlhelp32.h>
#include <exdisp.h>     // IShellWindows, IWebBrowserApp
#include <oleauto.h>    // BSTR/SysFreeString
#include <vector>
#include <string>

// ReSharper disable CppLocalVariableMayBeConst
// ReSharper disable CppDFAConstantFunctionResult
// ReSharper disable CppDFAConstantParameter
// ReSharper disable CppParameterMayBeConst

/**
 * REXPLORER — жёсткий перезапуск Windows Explorer с мгновенным восстановлением окон.
 *
 * Логика:
 *  1) Считывает список реально открытых папок, игнорируя виртуальные окна.
 *  2) Принудительно завершает все процессы проводника.
 *  3) Не запускает проводник вручную — ожидается автоподъём системой.
 *  4) Немедленно открывает сохранённые каталоги, чтобы проводник их подхватил.
 *
 * Код возврата:
 *  0 — успешное завершение.
 *
 */

//=====================================================================//
// ВСПОМОГАТЕЛЬНОЕ
//=====================================================================//

/**
 * Проверка префикса для широких строк.
 *
 * @param s       исходная строка.
 * @param prefix  предполагаемый префикс.
 * @return true, если строка начинается с указанного префикса; иначе false.
 */
static bool startsWith(const wchar_t* s, const wchar_t* prefix)
{
    if (!s || !prefix) return false;
    while (*prefix) { if (*s != *prefix) return false; ++s; ++prefix; }
    return true;
}

/**
 * Преобразование файлового URL в локальный путь.
 * Поддерживает базовое декодирование процента и замену разделителей.
 *
 * @param url  входной URL.
 * @return локальный путь; пустая строка при неподдерживаемом формате.
 */
static std::wstring urlToPath(const wchar_t* url)
{
    if (!url || !startsWith(url, L"file:///")) return {};
    const wchar_t* p = url + 8; // после "file:///"
    std::wstring out;
    while (*p) {
        if (*p == L'/') { out.push_back(L'\\'); ++p; }
        else if (*p == L'%' && p[1] && p[2]) {
            wchar_t hex[3] = { p[1], p[2], 0 };
            wchar_t* end = nullptr;
            long v = wcstol(hex, &end, 16);
            if (end && *end == 0) { out.push_back((wchar_t)v); p += 3; }
            else { out.push_back(*p++); }
        } else {
            out.push_back(*p++);
        }
    }
    return out;
}

/**
 * Сбор путей реально открытых папок проводника.
 * Виртуальные окна исключаются.
 *
 * @param out  контейнер для результата; очищается в начале.
 */
static void collectOpenFolderPaths(std::vector<std::wstring>& out)
{
    out.clear();

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool needUninit = SUCCEEDED(hr);

    IShellWindows* sw = nullptr;
    hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&sw));
    if (SUCCEEDED(hr) && sw) {
        long count = 0;
        if (SUCCEEDED(sw->get_Count(&count))) {
            long i = 0;
            while (i < count) {
                VARIANT vIdx; VariantInit(&vIdx); vIdx.vt = VT_I4; vIdx.lVal = i;

                IDispatch* disp = nullptr;
                if (SUCCEEDED(sw->Item(vIdx, &disp)) && disp) {
                    IWebBrowserApp* app = nullptr;
                    if (SUCCEEDED(disp->QueryInterface(IID_IWebBrowserApp, (void**)&app)) && app) {
                        BSTR burl = nullptr;
                        if (SUCCEEDED(app->get_LocationURL(&burl)) && burl) {
                            std::wstring p = urlToPath(burl);
                            if (!p.empty()) out.push_back(p);
                            SysFreeString(burl);
                        }
                        app->Release();
                    }
                    disp->Release();
                }
                ++i;
            }
        }
        sw->Release();
    }
    if (needUninit) CoUninitialize();
}

/**
 * Принудительное завершение всех процессов проводника.
 * Для каждого процесса выполняется локальное ожидание завершения.
 */
static void killAllExplorer()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (lstrcmpiW(pe.szExeFile, L"explorer.exe") == 0) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
                if (h) { TerminateProcess(h, 0); WaitForSingleObject(h, 2000); CloseHandle(h); }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

/**
 * Открытие списка каталогов в новых окнах проводника.
 * Выполняется последовательный запуск с небольшим интервалом.
 *
 * @param paths  набор путей для открытия.
 */
static void reopenFolders(const std::vector<std::wstring>& paths)
{
    size_t i = 0;
    while (i < paths.size()) {
        ShellExecuteW(nullptr, L"open", paths[i].c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        // Небольшой зазор, чтобы окна открывались корректно и равномерно
        Sleep(100);
        ++i;
    }
}

//=====================================================================//
// ОСНОВНОЙ ВХОД
//=====================================================================//

/**
 * Выполнение основной логики приложения.
 *
 * Порядок действий:
 *  — Сохранить список открытых реальных папок.
 *  — Завершить все процессы проводника.
 *  — Не запускать проводник напрямую; система поднимет его автоматически.
 *  — При наличии сохранённых путей — инициировать их открытие через Shell API.
 *
 * @return код завершения приложения.
 */
static int run()
{
    // 1) Список открытых реальных папок ДО рестарта
    std::vector<std::wstring> opened;
    collectOpenFolderPaths(opened);

    // 2) ЖЁСТКО завершаем Explorer
    killAllExplorer();

    // 3) НЕ стартуем explorer.exe напрямую — даём системе поднять его самой
    // 4) Если ранее были папки — сразу просим их открыть (Explorer подхватит)
    if (!opened.empty()) {
        reopenFolders(opened);
    }
    return 0;
}

/**
 * Графическая точка входа без консоли.
 *
 * @return код завершения приложения.
 */
int APIENTRY WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    return run();
}
