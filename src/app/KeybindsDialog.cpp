#include "app/KeybindsDialog.h"
#include "app/resource.h"
#include <algorithm>
#include <chrono>
#include <commctrl.h>
#include <commdlg.h>
#include <optional>

namespace taiko {
namespace {
constexpr wchar_t Title[] = L"Keybinds";
struct Editor {
    Controller* app{};
    KeyConfig live, draft;
    bool dirty{};
    HWND dialog{}, sounds{}, keys{};
};
struct Capture {
    Controller* app{};
    const KeyConfig* draft{};
    uint16_t original{}, key{};
    uint32_t sound{};
};

// what() may hold ANSI text from the C++ runtime, not UTF-8.
std::wstring message(const char* text) {
    try {
        return wide(text);
    } catch (...) {
        int n = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
        std::wstring result(n > 0 ? n : 1, 0);
        MultiByteToWideChar(CP_ACP, 0, text, -1, result.data(), n);
        result.resize(result.size() - 1);
        return result;
    }
}
const SoundSpec* findSound(const KeyConfig& c, uint32_t id) {
    for (const auto& s : c.sounds)
        if (s.id == id)
            return &s;
    return nullptr;
}
std::wstring soundLabel(const KeyConfig& c, uint32_t id) {
    auto* s = findSound(c, id);
    return std::to_wstring(id) + L": " + (s ? s->path.filename().wstring() : L"(없음)");
}
std::wstring soundPath(const KeyConfig& c, const std::filesystem::path& path) {
    auto base = c.file.parent_path();
    auto relative = path.lexically_relative(base);
    if (!base.empty() && !relative.empty() && *relative.begin() != L"..")
        return relative.wstring();
    return path.wstring();
}
// Localized key name from the keyboard layout, with the KeyBind.ini name when it differs.
std::wstring keyLabel(uint16_t vk) {
    auto name = wide(keyName(vk));
    wchar_t text[64]{};
    if (UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX)) {
        LONG code = LONG(scan & 0xff) << 16;
        if (scan & 0xff00)
            code |= 1 << 24; // E0/E1 extended key
        GetKeyNameTextW(code, text, 64);
    }
    std::wstring friendly = text;
    if (friendly.empty())
        return name.empty() ? L"키 코드 " + std::to_wstring(vk) : name;
    if (name.empty() || friendly == name)
        return friendly;
    return friendly + L"  (" + name + L")";
}

struct Label {
    int id;
    const wchar_t* text;
};
void labels(HWND d, const wchar_t* caption, std::initializer_list<Label> items) {
    SetWindowTextW(d, caption);
    for (const auto& item : items)
        SetDlgItemTextW(d, item.id, item.text);
}
LPARAM selected(HWND list) {
    int index = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (index < 0)
        return -1;
    LVITEMW item{};
    item.mask = LVIF_PARAM;
    item.iItem = index;
    ListView_GetItem(list, &item);
    return item.lParam;
}
void select(HWND list, LPARAM param) {
    LVFINDINFOW find{};
    find.flags = LVFI_PARAM;
    find.lParam = param;
    int index = param < 0 ? -1 : ListView_FindItem(list, -1, &find);
    if (index < 0)
        return;
    ListView_SetItemState(list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list, index, FALSE);
}
void addRow(HWND list, LPARAM param, const std::wstring& first, const std::wstring& second) {
    LVITEMW item{};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = ListView_GetItemCount(list);
    item.lParam = param;
    item.pszText = const_cast<LPWSTR>(first.c_str());
    int index = ListView_InsertItem(list, &item);
    ListView_SetItemText(list, index, 1, const_cast<LPWSTR>(second.c_str()));
}
void columns(HWND dialog, HWND list, const wchar_t* first, int firstDlu, const wchar_t* second) {
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    RECT width{0, 0, firstDlu, 0}, client{};
    MapDialogRect(dialog, &width);
    GetClientRect(list, &client);
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH;
    column.pszText = const_cast<LPWSTR>(first);
    column.cx = width.right;
    ListView_InsertColumn(list, 0, &column);
    column.pszText = const_cast<LPWSTR>(second);
    column.cx = std::max<int>(40, client.right - width.right - GetSystemMetrics(SM_CXVSCROLL));
    ListView_InsertColumn(list, 1, &column);
}

void updateButtons(Editor& e) {
    auto sound = selected(e.sounds), key = selected(e.keys);
    auto enable = [&](int id, bool on) { EnableWindow(GetDlgItem(e.dialog, id), on); };
    enable(IDC_SOUND_REPLACE, sound >= 0);
    enable(IDC_SOUND_REMOVE, sound >= 0);
    // Preview goes through the live audio stream, so only saved, unchanged sounds can play.
    bool playable = false;
    if (sound >= 0)
        if (auto *draft = findSound(e.draft, uint32_t(sound)), *live = findSound(e.live, uint32_t(sound));
            draft && live)
            playable =
                draft->path.lexically_normal() == live->path.lexically_normal() && e.app->snapshot().running;
    enable(IDC_SOUND_PLAY, playable);
    enable(IDC_KEY_ADD, !e.draft.sounds.empty());
    enable(IDC_KEY_EDIT, key >= 0);
    enable(IDC_KEY_REMOVE, key >= 0);
    enable(IDC_APPLY, e.dirty);
}
void fill(Editor& e, LPARAM sound, LPARAM key) {
    std::sort(e.draft.sounds.begin(), e.draft.sounds.end(), [](auto& a, auto& b) { return a.id < b.id; });
    ListView_DeleteAllItems(e.sounds);
    for (const auto& s : e.draft.sounds)
        addRow(e.sounds, s.id, std::to_wstring(s.id), soundPath(e.draft, s.path));
    ListView_DeleteAllItems(e.keys);
    for (size_t k = 0; k < e.draft.keys.size(); ++k)
        if (e.draft.keys[k])
            addRow(e.keys, LPARAM(k), keyLabel(uint16_t(k)), soundLabel(e.draft, e.draft.keys[k]));
    select(e.sounds, sound);
    select(e.keys, key);
    updateButtons(e);
}
void changed(Editor& e, LPARAM sound, LPARAM key) {
    e.dirty = true;
    fill(e, sound, key);
}

std::optional<std::filesystem::path> chooseWav(HWND owner, const std::filesystem::path& folder) {
    wchar_t file[32768]{};
    auto initial = folder.wstring();
    OPENFILENAMEW open{};
    open.lStructSize = sizeof(open);
    open.hwndOwner = owner;
    open.lpstrFilter = L"WAV 파일 (*.wav)\0*.wav\0모든 파일\0*.*\0";
    open.lpstrFile = file;
    open.nMaxFile = 32768;
    open.lpstrInitialDir = std::filesystem::is_directory(folder) ? initial.c_str() : nullptr;
    open.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&open))
        return std::nullopt;
    return std::filesystem::path(file).lexically_normal();
}
void pickSound(Editor& e, bool replace) {
    auto current = selected(e.sounds);
    if (replace && current < 0)
        return;
    auto path = chooseWav(e.dialog, e.app->root() / L"HitSounds");
    if (!path)
        return;
    try {
        decodeSound(1, *path);
    } catch (const std::exception& ex) {
        auto text =
            L"이 WAV 파일은 쓸 수 없습니다.\n\n" + path->filename().wstring() + L"\n" + message(ex.what());
        MessageBoxW(e.dialog, text.c_str(), Title, MB_OK | MB_ICONWARNING);
        return;
    }
    if (replace) {
        for (auto& s : e.draft.sounds)
            if (s.id == uint32_t(current))
                s.path = *path;
        changed(e, current, selected(e.keys));
        return;
    }
    uint32_t id = 1;
    for (const auto& s : e.draft.sounds)
        id = std::max(id, s.id + 1);
    if (id > 65535) {
        MessageBoxW(e.dialog, L"소리 번호가 65535를 넘었습니다.", Title, MB_OK | MB_ICONWARNING);
        return;
    }
    e.draft.sounds.push_back({id, *path, 0});
    changed(e, id, selected(e.keys));
}
void removeSound(Editor& e) {
    auto id = selected(e.sounds);
    if (id < 0)
        return;
    auto users = std::count(e.draft.keys.begin(), e.draft.keys.end(), uint32_t(id));
    if (users) {
        auto text = soundLabel(e.draft, uint32_t(id)) + L"을(를) 쓰는 키 " + std::to_wstring(users) +
                    L"개의 할당도 함께 지워집니다. 계속할까요?";
        if (MessageBoxW(e.dialog, text.c_str(), Title, MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
    }
    std::erase_if(e.draft.sounds, [&](const SoundSpec& s) { return s.id == uint32_t(id); });
    std::replace(e.draft.keys.begin(), e.draft.keys.end(), uint32_t(id), 0u);
    changed(e, -1, -1);
}

LRESULT CALLBACK captureBox(HWND w, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTTAB | DLGC_WANTCHARS;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
        // Swallow keys so Enter/Esc/Alt do not act on the dialog. The key itself is read from
        // Raw Input, which is exactly what playback matches against.
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(w);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(w, nullptr, TRUE);
        return 0;
    case WM_SETFONT:
        SetWindowLongPtrW(w, GWLP_USERDATA, LONG_PTR(wp));
        if (LOWORD(lp))
            InvalidateRect(w, nullptr, TRUE);
        return 0;
    case WM_GETFONT:
        return GetWindowLongPtrW(w, GWLP_USERDATA);
    case WM_SETTEXT: {
        auto result = DefWindowProcW(w, message, wp, lp);
        InvalidateRect(w, nullptr, TRUE);
        return result;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT rc;
        GetClientRect(w, &rc);
        FillRect(dc, &rc, GetSysColorBrush(COLOR_WINDOW));
        auto font = reinterpret_cast<HFONT>(GetWindowLongPtrW(w, GWLP_USERDATA));
        auto old = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        wchar_t text[128]{};
        GetWindowTextW(w, text, 128);
        DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (GetFocus() == w) {
            InflateRect(&rc, -2, -2);
            DrawFocusRect(dc, &rc);
        }
        SelectObject(dc, old);
        EndPaint(w, &ps);
        return 0;
    }
    }
    return DefWindowProcW(w, message, wp, lp);
}
void refreshCapture(HWND d, Capture& c) {
    SetDlgItemTextW(d, IDC_CAPTURE, c.key ? keyLabel(c.key).c_str() : L"키를 누르세요");
    bool named = c.key && !keyName(c.key).empty();
    std::wstring note;
    if (!c.key)
        note = L"이 칸을 클릭한 상태에서 쓸 키를 한 번 누르세요.";
    else if (!named)
        note = L"이 키는 지원하지 않습니다. 다른 키를 누르세요.";
    else if (c.key != c.original && c.draft->keys[c.key])
        note = L"지금은 " + soundLabel(*c.draft, c.draft->keys[c.key]) +
               L"에 할당되어 있습니다. 확인하면 바뀝니다.";
    SetDlgItemTextW(d, IDC_CAPTURE_NOTE, note.c_str());
    EnableWindow(GetDlgItem(d, IDOK), named);
}
INT_PTR CALLBACK captureDialog(HWND d, UINT message, WPARAM wp, LPARAM lp) {
    auto* c = reinterpret_cast<Capture*>(GetWindowLongPtrW(d, DWLP_USER));
    switch (message) {
    case WM_INITDIALOG: {
        c = reinterpret_cast<Capture*>(lp);
        SetWindowLongPtrW(d, DWLP_USER, lp);
        labels(d, L"키 할당",
               {{IDC_CAPTURE_KEY_LABEL, L"누를 키:"},
                {IDC_CAPTURE_SOUND_LABEL, L"소리:"},
                {IDOK, L"확인"},
                {IDCANCEL, L"취소"}});
        HWND combo = GetDlgItem(d, IDC_CAPTURE_SOUND);
        for (const auto& s : c->draft->sounds) {
            auto label = soundLabel(*c->draft, s.id);
            auto index = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            SendMessageW(combo, CB_SETITEMDATA, index, s.id);
            if (s.id == c->sound)
                SendMessageW(combo, CB_SETCURSEL, index, 0);
        }
        if (SendMessageW(combo, CB_GETCURSEL, 0, 0) < 0)
            SendMessageW(combo, CB_SETCURSEL, 0, 0);
        c->app->captureKeys(d);
        refreshCapture(d, *c);
        SetFocus(GetDlgItem(d, IDC_CAPTURE));
        return FALSE;
    }
    case KeyCapturedMessage:
        // Raw Input reports keys system-wide; take them only while the capture box has focus here.
        if (c && GetForegroundWindow() == d && GetFocus() == GetDlgItem(d, IDC_CAPTURE)) {
            c->key = static_cast<uint16_t>(wp);
            refreshCapture(d, *c);
        }
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            HWND combo = GetDlgItem(d, IDC_CAPTURE_SOUND);
            auto index = SendMessageW(combo, CB_GETCURSEL, 0, 0);
            if (index < 0 || !c->key || keyName(c->key).empty())
                return TRUE;
            c->sound = static_cast<uint32_t>(SendMessageW(combo, CB_GETITEMDATA, index, 0));
            EndDialog(d, IDOK);
        }
        if (LOWORD(wp) == IDCANCEL)
            EndDialog(d, IDCANCEL);
        return TRUE;
    case WM_DESTROY:
        if (c)
            c->app->captureKeys(nullptr);
        return FALSE;
    }
    return FALSE;
}
void editKey(Editor& e, bool add) {
    auto current = add ? -1 : selected(e.keys);
    if ((!add && current < 0) || e.draft.sounds.empty())
        return;
    Capture c{e.app, &e.draft};
    if (!add) {
        c.original = c.key = uint16_t(current);
        c.sound = e.draft.keys[c.key];
    } else {
        auto sound = selected(e.sounds);
        c.sound = sound >= 0 ? uint32_t(sound) : e.draft.sounds.front().id;
    }
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_KEYCAPTURE), e.dialog, captureDialog,
                        reinterpret_cast<LPARAM>(&c)) != IDOK)
        return;
    if (c.original && c.original != c.key)
        e.draft.keys[c.original] = 0;
    e.draft.keys[c.key] = c.sound;
    changed(e, selected(e.sounds), c.key);
}
void removeKey(Editor& e) {
    auto key = selected(e.keys);
    if (key < 0)
        return;
    e.draft.keys[size_t(key)] = 0;
    changed(e, selected(e.sounds), -1);
}

