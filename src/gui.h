// gui.h - Win32 front end
#pragma once
#include "common.h"

// Opens the GUI. An optional file is preloaded into the input box and
// autoStart kicks the conversion off immediately (used by "-gui <file> --auto").
int runGui(const std::wstring& initialFile = std::wstring(), bool autoStart = false);

// Shows a modal message box owned by the GUI (or a plain one when headless).
void guiMessage(const std::wstring& text, const std::wstring& caption, bool isError);
