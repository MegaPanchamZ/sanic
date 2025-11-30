/**
 * AssetBrowser.cpp
 * 
 * Asset browser panel implementation.
 */

#include "AssetBrowser.h"
#include "../Editor.h"
#include <imgui.h>
#include <algorithm>

namespace Sanic::Editor {

void AssetBrowser::initialize(Editor* editor) {
    editor_ = editor;
    
    // Initialize type filters
    typeFilters_[AssetType::Folder] = true;
    typeFilters_[AssetType::Mesh] = true;
    typeFilters_[AssetType::Texture] = true;
    typeFilters_[AssetType::Material] = true;
    typeFilters_[AssetType::Shader] = true;
    typeFilters_[AssetType::Scene] = true;
    typeFilters_[AssetType::Script] = true;
    typeFilters_[AssetType::Audio] = true;
    typeFilters_[AssetType::Prefab] = true;
    
    // Set root path to assets folder
    rootPath_ = "assets";
    currentPath_ = rootPath_;
    
    refresh();
}

void AssetBrowser::update(float deltaTime) {
    // Nothing to update
}

void AssetBrowser::draw() {
    drawToolbar();
    
    ImGui::Separator();
    
    // Split view: folder tree | content
    if (showTreeView_) {
        ImGui::Columns(2, "AssetBrowserColumns", true);
        
        // Folder tree
        ImGui::BeginChild("FolderTree", ImVec2(0, 0), true);
        drawFolderTree();
        ImGui::EndChild();
        
        ImGui::NextColumn();
    }
    
    // Content area
    ImGui::BeginChild("ContentArea", ImVec2(0, 0), true);
    drawBreadcrumbs();
    ImGui::Separator();
    drawContentArea();
    ImGui::EndChild();
    
    if (showTreeView_) {
        ImGui::Columns(1);
    }
}

void AssetBrowser::drawToolbar() {
    // Back/Forward buttons
    bool canGoBack = historyIndex_ > 0;
    bool canGoForward = historyIndex_ < static_cast<int>(pathHistory_.size()) - 1;
    
    if (ImGui::ArrowButton("##Back", ImGuiDir_Left) && canGoBack) {
        historyIndex_--;
        currentPath_ = pathHistory_[historyIndex_];
        scanDirectory(currentPath_);
    }
    ImGui::SameLine();
    
    if (ImGui::ArrowButton("##Forward", ImGuiDir_Right) && canGoForward) {
        historyIndex_++;
        currentPath_ = pathHistory_[historyIndex_];
        scanDirectory(currentPath_);
    }
    ImGui::SameLine();
    
    // Up button
    if (ImGui::Button("^") && currentPath_ != rootPath_) {
        navigateUp();
    }
    ImGui::SameLine();
    
    // Refresh
    if (ImGui::Button("Refresh")) {
        refresh();
    }
    ImGui::SameLine();
    
    ImGui::Separator();
    ImGui::SameLine();
    
    // View toggle
    ImGui::Checkbox("Tree", &showTreeView_);
    ImGui::SameLine();
    
    if (ImGui::Checkbox("Grid", &gridView_)) {
        // Toggle view mode
    }
    ImGui::SameLine();
    
    // Thumbnail size slider
    if (gridView_) {
        ImGui::PushItemWidth(100.0f);
        ImGui::SliderFloat("##ThumbSize", &thumbnailSize_, 40.0f, 160.0f, "%.0f px");
        ImGui::PopItemWidth();
        ImGui::SameLine();
    }
    
    ImGui::Separator();
    ImGui::SameLine();
    
    // Search
    ImGui::PushItemWidth(200.0f);
    ImGui::InputTextWithHint("##Search", "Search...", searchBuffer_, sizeof(searchBuffer_));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    
    // Import button
    if (ImGui::Button("Import")) {
        // TODO: Open file dialog
    }
}

void AssetBrowser::drawFolderTree() {
    if (!std::filesystem::exists(rootPath_)) {
        ImGui::TextDisabled("Assets folder not found");
        return;
    }
    
    drawFolderTreeNode(rootPath_);
}

void AssetBrowser::drawFolderTreeNode(const std::filesystem::path& path) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    
    // Check if this is the current path
    if (path.string() == currentPath_) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    
    // Check for subdirectories
    bool hasSubdirs = false;
    try {
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            if (entry.is_directory()) {
                hasSubdirs = true;
                break;
            }
        }
    } catch (...) {}
    
    if (!hasSubdirs) {
        flags |= ImGuiTreeNodeFlags_Leaf;
    }
    
    bool opened = ImGui::TreeNodeEx(path.filename().string().c_str(), flags);
    
    // Click to navigate
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        navigateTo(path.string());
    }
    
    if (opened) {
        try {
            std::vector<std::filesystem::path> subdirs;
            for (const auto& entry : std::filesystem::directory_iterator(path)) {
                if (entry.is_directory()) {
                    subdirs.push_back(entry.path());
                }
            }
            
            // Sort alphabetically
            std::sort(subdirs.begin(), subdirs.end());
            
            for (const auto& subdir : subdirs) {
                drawFolderTreeNode(subdir);
            }
        } catch (...) {}
        
        ImGui::TreePop();
    }
}