bool apply(Editor& e) {
    if (e.draft.sounds.empty()) {
        MessageBoxW(e.dialog, L"소리를 하나 이상 추가하세요.", Title, MB_OK | MB_ICONWARNING);
        return false;
    }
    try {
        parseConfig(configText(e.draft), e.draft.file);
    } catch (const std::exception& ex) {
        auto text = L"키 설정이 올바르지 않습니다.\n\n" + message(ex.what());
        MessageBoxW(e.dialog, text.c_str(), Title, MB_OK | MB_ICONWARNING);
        return false;
    }
    // Saving restarts the audio stream; it takes well under a second, so wait for the result here.
    auto cursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    auto result = e.app->saveBindings(e.draft);
    bool ready = result.wait_for(std::chrono::seconds(30)) == std::future_status::ready;
    SetCursor(cursor);
    if (!ready) {
        MessageBoxW(e.dialog, L"저장 결과를 받지 못했습니다. 잠시 후 다시 시도하세요.", Title,
                    MB_OK | MB_ICONWARNING);
        return false;
    }
    if (auto error = result.get(); !error.empty()) {
        auto text = L"키 설정을 저장하지 못했습니다.\n\n" + message(error.c_str());
        MessageBoxW(e.dialog, text.c_str(), Title, MB_OK | MB_ICONWARNING);
        return false;
    }
    // Reload what was saved: imported sounds now point at their copies beside the EXE.
    e.live = e.draft = e.app->snapshot().config;
    e.dirty = false;
    fill(e, selected(e.sounds), selected(e.keys));
    return true;
}
INT_PTR CALLBACK editorDialog(HWND d, UINT message, WPARAM wp, LPARAM lp) {
    auto* e = reinterpret_cast<Editor*>(GetWindowLongPtrW(d, DWLP_USER));
    switch (message) {
    case WM_INITDIALOG:
        e = reinterpret_cast<Editor*>(lp);
        SetWindowLongPtrW(d, DWLP_USER, lp);
        e->dialog = d;
        e->sounds = GetDlgItem(d, IDC_SOUNDS);
        e->keys = GetDlgItem(d, IDC_KEYS);
        labels(d, Title,
               {{IDC_SOUND_GROUP, L"소리"},
                {IDC_SOUND_ADD, L"추가..."},
                {IDC_SOUND_REPLACE, L"바꾸기..."},
                {IDC_SOUND_REMOVE, L"삭제"},
                {IDC_SOUND_PLAY, L"듣기"},
                {IDC_KEY_GROUP, L"키"},
                {IDC_KEY_ADD, L"추가..."},
                {IDC_KEY_EDIT, L"변경..."},
                {IDC_KEY_REMOVE, L"삭제"},
                {IDOK, L"확인"},
                {IDCANCEL, L"취소"},
                {IDC_APPLY, L"적용"}});
        columns(d, e->sounds, L"번호", 26, L"파일");
        columns(d, e->keys, L"키", 90, L"소리");
        fill(*e, -1, -1);
        return TRUE;
    case WM_NOTIFY: {
        if (!e)
            return FALSE;
        auto* header = reinterpret_cast<NMHDR*>(lp);
        bool keys = header->hwndFrom == e->keys;
        if (header->hwndFrom != e->sounds && !keys)
            return FALSE;
        if (header->code == LVN_ITEMCHANGED)
            updateButtons(*e);
        if (header->code == NM_DBLCLK) {
            if (keys)
                editKey(*e, false);
            else if (IsWindowEnabled(GetDlgItem(d, IDC_SOUND_PLAY)))
                e->app->preview(uint32_t(selected(e->sounds)));
        }
        if (header->code == LVN_KEYDOWN && reinterpret_cast<NMLVKEYDOWN*>(lp)->wVKey == VK_DELETE) {
            if (keys)
                removeKey(*e);
            else
                removeSound(*e);
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (!e)
            return FALSE;
        switch (LOWORD(wp)) {
        case IDC_SOUND_ADD:
            pickSound(*e, false);
            break;
        case IDC_SOUND_REPLACE:
            pickSound(*e, true);
            break;
        case IDC_SOUND_REMOVE:
            removeSound(*e);
            break;
        case IDC_SOUND_PLAY:
            if (auto id = selected(e->sounds); id >= 0)
                e->app->preview(uint32_t(id));
            break;
        case IDC_KEY_ADD:
            editKey(*e, true);
            break;
        case IDC_KEY_EDIT:
            editKey(*e, false);
            break;
        case IDC_KEY_REMOVE:
            removeKey(*e);
            break;
        case IDC_APPLY:
            apply(*e);
            break;
        case IDOK:
            if (!e->dirty || apply(*e))
                EndDialog(d, IDOK);
            break;
        case IDCANCEL:
            EndDialog(d, IDCANCEL);
            break;
        }
        return TRUE;
    }
    return FALSE;
}
} // namespace

void showKeybinds(HWND owner, Controller& app) {
    static bool registered = [] {
        WNDCLASSW cls{};
        cls.lpfnWndProc = captureBox;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.lpszClassName = L"TaikoKeyCaptureBox";
        return RegisterClassW(&cls) != 0;
    }();
    (void)registered;
    auto snapshot = app.snapshot();
    Editor e;
    e.app = &app;
    e.live = snapshot.config;
    e.live.file = app.root() / L"KeyBind.ini";
    e.draft = e.live;
    if (e.live.sounds.empty() && std::filesystem::exists(e.live.file)) {
        auto text =
            L"현재 KeyBind.ini를 읽지 못해 빈 목록으로 시작합니다. 저장하면 이 파일을 새로 씁니다.\n\n" +
            snapshot.error;
        MessageBoxW(owner, text.c_str(), Title, MB_OK | MB_ICONWARNING);
    }
    DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_KEYBINDS), owner, editorDialog,
                    reinterpret_cast<LPARAM>(&e));
}
} // namespace taiko
