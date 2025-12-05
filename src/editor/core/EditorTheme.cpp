/**
 * EditorTheme.cpp
 * 
 * Implements the visual styling for the editor, mimicking Unreal Engine 5's dark theme.
 */

#include <imgui.h>

namespace Sanic::Editor {

void ApplyUnrealTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Rounding
    style.WindowRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 12.0f;
    style.TabRounding = 4.0f;
    
    // Padding
    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(10, 4);
    style.ItemSpacing = ImVec2(8, 4);
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 16.0f;
    
    // Borders
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    
    // Colors (UE5 Dark Theme approximation)
    ImVec4* colors = style.Colors;
    
    const ImVec4 bgDark       = ImVec4(0.06f, 0.06f, 0.06f, 1.00f); // Main background
    const ImVec4 bgPanel      = ImVec4(0.11f, 0.11f, 0.11f, 1.00f); // Panel background
    const ImVec4 bgInput      = ImVec4(0.02f, 0.02f, 0.02f, 1.00f); // Input field background
    const ImVec4 border       = ImVec4(0.00f, 0.00f, 0.00f, 0.50f); // Borders
    const ImVec4 accent       = ImVec4(0.00f, 0.44f, 0.88f, 1.00f); // UE Blue
    const ImVec4 accentHover  = ImVec4(0.10f, 0.50f, 0.95f, 1.00f);
    const ImVec4 accentActive = ImVec4(0.00f, 0.35f, 0.75f, 1.00f);
    const ImVec4 text         = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    const ImVec4 textDisabled = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    const ImVec4 header       = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    const ImVec4 headerHover  = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    const ImVec4 headerActive = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
    
    colors[ImGuiCol_Text]                   = text;
    colors[ImGuiCol_TextDisabled]           = textDisabled;
    colors[ImGuiCol_WindowBg]               = bgPanel;
    colors[ImGuiCol_ChildBg]                = bgPanel;
    colors[ImGuiCol_PopupBg]                = bgDark;
    colors[ImGuiCol_Border]                 = border;
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = bgInput;
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_TitleBg]                = bgDark;
    colors[ImGuiCol_TitleBgActive]          = bgDark;
    colors[ImGuiCol_TitleBgCollapsed]       = bgDark;
    colors[ImGuiCol_MenuBarBg]              = bgDark;
    colors[ImGuiCol_ScrollbarBg]            = bgInput;
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_CheckMark]              = accent;
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = accent;
    colors[ImGuiCol_Button]                 = header;
    colors[ImGuiCol_ButtonHovered]          = headerHover;
    colors[ImGuiCol_ButtonActive]           = headerActive;
    colors[ImGuiCol_Header]                 = header;
    colors[ImGuiCol_HeaderHovered]          = headerHover;
    colors[ImGuiCol_HeaderActive]           = headerActive;
    colors[ImGuiCol_Separator]              = border;
    colors[ImGuiCol_SeparatorHovered]       = accent;
    colors[ImGuiCol_SeparatorActive]        = accent;
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_Tab]                    = bgPanel;
    colors[ImGuiCol_TabHovered]             = headerHover;
    colors[ImGuiCol_TabActive]              = headerActive;
    colors[ImGuiCol_TabUnfocused]           = bgPanel;
    colors[ImGuiCol_TabUnfocusedActive]     = header;
    colors[ImGuiCol_DockingPreview]         = accent;
    colors[ImGuiCol_DockingEmptyBg]         = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_PlotLines]              = accent;
    colors[ImGuiCol_PlotLinesHovered]       = accentHover;
    colors[ImGuiCol_PlotHistogram]          = accent;
    colors[ImGuiCol_PlotHistogramHovered]   = accentHover;
    colors[ImGuiCol_TableHeaderBg]          = header;
    colors[ImGuiCol_TableBorderStrong]      = border;
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.00f, 0.00f, 0.00f, 0.30f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);
    colors[ImGuiCol_TextSelectedBg]         = accent;
    colors[ImGuiCol_DragDropTarget]         = accent;
    colors[ImGuiCol_NavHighlight]           = accent;
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);
}

} // namespace Sanic::Editor
