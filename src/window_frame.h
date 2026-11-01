#pragma once

#include <functional>

namespace imclash::frame {

// Borderless Win32 window support. The window is created by EUI as a plain
// popup; this hook adds the resize frame, a custom title-bar drag band, keeps
// the window out of the taskbar and wires the tray icon.
void installBorderlessWindow(float titleBarLogicalHeight,
                             float leftControlsWidth,
                             float rightControlsWidth);

// Hides the window through EUI's tray integration (WM_CLOSE turns into
// "hide to tray" while the tray is available). The tray Show command and a
// double-click both restore the window.
void hideToTray();
void showMainWindow();

// Centers the main window on the primary monitor work area.
void centerWindow();

// Used by --autostart: keep the window off-screen until it is hidden to the
// tray, so startup has no visible window flash.
void prepareSilentStartup();

// Used by --autostart: hide once the tray icon has been created.
void scheduleStartupHide();

// Called on the UI thread roughly once per second.
void setTickHandler(std::function<void()> handler);

void setTitleBarDragEnabled(bool enabled);

} // namespace imclash::frame