void AssetBrowser::drawBreadcrumbs() {
    std::filesystem::path path(currentPath_);
    std::vector<std::filesystem::path> parts;
    
    for (auto& part : path) {
        parts.push_back(part);
    }
    
    std::filesystem::path accumulated;
    for (size_t i = 0; i < parts.size(); ++i) {
        accumulated /= parts[i];
        
        if (i > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled(">");
            ImGui::SameLine();
        }
        
        if (ImGui::SmallButton(parts[i].string().c_str())) {
            navigateTo(accumulated.string());
        }
    }
}

void AssetBrowser::drawContentArea() {
    if (gridView_) {
        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columns = std::max(1, static_cast<int>(panelWidth / (thumbnailSize_ + 16.0f)));
        
        ImGui::Columns(columns, nullptr, false);
        
        for (const auto& entry : entries_) {
            // Apply search filter
            if (searchBuffer_[0] != '\0') {
                if (entry.name.find(searchBuffer_) == std::string::npos) {
                    continue;
                }
            }
            
            // Apply type filter
            if (!typeFilters_[entry.type]) {
                continue;
            }
            
            drawAssetTile(entry);
            ImGui::NextColumn();
        }
        
        ImGui::Columns(1);
    } else {
        // List view
        ImGui::BeginTable("AssetList", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable);
        
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Size");
        ImGui::TableSetupColumn("Modified");
        ImGui::TableHeadersRow();
        
        for (const auto& entry : entries_) {
            // Apply search filter
            if (searchBuffer_[0] != '\0') {
                if (entry.name.find(searchBuffer_) == std::string::npos) {
                    continue;
                }
            }
            
            // Apply type filter
            if (!typeFilters_[entry.type]) {
                continue;
            }
            
            drawAssetList(entry);
        }
        
        ImGui::EndTable();
    }
    
    // Empty folder message
    if (entries_.empty()) {
        ImGui::TextDisabled("This folder is empty");
    }
    
    // Context menu for empty area
    if (ImGui::BeginPopupContextWindow("ContentContextMenu")) {
        if (ImGui::MenuItem("New Folder")) {
            // TODO: Create folder
        }
        if (ImGui::MenuItem("Import Asset")) {
            // TODO: Open file dialog
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Refresh")) {
            refresh();
        }
        ImGui::EndPopup();
    }
}

void AssetBrowser::drawAssetTile(const AssetEntry& entry) {
    ImGui::PushID(entry.path.c_str());
    
    bool isSelected = (entry.path == selectedAsset_);
    
    ImGui::BeginGroup();
    
    // Thumbnail/icon
    ImVec2 thumbSize(thumbnailSize_, thumbnailSize_);
    
    // TODO: Actual texture thumbnails
    // For now, draw a colored rectangle based on type
    ImVec4 bgColor(0.2f, 0.2f, 0.2f, 1.0f);
    if (isSelected) {
        bgColor = ImVec4(0.3f, 0.4f, 0.6f, 1.0f);
    }
    
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        cursorPos,
        ImVec2(cursorPos.x + thumbSize.x, cursorPos.y + thumbSize.y),
        ImGui::ColorConvertFloat4ToU32(bgColor),
        4.0f
    );
    
    // Icon in center
    ImVec2 iconPos(
        cursorPos.x + thumbSize.x * 0.5f - 8.0f,
        cursorPos.y + thumbSize.y * 0.5f - 8.0f
    );
    ImGui::SetCursorScreenPos(iconPos);
    ImGui::Text("%s", getAssetIcon(entry.type));
    
    ImGui::SetCursorScreenPos(ImVec2(cursorPos.x, cursorPos.y + thumbSize.y + 4.0f));
    
    // Selectable area
    ImGui::InvisibleButton("##thumb", thumbSize);
    if (ImGui::IsItemClicked()) {
        selectAsset(entry.path);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        openAsset(entry);
    }
    
    // Drag source
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("ASSET_PATH", entry.path.c_str(), entry.path.size() + 1);
        ImGui::Text("%s", entry.name.c_str());
        ImGui::EndDragDropSource();
    }
    
    // Name label (truncated)
    float textWidth = ImGui::CalcTextSize(entry.name.c_str()).x;
    if (textWidth > thumbSize.x) {
        // Truncate with ellipsis
        std::string truncated = entry.name.substr(0, 10) + "...";
        ImGui::TextWrapped("%s", truncated.c_str());
    } else {
        ImGui::TextWrapped("%s", entry.name.c_str());
    }
    
    ImGui::EndGroup();
    
    // Context menu
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Open")) {
            openAsset(entry);
        }
        if (!entry.isDirectory) {
            if (ImGui::MenuItem("Rename")) {
                // TODO: Rename
            }
            if (ImGui::MenuItem("Delete")) {
                // TODO: Delete with confirmation
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Show in Explorer")) {
                // TODO: Open containing folder
            }
        }
        ImGui::EndPopup();
    }
    
    ImGui::PopID();
}

