#pragma once
#include <windows.h>

// UI blocks copied from 4dblackhole/Taiko-Key-ASIO, commit c3ed9a6.
// Upstream source and attribution: THIRD_PARTY_NOTICES.md.
namespace original_ui {
enum class HwndID {
    COMBO_DRIVER = 100,
    STATIC_COMBO_DRIVER = 201,
    STATIC_VOLUME = 202,
    BUTTON_RELOAD_INI = 105,
    TRACKBAR_VOLUME = 104,
};
constexpr int DriverComboY = 10;
constexpr int ReloadButtonY = 165;
extern HWND hCombo, hTrackbarVolume, hVolumeDesc;
void create(HWND hWnd, HINSTANCE hInst);
void paint(HWND hWnd, HINSTANCE hInst);
LRESULT color(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void setClientRect(HWND hWnd, int width, int height);
void release();
} // namespace original_ui
