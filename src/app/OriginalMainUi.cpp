// Directly extracted UI blocks from Taiko-Key-ASIO/TaikoKeyASIO_main.cpp.
// Commit: c3ed9a669e3369dd149171fba8fc06e84589d316. See THIRD_PARTY_NOTICES.md.
// Changes: extracted functions, control ID mapping, Driver -> Device, resource ID.
// Input/audio are connected by Main.cpp to the existing WASAPI controller.
#include "app/OriginalMainUi.h"
#include <commctrl.h>
#include <tchar.h>
#define IDB_BITMAP_FMODASIO 101
namespace original_ui {
HWND hCombo, hTrackbarVolume, hVolumeDesc;
static HWND hComboDesc;
[[maybe_unused]] static HWND hButtonKeyLoad;
static HBITMAP bitmapFmodasio, oldbit;
static HBRUSH asioBrush;
static HFONT fontSize20, fontSize16;
void setClientRect(HWND hWnd, int width, int height)
{
    RECT crt;
    DWORD Style, ExStyle;

    SetRect(&crt, 0, 0, width, height);
    Style = (DWORD)GetWindowLongPtr(hWnd, GWL_STYLE);
    ExStyle = (DWORD)GetWindowLongPtr(hWnd, GWL_EXSTYLE);
    AdjustWindowRectEx(&crt, Style, GetMenu(hWnd) != NULL, ExStyle);
    if (Style & WS_VSCROLL)crt.right += GetSystemMetrics(SM_CXVSCROLL);
    if (Style & WS_HSCROLL)crt.bottom += GetSystemMetrics(SM_CYVSCROLL);
    SetWindowPos(hWnd, NULL, 0, 0, crt.right - crt.left, crt.bottom - crt.top,
        SWP_NOMOVE | SWP_NOZORDER);
}
void create(HWND hWnd, HINSTANCE hInst) {
        asioBrush = CreateSolidBrush(RGB(6, 6, 6));

        //Init Ctrls
        hCombo = CreateWindow(_T("combobox"), NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL| CBS_DROPDOWNLIST ,
            70, DriverComboY, 300, 200, hWnd, (HMENU)HwndID::COMBO_DRIVER, hInst, NULL);
        hComboDesc = CreateWindow(_T("static"), _T("Device:"), WS_CHILD | WS_VISIBLE | SS_CENTER,
            10, DriverComboY, 60, 24, hWnd, (HMENU)HwndID::STATIC_COMBO_DRIVER, hInst, NULL);
        hButtonKeyLoad = CreateWindow(_T("button"), _T("Reload ini File"), WS_CHILD | WS_VISIBLE | BS_CENTER,
            10, ReloadButtonY, 110, 30, hWnd, (HMENU)HwndID::BUTTON_RELOAD_INI, hInst, NULL);
        hTrackbarVolume = CreateWindow(TRACKBAR_CLASS, _T("Volume"), WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS | TBS_BOTH | TBS_NOTICKS | WS_TABSTOP,
            240, ReloadButtonY, 150, 30, hWnd, (HMENU)HwndID::TRACKBAR_VOLUME, hInst, NULL);
        hVolumeDesc = CreateWindow(_T("static"), _T("Volume: 100"), WS_CHILD | WS_VISIBLE | SS_LEFT,
            130, ReloadButtonY+4, 100, 24, hWnd, (HMENU)HwndID::STATIC_VOLUME, hInst, NULL);

        SendMessage(hTrackbarVolume, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));
        SendMessage(hTrackbarVolume, TBM_SETPOS, TRUE, 100);

        //font
        HDC dc = GetDC(hWnd);
        LOGFONT lf;
        HFONT oldFont = (HFONT)GetCurrentObject(dc, OBJ_FONT);
        GetObject(oldFont, sizeof(LOGFONT), &lf);
        lf.lfHeight = 20;
        lf.lfWidth = 8;
        lf.lfWeight = FW_BOLD;
        _stprintf_s(lf.lfFaceName, _T("sans"));
        fontSize20 = CreateFontIndirect(&lf);
        SendMessage(hComboDesc, WM_SETFONT, (WPARAM)fontSize20, MAKELPARAM(TRUE, 0));
        lf.lfHeight = 16;
        lf.lfWidth = 7;
        fontSize16 = CreateFontIndirect(&lf);
        SendMessage(hVolumeDesc, WM_SETFONT, (WPARAM)fontSize16, MAKELPARAM(TRUE, 0));
        ReleaseDC(hWnd, dc);


}
LRESULT color(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if ((HWND)lParam == hComboDesc)
        {
            SetTextColor((HDC)wParam, RGB(255, 255, 255));
            SetBkMode((HDC)wParam, TRANSPARENT);
            return (INT_PTR)(HBRUSH)GetStockObject(NULL_BRUSH);
        }
        else if ((HWND)lParam == hTrackbarVolume || (HWND)lParam == hVolumeDesc)
        {
            SetTextColor((HDC)wParam, RGB(255, 255, 255));
            SetBkMode((HDC)wParam, TRANSPARENT);
            return (INT_PTR)asioBrush;
        }
        else return DefWindowProc(hWnd, message, wParam, lParam);

}
void paint(HWND hWnd, HINSTANCE hInst) {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: 여기에 hdc를 사용하는 그리기 코드를 추가합니다...
            bitmapFmodasio = LoadBitmap(hInst, MAKEINTRESOURCE(IDB_BITMAP_FMODASIO));
            HDC hMemDC = CreateCompatibleDC(hdc);
            oldbit = (HBITMAP)SelectObject(hMemDC, bitmapFmodasio);
            BitBlt(hdc, 0, 0, 400, 400, hMemDC, 0, 0, SRCCOPY);
            SelectObject(hdc, oldbit);
            DeleteObject(bitmapFmodasio);
            DeleteDC(hMemDC);
            EndPaint(hWnd, &ps);
}
void release() {
    DeleteObject(asioBrush);
    DeleteObject(fontSize20);
    DeleteObject(fontSize16);
}
} // namespace original_ui