void AssetBrowser::drawAssetList(const AssetEntry& entry) {
    ImGui::TableNextRow();
    
    bool isSelected = (entry.path == selectedAsset_);
    
    // Name column
    ImGui::TableNextColumn();
    
    ImGuiSelectableFlags flags = ImGuiSelectableFlags_SpanAllColumns;
    if (ImGui::Selectable(entry.name.c_str(), isSelected, flags)) {
        selectAsset(entry.path);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        openAsset(entry);
    }
    
    // Type column
    ImGui::TableNextColumn();
    ImGui::Text("%s %s", getAssetIcon(entry.type), entry.extension.c_str());
    
    // Size column
    ImGui::TableNextColumn();
    if (!entry.isDirectory) {
        if (entry.size < 1024) {
            ImGui::Text("%llu B", entry.size);
        } else if (entry.size < 1024 * 1024) {
            ImGui::Text("%.1f KB", entry.size / 1024.0f);
        } else {
            ImGui::Text("%.1f MB", entry.size / (1024.0f * 1024.0f));
        }
    }
    
    // Modified column
    ImGui::TableNextColumn();
    // TODO: Format date
    ImGui::TextDisabled("-");
}

void AssetBrowser::setCurrentPath(const std::string& path) {
    currentPath_ = path;
    scanDirectory(currentPath_);
    
    // Update history
    if (pathHistory_.empty() || pathHistory_.back() != path) {
        // Remove forward history
        if (historyIndex_ < static_cast<int>(pathHistory_.size()) - 1) {
            pathHistory_.erase(pathHistory_.begin() + historyIndex_ + 1, pathHistory_.end());
        }
        pathHistory_.push_back(path);
        historyIndex_ = static_cast<int>(pathHistory_.size()) - 1;
    }
}

void AssetBrowser::navigateUp() {
    std::filesystem::path path(currentPath_);
    if (path.has_parent_path() && path != rootPath_) {
        navigateTo(path.parent_path().string());
    }
}

void AssetBrowser::navigateTo(const std::string& path) {
    setCurrentPath(path);
}

void AssetBrowser::refresh() {
    scanDirectory(currentPath_);
}

void AssetBrowser::selectAsset(const std::string& path) {
    selectedAsset_ = path;
}

void AssetBrowser::scanDirectory(const std::string& path) {
    entries_.clear();
    
    if (!std::filesystem::exists(path)) {
        return;
    }
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            AssetEntry asset;
            asset.name = entry.path().filename().string();
            asset.path = entry.path().string();
            asset.isDirectory = entry.is_directory();
            
            if (asset.isDirectory) {
                asset.type = AssetType::Folder;
                asset.size = 0;
            } else {
                asset.extension = entry.path().extension().string();
                asset.type = getAssetType(asset.extension);
                asset.size = entry.file_size();
            }
            
            asset.lastModified = entry.last_write_time();
            
            entries_.push_back(asset);
        }
    } catch (const std::exception& e) {
        // Handle error
    }
    
    // Sort: folders first, then alphabetically
    std::sort(entries_.begin(), entries_.end(), [](const AssetEntry& a, const AssetEntry& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory > b.isDirectory;
        }
        return a.name < b.name;
    });
}

AssetType AssetBrowser::getAssetType(const std::string& extension) {
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
        return AssetType::Mesh;
    }
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr") {
        return AssetType::Texture;
    }
    if (ext == ".mat" || ext == ".material") {
        return AssetType::Material;
    }
    if (ext == ".vert" || ext == ".frag" || ext == ".comp" || ext == ".glsl" || ext == ".hlsl" || ext == ".spv") {
        return AssetType::Shader;
    }
    if (ext == ".scene" || ext == ".map") {
        return AssetType::Scene;
    }
    if (ext == ".cs" || ext == ".lua" || ext == ".py") {
        return AssetType::Script;
    }
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") {
        return AssetType::Audio;
    }
    if (ext == ".ttf" || ext == ".otf") {
        return AssetType::Font;
    }
    if (ext == ".prefab") {
        return AssetType::Prefab;
    }
    
    return AssetType::Unknown;
}

const char* AssetBrowser::getAssetIcon(AssetType type) {
    switch (type) {
        case AssetType::Folder:   return "[D]";
        case AssetType::Mesh:     return "[M]";
        case AssetType::Texture:  return "[T]";
        case AssetType::Material: return "[*]";
        case AssetType::Shader:   return "[#]";
        case AssetType::Scene:    return "[S]";
        case AssetType::Script:   return "[>]";
        case AssetType::Audio:    return "[~]";
        case AssetType::Font:     return "[F]";
        case AssetType::Prefab:   return "[P]";
        default:                  return "[?]";
    }
}

void AssetBrowser::openAsset(const AssetEntry& entry) {
    if (entry.isDirectory) {
        navigateTo(entry.path);
    } else {
        // TODO: Open asset in appropriate viewer/editor
        // For now, just select it
        selectAsset(entry.path);
    }
}

void AssetBrowser::importAsset(const std::string& path) {
    // TODO: Asset import pipeline
}

} // namespace Sanic::Editor
