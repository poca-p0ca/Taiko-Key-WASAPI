#include "app/Controller.h"
#include "app/OriginalMainUi.h"
#include <commctrl.h>
#include <commdlg.h>
#include <memory>
#include <shellapi.h>

namespace {
using namespace taiko;
// Native controls and bitmap/BitBlt composition follow Taiko-Key-ASIO's
// TaikoKeyASIO_main.cpp (c3ed9a6). See THIRD_PARTY_NOTICES.md for provenance.
constexpr int Width = 400, Height = 400;
enum {
    DeviceBox = 100,
    PeriodBox,
    DonButton,
    KatButton,
    VolumeSlider,
    ReloadButton,
    ImportButton,
    EditButton,
    FolderButton,
    RefreshButton,
    SettingsMenu
};
std::unique_ptr<Controller> app;
HWND mainWindow{}, settingsWindow{}, mainVolume{}, mainSlider{};
HWND periods{}, statusText{}, pathText{}, keysText{}, detailsText{}, errorText{}, volumeText{}, slider{};
HFONT font{};
bool volumeInitialized{};

struct DevicePicker {
    HWND window{};
    std::vector<Device> displayed, enumerated;
    std::wstring selected;
    void update(const Snapshot& s) {
        if (!window || SendMessageW(window, CB_GETDROPPEDSTATE, 0, 0))
            return;
        bool changed = selected != s.settings.device || enumerated.size() != s.devices.size();
        if (!changed)
            for (size_t i = 0; i < enumerated.size(); ++i)
                if (enumerated[i].id != s.devices[i].id || enumerated[i].name != s.devices[i].name) {
                    changed = true;
                    break;
                }
        if (!changed && SendMessageW(window, CB_GETCOUNT, 0, 0) > 0)
            return;
        enumerated = displayed = s.devices;
        selected = s.settings.device;
        SendMessageW(window, CB_RESETCONTENT, 0, 0);
        SendMessageW(window, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Default"));
        int selection = 0;
        for (size_t i = 0; i < displayed.size(); ++i) {
            SendMessageW(window, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(displayed[i].name.c_str()));
            if (displayed[i].id == selected)
                selection = static_cast<int>(i + 1);
        }
        if (!selected.empty() && selection == 0) {
            displayed.push_back({selected, L"선택한 장치 — 연결 대기"});
            SendMessageW(window, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(displayed.back().name.c_str()));
            selection = static_cast<int>(displayed.size());
        }
        SendMessageW(window, CB_SETCURSEL, selection, 0);
    }
    void changed() const {
        int selectedIndex = static_cast<int>(SendMessageW(window, CB_GETCURSEL, 0, 0));
        if (selectedIndex >= 0 && size_t(selectedIndex) <= displayed.size())
            app->post({CommandType::Device, selectedIndex ? displayed[selectedIndex - 1].id : L"", {}});
    }
} mainDevices, settingsDevices;

HWND control(HWND parent, LPCWSTR cls, LPCWSTR text, DWORD style, int x, int y, int width, int height,
             int id = 0, HFONT customFont = nullptr) {
    HWND w = CreateWindowExW(wcscmp(cls, WC_EDITW) == 0 ? WS_EX_CLIENTEDGE : 0, cls, text,
                             WS_CHILD | WS_VISIBLE | style, x, y, width, height, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr),
                             nullptr);
    SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(customFont ? customFont : font), TRUE);
    return w;
}
void setText(HWND w, const std::wstring& text) {
    if (!w)
        return;
    int n = GetWindowTextLengthW(w);
    std::wstring current(n + 1, 0);
    GetWindowTextW(w, current.data(), n + 1);
    current.resize(n);
    if (current != text)
        SetWindowTextW(w, text.c_str());
}
void setSlider(HWND w, int value) {
    if (w && SendMessageW(w, TBM_GETPOS, 0, 0) != value)
        SendMessageW(w, TBM_SETPOS, TRUE, value);
}
void updateVolume() {
    int value = app->volume();
    setSlider(mainSlider, value);
    setSlider(slider, value);
    setText(mainVolume, L"Volume: " + std::to_wstring(value));
    setText(volumeText, L"볼륨: " + std::to_wstring(value) + L"%");
}
void update() {
    auto s = app->snapshot();
    mainDevices.update(s);
    setText(mainWindow, !s.error.empty() ? L"Taiko Key WASAPI — 설정 확인 필요" : L"Taiko Key WASAPI");
    if (!volumeInitialized && s.revision) {
        volumeInitialized = true;
        EnableWindow(mainSlider, TRUE);
        if (slider)
            EnableWindow(slider, TRUE);
    }
    if (volumeInitialized)
        updateVolume();
    if (!settingsWindow || !IsWindowVisible(settingsWindow))
        return;
    settingsDevices.update(s);
    setText(statusText, L"상태: " + s.status);
    setText(pathText, s.settings.config.wstring());
    setText(keysText, s.bindings);
    setText(detailsText, s.details);
    setText(errorText, s.error.empty() ? L"" : L"오류가 있습니다. 아래 진단 내용을 확인하세요.");
    if (!SendMessageW(periods, CB_GETDROPPEDSTATE, 0, 0))
        SendMessageW(periods, CB_SETCURSEL, s.settings.stable ? 1 : 0, 0);
    EnableWindow(GetDlgItem(settingsWindow, DonButton), s.running && s.previewDon);
    EnableWindow(GetDlgItem(settingsWindow, KatButton), s.running && s.previewKat);
}
void initSlider(HWND w) {
    SendMessageW(w, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));
    SendMessageW(w, TBM_SETPOS, TRUE, app->volume());
    SendMessageW(w, TBM_SETPAGESIZE, 0, 5);
    EnableWindow(w, volumeInitialized);
}
void fileCommand(HWND w, int id) {
    if (id == ReloadButton)
        app->post({CommandType::Reload, {}, {}});
    if (id == RefreshButton)
        app->post({CommandType::Refresh, {}, {}});
    if (id == FolderButton)
        ShellExecuteW(w, L"open", app->root().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (id == EditButton) {
        auto path = app->snapshot().settings.config;
        std::wstring arg = L"\"" + path.wstring() + L"\"";
        ShellExecuteW(w, L"open", L"notepad.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
    }
    if (id == ImportButton) {
        wchar_t file[32768]{};
        OPENFILENAMEW open{};
        open.lStructSize = sizeof(open);
        open.hwndOwner = w;
        open.lpstrFilter = L"KeyBind INI\0*.ini\0All files\0*.*\0";
        open.lpstrFile = file;
        open.nMaxFile = 32768;
        open.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&open))
            app->post({CommandType::Import, file, {}});
    }
}
LRESULT CALLBACK settingsProcedure(HWND w, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE:
        control(w, WC_STATICW, L"오디오 및 키 설정", 0, 24, 18, 740, 28);
        control(w, WC_STATICW, L"출력 장치", 0, 24, 60, 120, 24);
        settingsDevices.window = control(w, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                                         148, 56, 472, 260, DeviceBox);
        control(w, WC_BUTTONW, L"장치 새로고침", WS_TABSTOP, 632, 56, 144, 28, RefreshButton);
        control(w, WC_STATICW, L"지연 설정", 0, 24, 102, 120, 24);
        periods = control(w, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, 148, 98, 628, 120, PeriodBox);
        SendMessageW(periods, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"최소 주기 · 목표 큐 1주기"));
        SendMessageW(periods, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"안정성 우선 · 기본 주기 / 목표 큐 2주기"));
        volumeText = control(w, WC_STATICW, L"", 0, 24, 146, 120, 24);
        slider =
            control(w, TRACKBAR_CLASSW, L"볼륨", WS_TABSTOP | TBS_NOTICKS, 148, 138, 348, 36, VolumeSlider);
        initSlider(slider);
        control(w, WC_BUTTONW, L"동 미리듣기", WS_TABSTOP, 508, 138, 128, 32, DonButton);
        control(w, WC_BUTTONW, L"캇 미리듣기", WS_TABSTOP, 648, 138, 128, 32, KatButton);
        control(w, WC_STATICW, L"키 설정 파일", 0, 24, 192, 120, 24);
        pathText = control(w, WC_EDITW, L"", ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP, 148, 188, 628, 28);
        keysText = control(w, WC_STATICW, L"", SS_NOPREFIX, 24, 230, 752, 42);
        control(w, WC_BUTTONW, L"키 / 음원 편집", WS_TABSTOP, 24, 280, 180, 34, EditButton);
        control(w, WC_BUTTONW, L"다시 읽기", WS_TABSTOP, 216, 280, 160, 34, ReloadButton);
        control(w, WC_BUTTONW, L"INI 가져오기", WS_TABSTOP, 388, 280, 168, 34, ImportButton);
        control(w, WC_BUTTONW, L"설정 / 로그 폴더", WS_TABSTOP, 568, 280, 208, 34, FolderButton);
        statusText = control(w, WC_STATICW, L"", 0, 24, 336, 752, 24);
        errorText = control(w, WC_STATICW, L"", 0, 24, 364, 752, 24);
        detailsText =
            control(w, WC_EDITW, L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP,
                    24, 398, 752, 178);
        control(w, WC_STATICW,
                L"앱 실행 중 항상 입력을 받습니다. 이 설정창을 닫거나 최소화해도 재생은 유지됩니다.", 0, 24,
                592, 752, 24);
        control(w, WC_STATICW, L"osu!의 타격 효과음은 게임 설정에서 직접 꺼 주세요.", 0, 24, 620, 752, 24);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) {
            ShowWindow(w, SW_HIDE);
            SetForegroundWindow(mainWindow);
        }
        if (LOWORD(wp) == DeviceBox && HIWORD(wp) == CBN_SELCHANGE)
            settingsDevices.changed();
        if (LOWORD(wp) == PeriodBox && HIWORD(wp) == CBN_SELCHANGE)
            app->post({CommandType::Period, {}, SendMessageW(periods, CB_GETCURSEL, 0, 0) == 1});
        if (LOWORD(wp) == DonButton)
            app->preview(1);
        if (LOWORD(wp) == KatButton)
            app->preview(2);
        fileCommand(w, LOWORD(wp));
        return 0;
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lp) == slider) {
            app->setVolume(static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0)));
            updateVolume();
        }
        return 0;
    case WM_CLOSE:
        ShowWindow(w, SW_HIDE);
        SetForegroundWindow(mainWindow);
        return 0;
    }
    return DefWindowProcW(w, message, wp, lp);
}
HWND createWindow(LPCWSTR cls, LPCWSTR title, int width, int height, HWND owner = nullptr,
                  HMENU menu = nullptr) {
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT rect{0, 0, width, height};
    AdjustWindowRectEx(&rect, style, menu != nullptr, 0);
    return CreateWindowExW(0, cls, title, style, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                           rect.bottom - rect.top, owner, menu, GetModuleHandleW(nullptr), nullptr);
}
void showSettings() {
    if (!settingsWindow) {
        settingsWindow =
            createWindow(L"TaikoKeyWASAPI.Settings", L"Taiko Key WASAPI — Settings", 800, 660, mainWindow);
        if (!settingsWindow) {
            MessageBoxW(mainWindow, L"설정창을 열 수 없습니다.", L"Taiko Key WASAPI", MB_ICONERROR);
            return;
        }
    }
    ShowWindow(settingsWindow, SW_SHOWNORMAL);
    update();
    SetForegroundWindow(settingsWindow);
}
LRESULT CALLBACK procedure(HWND w, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE: {
        original_ui::create(w, GetModuleHandleW(nullptr));
        mainDevices.window = original_ui::hCombo;
        mainVolume = original_ui::hVolumeDesc;
        mainSlider = original_ui::hTrackbarVolume;
        // Preserve saved volume instead of the original app's hard-coded 100%.
        SendMessageW(mainSlider, TBM_SETPOS, TRUE, app->volume());
        EnableWindow(mainSlider, FALSE);
        SetTimer(w, 1, 250, nullptr);
        return 0;
    }
    case WM_PAINT:
        original_ui::paint(w, GetModuleHandleW(nullptr));
        return 0;
    case WM_CTLCOLORSTATIC:
        return original_ui::color(w, message, wp, lp);
    case WM_LBUTTONDOWN:
        SetFocus(w);
        return 0;
    case WM_TIMER:
        update();
        return 0;
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lp) == mainSlider) {
            app->setVolume(static_cast<int>(SendMessageW(mainSlider, TBM_GETPOS, 0, 0)));
            updateVolume();
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND)
            app->post({CommandType::Resume, {}, {}});
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == DeviceBox && HIWORD(wp) == CBN_SELCHANGE)
            mainDevices.changed();
        if (LOWORD(wp) == SettingsMenu)
            showSettings();
        fileCommand(w, LOWORD(wp));
        return 0;
    case WM_DESTROY:
        KillTimer(w, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, message, wp, lp);
}
void releaseUi() {
    original_ui::release();
    DeleteObject(font);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        // Keep the mutex shared with 0.1.x: never run two global input players.
        taiko::Handle singleton(CreateMutexW(nullptr, TRUE, L"Local\\TaikoKeyWASAPI-0.1"));
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            MessageBoxW(nullptr, L"Taiko Key WASAPI가 이미 실행 중입니다.", L"Taiko Key WASAPI", MB_OK);
            return 0;
        }
        INITCOMMONCONTROLSEX cc{sizeof(cc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
        InitCommonControlsEx(&cc);
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
        font = CreateFontIndirectW(&metrics.lfMessageFont);
        wchar_t exe[32768]{};
        DWORD length = GetModuleFileNameW(nullptr, exe, 32768);
        if (!length || length >= 32768)
            throw std::runtime_error("Cannot locate application directory");
        // Resolve from the EXE, not the shortcut's working directory or AppData.
        app = std::make_unique<taiko::Controller>(std::filesystem::path(exe).parent_path());
        WNDCLASSW cls{};
        cls.hInstance = instance;
        cls.style = CS_HREDRAW | CS_VREDRAW;
        cls.lpfnWndProc = procedure;
        cls.lpszClassName = L"TaikoKeyWASAPI";
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&cls);
        cls.lpfnWndProc = settingsProcedure;
        cls.lpszClassName = L"TaikoKeyWASAPI.Settings";
        cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        RegisterClassW(&cls);
        HMENU menu = LoadMenuW(instance, MAKEINTRESOURCEW(102));
        if (!menu)
            throw std::runtime_error("Cannot load native Settings menu");
        mainWindow = CreateWindowW(L"TaikoKeyWASAPI", L"Taiko Key WASAPI",
                                   WS_OVERLAPPEDWINDOW & (~WS_THICKFRAME) & (~WS_MAXIMIZEBOX), CW_USEDEFAULT,
                                   CW_USEDEFAULT, Width, Height, nullptr, menu, instance, nullptr);
        if (!mainWindow) {
            DestroyMenu(menu);
            throw std::runtime_error("Cannot create application window");
        }
        original_ui::setClientRect(mainWindow, Width, Height);
        app->start();
        ShowWindow(mainWindow, show);
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            if (settingsWindow && IsWindowVisible(settingsWindow) && IsDialogMessageW(settingsWindow, &msg))
                continue;
            if (!IsDialogMessageW(mainWindow, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        app.reset();
        releaseUi();
        return 0;
    } catch (const std::exception& e) {
        app.reset();
        releaseUi();
        MessageBoxW(nullptr, taiko::wide(e.what()).c_str(), L"Taiko Key WASAPI", MB_ICONERROR);
        return 1;
    }
}
