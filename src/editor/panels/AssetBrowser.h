/**
 * AssetBrowser.h
 * 
 * Asset browser panel for navigating and managing project assets.
 */

#pragma once

#include "../EditorWindow.h"
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>

namespace Sanic::Editor {

enum class AssetType {
    Unknown,
    Folder,
    Mesh,
    Texture,
    Material,
    Shader,
    Scene,
    Script,
    Audio,
    Font,
    Prefab
};

struct AssetEntry {
    std::string name;
    std::string path;
    std::string extension;
    AssetType type;
    bool isDirectory;
    uint64_t size;
    std::filesystem::file_time_type lastModified;
};

class AssetBrowser : public EditorWindow {
public:
    void initialize(Editor* editor) override;
    void update(float deltaTime) override;
    void draw() override;
    const char* getName() const override { return "Asset Browser"; }
    
    // Navigation
    void setRootPath(const std::string& path);
    void setCurrentPath(const std::string& path);
    const std::string& getCurrentPath() const { return currentPath_; }
    void navigateUp();
    void navigateTo(const std::string& path);
    void refresh();
    
    // Selection
    void selectAsset(const std::string& path);
    const std::string& getSelectedAsset() const { return selectedAsset_; }
    
private:
    void drawToolbar();
    void drawFolderTree();
    void drawFolderTreeNode(const std::filesystem::path& path);
    void drawContentArea();
    void drawAssetTile(const AssetEntry& entry);
    void drawAssetList(const AssetEntry& entry);
    void drawBreadcrumbs();
    
    void scanDirectory(const std::string& path);
    AssetType getAssetType(const std::string& extension);
    const char* getAssetIcon(AssetType type);
    void openAsset(const AssetEntry& entry);
    void importAsset(const std::string& path);
    
    Editor* editor_ = nullptr;
    
    std::string rootPath_;
    std::string currentPath_;
    std::string selectedAsset_;
    
    std::vector<AssetEntry> entries_;
    std::vector<std::string> pathHistory_;
    int historyIndex_ = -1;
    
    // View settings
    bool showTreeView_ = true;
    bool gridView_ = true;
    float thumbnailSize_ = 80.0f;
    char searchBuffer_[256] = "";
    
    // Type filters
    std::unordered_map<AssetType, bool> typeFilters_;
    
    // Drag and drop
    std::string draggedAsset_;
};

} // namespace Sanic::Editor
