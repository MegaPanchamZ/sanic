/**
 * IconsMaterialDesign.h
 * 
 * Material Design Icons font for the Sanic Engine Editor.
 * https://github.com/Templarian/MaterialDesign-Font
 * https://pictogrammers.com/library/mdi/
 * 
 * License: Apache 2.0
 * 
 * NOTE: The Desktop version of MDI uses codepoints in the range U+F0000-U+F2000
 * which requires IMGUI_USE_WCHAR32 to be defined. Until that's enabled,
 * icons won't display. Consider using FontAwesome or another icon font
 * that fits in the Basic Multilingual Plane (U+0000-U+FFFF).
 */

#pragma once

// Disabled until IMGUI_USE_WCHAR32 is enabled
// #define SANIC_FONT_MDI

#ifdef SANIC_FONT_MDI
// Include the generated compressed font data
#include "MaterialDesignIcons_data.h"

// Icon codepoint ranges for Material Design Icons
// The font uses Unicode Private Use Area starting at U+F0000
#define ICON_MIN_MDI 0xF0000
#define ICON_MAX_MDI 0xF1FFF
#endif

// For now, use simple Unicode symbols that work with the default font
// These are from the standard Unicode range and will display correctly
namespace Icons {
    // File operations (using standard Unicode)
    constexpr const char* File = "\xF0\x9F\x93\x84";           // 📄
    constexpr const char* Folder = "\xF0\x9F\x93\x81";         // 📁
    constexpr const char* FolderOpen = "\xF0\x9F\x93\x82";     // 📂
    constexpr const char* Save = "\xF0\x9F\x92\xBE";           // 💾
    
    // Playback (ASCII art style for reliability)
    constexpr const char* Play = "\xE2\x96\xB6";               // ▶
    constexpr const char* Pause = "\xE2\x8F\xB8";              // ⏸
    constexpr const char* Stop = "\xE2\x96\xA0";               // ■
    constexpr const char* StepForward = "\xE2\x8F\xAD";        // ⏭
    
    // Edit operations  
    constexpr const char* Undo = "\xE2\x86\xB6";               // ↶
    constexpr const char* Redo = "\xE2\x86\xB7";               // ↷
    constexpr const char* Cut = "\xE2\x9C\x82";                // ✂
    constexpr const char* Copy = "\xE2\x8E\x98";               // ⎘
    constexpr const char* Delete = "\xF0\x9F\x97\x91";         // 🗑
    
    // View
    constexpr const char* Eye = "\xF0\x9F\x91\x81";            // 👁
    constexpr const char* Search = "\xF0\x9F\x94\x8D";         // 🔍
    constexpr const char* Settings = "\xE2\x9A\x99";           // ⚙
    
    // Transform
    constexpr const char* Move = "\xE2\x86\x94";               // ↔
    constexpr const char* Rotate = "\xE2\x86\xBB";             // ↻
    constexpr const char* Scale = "\xE2\x87\xB2";              // ⇲
    
    // Objects
    constexpr const char* Cube = "\xE2\x97\x86";               // ◆
    constexpr const char* CubeOutline = "\xE2\x97\x87";        // ◇
    
    // Notifications
    constexpr const char* Info = "\xE2\x84\xB9";               // ℹ
    constexpr const char* Warning = "\xE2\x9A\xA0";            // ⚠
    constexpr const char* Error = "\xE2\x9C\x96";              // ✖
    constexpr const char* Success = "\xE2\x9C\x93";            // ✓
    
    // UI
    constexpr const char* Plus = "+";
    constexpr const char* Minus = "-";
    constexpr const char* Close = "\xC3\x97";                  // ×
    constexpr const char* Check = "\xE2\x9C\x93";              // ✓
    constexpr const char* Menu = "\xE2\x98\xB0";               // ☰
    constexpr const char* ChevronRight = "\xE2\x80\xBA";       // ›
    constexpr const char* ChevronDown = "\xE2\x80\xBA";        // › (rotated in rendering)
    constexpr const char* ArrowRight = "\xE2\x86\x92";         // →
    constexpr const char* ArrowDown = "\xE2\x86\x93";          // ↓
}
