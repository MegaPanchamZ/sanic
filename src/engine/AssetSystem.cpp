// ============================================================================
// SANIC ENGINE - Asset System Implementation
// ============================================================================

#include "AssetSystem.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <random>

namespace Sanic {

// ============================================================================
// PROJECT DESCRIPTOR
// ============================================================================
bool ProjectDescriptor::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[Project] Failed to open: " << path << std::endl;
        return false;
    }
    
    try {
        nlohmann::json j;
        file >> j;
        
        name = j.value("name", "Untitled");
        description = j.value("description", "");
        engineVersion = j.value("engineVersion", "1.0");
        defaultWorld = j.value("defaultWorld", "");
        
        if (j.contains("modules")) {
            for (const auto& m : j["modules"]) {
                modules.push_back(m.get<std::string>());
            }
        }
        
        if (j.contains("plugins")) {
            for (const auto& p : j["plugins"]) {
                plugins.push_back(p.get<std::string>());
            }
        }
        
        if (j.contains("settings")) {
            for (auto& [key, val] : j["settings"].items()) {
                settings[key] = val.get<std::string>();
            }
        }
        
        std::cout << "[Project] Loaded: " << name << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[Project] Parse error: " << e.what() << std::endl;
        return false;
    }
}

bool ProjectDescriptor::saveToFile(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    
    nlohmann::json j;
    j["name"] = name;
    j["description"] = description;
    j["engineVersion"] = engineVersion;
    j["defaultWorld"] = defaultWorld;
    j["modules"] = modules;
    j["plugins"] = plugins;
    j["settings"] = settings;
    
    file << j.dump(2);
    return true;
}

// ============================================================================
// SANIC PATHS
// ============================================================================
SanicPaths& SanicPaths::get() {
    static SanicPaths instance;
    return instance;
}

void SanicPaths::initialize(const std::string& projectPath) {
    m_projectDir = normalize(projectPath);
    
    // Engine dir is either set explicitly or assumed relative to executable
    if (m_engineDir.empty()) {
        // Default: engine is parent of project
        m_engineDir = normalize(projectPath + "/..");
    }
    
    // Add default mount points
    m_mountPoints.clear();
    addMountPoint("/Engine", m_engineDir, 0);
    addMountPoint("/Content", m_projectDir + "/Content", 100);
    addMountPoint("/Config", m_projectDir + "/Config", 100);
    addMountPoint("/Shaders", m_projectDir + "/Shaders", 100);
    
    std::cout << "[Paths] Project: " << m_projectDir << std::endl;
    std::cout << "[Paths] Engine: " << m_engineDir << std::endl;
}

void SanicPaths::initializeEngineOnly(const std::string& enginePath) {
    m_engineDir = normalize(enginePath);
    m_projectDir.clear();
    
    m_mountPoints.clear();
    addMountPoint("/Engine", m_engineDir, 0);
    addMountPoint("/Content", m_engineDir + "/Content", 0);
    addMountPoint("/Shaders", m_engineDir + "/Shaders", 0);
}

std::string SanicPaths::normalize(const std::string& path) const {
    std::string result = path;
    
    // Convert backslashes to forward slashes
    std::replace(result.begin(), result.end(), '\\', '/');
    
    // Remove trailing slash
    while (!result.empty() && result.back() == '/') {
        result.pop_back();
    }
    
    // Resolve .. and . 
    std::filesystem::path p(result);
    if (std::filesystem::exists(p)) {
        result = std::filesystem::canonical(p).string();
        std::replace(result.begin(), result.end(), '\\', '/');
    }
    
    return result;
}

std::string SanicPaths::makeAbsolute(const std::string& relativePath) const {
    if (relativePath.empty()) return "";
    
    // Already absolute?
    if (relativePath[0] == '/' || (relativePath.length() > 1 && relativePath[1] == ':')) {
        return normalize(relativePath);
    }
    
    return normalize(m_projectDir + "/" + relativePath);
}

std::string SanicPaths::makeRelative(const std::string& absolutePath, const std::string& basePath) const {
    std::filesystem::path abs(absolutePath);
    std::filesystem::path base(basePath);
    return std::filesystem::relative(abs, base).string();
}

std::string SanicPaths::combine(const std::string& base, const std::string& path) const {
    if (path.empty()) return base;
    if (base.empty()) return path;
    
    std::string result = base;
    if (result.back() != '/' && path.front() != '/') {
        result += '/';
    }
    result += path;
    return normalize(result);
}

std::string SanicPaths::getExtension(const std::string& path) const {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return "";
    return path.substr(dot);
}

std::string SanicPaths::getFilename(const std::string& path) const {
    size_t slash = path.rfind('/');
    if (slash == std::string::npos) return path;
    return path.substr(slash + 1);
}

std::string SanicPaths::getDirectory(const std::string& path) const {
    size_t slash = path.rfind('/');
    if (slash == std::string::npos) return "";
    return path.substr(0, slash);
}

std::string SanicPaths::resolveVirtualPath(const std::string& virtualPath) const {
    if (virtualPath.empty() || virtualPath[0] != '/') {
        // Not a virtual path, treat as relative to project
        return makeAbsolute(virtualPath);
    }
    
    // Find best matching mount point (highest priority)
    const MountPoint* bestMatch = nullptr;
    size_t longestMatch = 0;
    
    for (const auto& mp : m_mountPoints) {
        if (virtualPath.rfind(mp.virtualRoot, 0) == 0) {
            if (mp.virtualRoot.length() > longestMatch) {
                longestMatch = mp.virtualRoot.length();
                bestMatch = &mp;
            }
        }
    }
    
    if (bestMatch) {
        std::string remainder = virtualPath.substr(bestMatch->virtualRoot.length());
        return normalize(bestMatch->diskPath + remainder);
    }
    
    // No mount point found, treat as relative to project
    return makeAbsolute(virtualPath.substr(1)); // Remove leading /
}

std::string SanicPaths::toVirtualPath(const std::string& diskPath) const {
    std::string normalized = normalize(diskPath);
    
    // Find best matching mount point (longest disk path match)
    const MountPoint* bestMatch = nullptr;
    size_t longestMatch = 0;
    
    for (const auto& mp : m_mountPoints) {
        std::string normalizedMount = normalize(mp.diskPath);
        if (normalized.rfind(normalizedMount, 0) == 0) {
            if (normalizedMount.length() > longestMatch) {
                longestMatch = normalizedMount.length();
                bestMatch = &mp;
            }
        }
    }
    
    if (bestMatch) {
        std::string remainder = normalized.substr(longestMatch);
        return bestMatch->virtualRoot + remainder;
    }
    
    return diskPath;
}

void SanicPaths::addMountPoint(const std::string& virtualRoot, const std::string& diskPath, int priority) {
    MountPoint mp{virtualRoot, normalize(diskPath), priority};
    
    // Insert sorted by priority (higher first)
    auto it = std::find_if(m_mountPoints.begin(), m_mountPoints.end(),
        [priority](const MountPoint& m) { return m.priority < priority; });
    
    m_mountPoints.insert(it, mp);
    
    std::cout << "[Paths] Mounted " << virtualRoot << " -> " << diskPath << " (priority " << priority << ")" << std::endl;
}

void SanicPaths::removeMountPoint(const std::string& virtualRoot) {
    m_mountPoints.erase(
        std::remove_if(m_mountPoints.begin(), m_mountPoints.end(),
            [&](const MountPoint& m) { return m.virtualRoot == virtualRoot; }),
        m_mountPoints.end()
    );
}

// ============================================================================
// ASSET REGISTRY
// ============================================================================
AssetRegistry& AssetRegistry::get() {
    static AssetRegistry instance;
    return instance;
}

void AssetRegistry::scanDirectory(const std::string& path, bool recursive) {
    std::filesystem::path dir(path);
    if (!std::filesystem::exists(dir)) {
        std::cerr << "[AssetRegistry] Directory not found: " << path << std::endl;
        return;
    }
    
    auto scanFunc = [this](const std::filesystem::path& p) {
        scanFile(p);
    };
    
    if (recursive) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                scanFunc(entry.path());
            }
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file()) {
                scanFunc(entry.path());
            }
        }
    }
    
    std::cout << "[AssetRegistry] Scanned " << path << " - " << m_assets.size() << " assets total" << std::endl;
}

void AssetRegistry::scanProjectContent() {
    auto& paths = SanicPaths::get();
    if (paths.hasProject()) {
        scanDirectory(paths.projectContentDir(), true);
    }
}

void AssetRegistry::scanEngineContent() {
    auto& paths = SanicPaths::get();
    scanDirectory(paths.engineContentDir(), true);
}

void AssetRegistry::scanFile(const std::filesystem::path& filePath) {
    std::string ext = filePath.extension().string();
    AssetType type = getAssetTypeFromExtension(ext);
    
    if (type == AssetType::Unknown) {
        return; // Not a recognized asset type
    }
    
    AssetMetadata meta;
    meta.diskPath = SanicPaths::get().normalize(filePath.string());
    meta.path = SanicPaths::get().toVirtualPath(meta.diskPath);
    meta.name = filePath.stem().string();
    meta.type = type;
    meta.fileSize = std::filesystem::file_size(filePath);
    
    auto ftime = std::filesystem::last_write_time(filePath);
    meta.lastModified = std::chrono::duration_cast<std::chrono::seconds>(
        ftime.time_since_epoch()).count();
    
    // Generate a simple GUID based on path hash (for now)
    std::hash<std::string> hasher;
    meta.guid[0] = hasher(meta.path);
    meta.guid[1] = hasher(meta.diskPath);
    
    m_assets[meta.path] = std::move(meta);
}

AssetType AssetRegistry::getAssetTypeFromExtension(const std::string& ext) const {
    static const std::unordered_map<std::string, AssetType> extMap = {
        {".sworld", AssetType::World},
        {".smesh", AssetType::Mesh},
        {".smat", AssetType::Material},
        {".stex", AssetType::Texture},
        {".saudio", AssetType::Audio},
        {".sbp", AssetType::Blueprint},
        {".sanim", AssetType::Animation},
        {".sphys", AssetType::Physics},
        {".sterrain", AssetType::Terrain},
        {".sspline", AssetType::Spline},
        {".sprefab", AssetType::Prefab},
        {".json", AssetType::Config},
        {".glsl", AssetType::Shader},
        {".spv", AssetType::Shader},
        // Also support common formats for import
        {".obj", AssetType::Mesh},
        {".fbx", AssetType::Mesh},
        {".gltf", AssetType::Mesh},
        {".glb", AssetType::Mesh},
        {".png", AssetType::Texture},
        {".jpg", AssetType::Texture},
        {".jpeg", AssetType::Texture},
        {".hdr", AssetType::Texture},
        {".wav", AssetType::Audio},
        {".ogg", AssetType::Audio},
        {".mp3", AssetType::Audio},
    };
    
    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    auto it = extMap.find(lower);
    return (it != extMap.end()) ? it->second : AssetType::Unknown;
}

const AssetMetadata* AssetRegistry::findAsset(const std::string& virtualPath) const {
    auto it = m_assets.find(virtualPath);
    return (it != m_assets.end()) ? &it->second : nullptr;
}

std::vector<const AssetMetadata*> AssetRegistry::findAssetsByType(AssetType type) const {
    std::vector<const AssetMetadata*> result;
    for (const auto& [path, meta] : m_assets) {
        if (meta.type == type) {
            result.push_back(&meta);
        }
    }
    return result;
}

std::vector<const AssetMetadata*> AssetRegistry::findAssetsByPath(const std::string& pathPrefix) const {
    std::vector<const AssetMetadata*> result;
    for (const auto& [path, meta] : m_assets) {
        if (path.rfind(pathPrefix, 0) == 0) {
            result.push_back(&meta);
        }
    }
    return result;
}

std::vector<const AssetMetadata*> AssetRegistry::findAssetsByTag(const std::string& key, const std::string& value) const {
    std::vector<const AssetMetadata*> result;
    for (const auto& [path, meta] : m_assets) {
        auto it = meta.tags.find(key);
        if (it != meta.tags.end() && it->second == value) {
            result.push_back(&meta);
        }
    }
    return result;
}

std::vector<const AssetMetadata*> AssetRegistry::getAllAssets() const {
    std::vector<const AssetMetadata*> result;
    for (const auto& [path, meta] : m_assets) {
        result.push_back(&meta);
    }
    return result;
}

void AssetRegistry::registerAsset(const AssetMetadata& metadata) {
    m_assets[metadata.path] = metadata;
}

void AssetRegistry::unregisterAsset(const std::string& virtualPath) {
    m_assets.erase(virtualPath);
}

std::vector<std::string> AssetRegistry::getDependencies(const std::string& virtualPath) const {
    auto it = m_assets.find(virtualPath);
    if (it != m_assets.end()) {
        return it->second.dependencies;
    }
    return {};
}

std::vector<std::string> AssetRegistry::getReferencers(const std::string& virtualPath) const {
    auto it = m_referencers.find(virtualPath);
    if (it != m_referencers.end()) {
        return it->second;
    }
    return {};
}

void AssetRegistry::saveRegistryCache(const std::string& path) const {
    nlohmann::json j;
    j["version"] = 1;
    j["assetCount"] = m_assets.size();
    
    nlohmann::json assets = nlohmann::json::array();
    for (const auto& [vpath, meta] : m_assets) {
        nlohmann::json a;
        a["path"] = meta.path;
        a["diskPath"] = meta.diskPath;
        a["name"] = meta.name;
        a["type"] = static_cast<int>(meta.type);
        a["fileSize"] = meta.fileSize;
        a["lastModified"] = meta.lastModified;
        a["dependencies"] = meta.dependencies;
        a["tags"] = meta.tags;
        assets.push_back(a);
    }
    j["assets"] = assets;
    
    std::ofstream file(path);
    if (file.is_open()) {
        file << j.dump(2);
        std::cout << "[AssetRegistry] Cache saved: " << path << std::endl;
    }
}

bool AssetRegistry::loadRegistryCache(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    
    try {
        nlohmann::json j;
        file >> j;
        
        m_assets.clear();
        for (const auto& a : j["assets"]) {
            AssetMetadata meta;
            meta.path = a["path"];
            meta.diskPath = a["diskPath"];
            meta.name = a["name"];
            meta.type = static_cast<AssetType>(a["type"].get<int>());
            meta.fileSize = a["fileSize"];
            meta.lastModified = a["lastModified"];
            
            if (a.contains("dependencies")) {
                for (const auto& d : a["dependencies"]) {
                    meta.dependencies.push_back(d);
                }
            }
            if (a.contains("tags")) {
                for (auto& [k, v] : a["tags"].items()) {
                    meta.tags[k] = v;
                }
            }
            
            m_assets[meta.path] = std::move(meta);
        }
        
        std::cout << "[AssetRegistry] Cache loaded: " << m_assets.size() << " assets" << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[AssetRegistry] Cache parse error: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// ASSET LOADER
// ============================================================================
AssetLoader& AssetLoader::get() {
    static AssetLoader instance;
    return instance;
}

std::shared_ptr<Asset> AssetLoader::loadGeneric(const std::string& virtualPath) {
    // Check cache first
    auto it = m_cache.find(virtualPath);
    if (it != m_cache.end()) {
        return it->second;
    }
    
    // Find in registry
    const AssetMetadata* meta = AssetRegistry::get().findAsset(virtualPath);
    if (!meta) {
        std::cerr << "[AssetLoader] Asset not found: " << virtualPath << std::endl;
        return nullptr;
    }
    
    // Load from disk
    auto asset = loadFromDisk(meta->diskPath, meta->type);
    if (asset) {
        m_cache[virtualPath] = asset;
    }
    return asset;
}

std::shared_ptr<Asset> AssetLoader::loadFromDisk(const std::string& diskPath, AssetType type) {
    std::shared_ptr<Asset> asset;
    
    switch (type) {
        case AssetType::World:
            asset = std::make_shared<WorldAsset>();
            break;
        case AssetType::Mesh:
            asset = std::make_shared<MeshAsset>();
            break;
        case AssetType::Material:
            asset = std::make_shared<MaterialAsset>();
            break;
        case AssetType::Terrain:
            asset = std::make_shared<TerrainAsset>();
            break;
        case AssetType::Spline:
            asset = std::make_shared<SplineAsset>();
            break;
        case AssetType::Prefab:
            asset = std::make_shared<PrefabAsset>();
            break;
        default:
            std::cerr << "[AssetLoader] Unsupported asset type" << std::endl;
            return nullptr;
    }
    
    if (!asset->load(diskPath)) {
        std::cerr << "[AssetLoader] Failed to load: " << diskPath << std::endl;
        return nullptr;
    }
    
    return asset;
}

void AssetLoader::loadAsync(const std::string& virtualPath, LoadCallback callback) {
    // For now, just load synchronously on a thread
    // TODO: Proper async loading with job system
    auto asset = loadGeneric(virtualPath);
    callback(asset, asset != nullptr);
}

void AssetLoader::loadBatch(const std::vector<std::string>& paths, 
                            std::function<void(size_t, size_t)> progress) {
    size_t total = paths.size();
    for (size_t i = 0; i < total; ++i) {
        loadGeneric(paths[i]);
        if (progress) {
            progress(i + 1, total);
        }
    }
}

void AssetLoader::unload(const std::string& virtualPath) {
    m_cache.erase(virtualPath);
}

void AssetLoader::unloadUnused() {
    for (auto it = m_cache.begin(); it != m_cache.end(); ) {
        if (it->second.use_count() == 1) {
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}

void AssetLoader::clearCache() {
    m_cache.clear();
}

bool AssetLoader::isLoaded(const std::string& virtualPath) const {
    return m_cache.find(virtualPath) != m_cache.end();
}

// ============================================================================
// WORLD ASSET
// ============================================================================
bool WorldAsset::load(const std::string& diskPath) {
    std::ifstream file(diskPath);
    if (!file.is_open()) {
        return false;
    }
    
    try {
        nlohmann::json j;
        file >> j;
        
        m_name = j.value("name", "Untitled");
        displayName = j.value("displayName", m_name);
        
        if (j.contains("sun")) {
            auto& sun = j["sun"];
            sunDirection = glm::vec3(sun["direction"][0], sun["direction"][1], sun["direction"][2]);
            sunColor = glm::vec3(sun["color"][0], sun["color"][1], sun["color"][2]);
            sunIntensity = sun.value("intensity", 1.0f);
        }
        
        if (j.contains("ambient")) {
            auto& amb = j["ambient"];
            ambientColor = glm::vec3(amb[0], amb[1], amb[2]);
        }
        
        if (j.contains("fog")) {
            auto& fog = j["fog"];
            fogColor = glm::vec3(fog["color"][0], fog["color"][1], fog["color"][2]);
            fogDensity = fog.value("density", 0.001f);
        }
        
        terrainPath = j.value("terrain", "");
        navMeshPath = j.value("navMesh", "");
        
        if (j.contains("splines")) {
            for (const auto& s : j["splines"]) {
                splinePaths.push_back(s.get<std::string>());
            }
        }
        
        if (j.contains("objects")) {
            for (const auto& obj : j["objects"]) {
                WorldObjectData data;
                data.name = obj.value("name", "Object");
                data.prefabPath = obj.value("prefab", "");
                data.meshPath = obj.value("mesh", "");
                data.materialPath = obj.value("material", "");
                
                if (obj.contains("position")) {
                    data.position = glm::vec3(obj["position"][0], obj["position"][1], obj["position"][2]);
                }
                if (obj.contains("rotation")) {
                    data.rotation = glm::quat(obj["rotation"][3], obj["rotation"][0], 
                                              obj["rotation"][1], obj["rotation"][2]);
                }
                if (obj.contains("scale")) {
                    data.scale = glm::vec3(obj["scale"][0], obj["scale"][1], obj["scale"][2]);
                }
                
                data.isStatic = obj.value("static", true);
                
                if (obj.contains("tags")) {
                    for (const auto& t : obj["tags"]) {
                        data.tags.push_back(t.get<std::string>());
                    }
                }
                
                objects.push_back(std::move(data));
            }
        }
        
        if (j.contains("streamingLevels")) {
            for (const auto& sl : j["streamingLevels"]) {
                StreamingLevelData data;
                data.levelPath = sl["path"];
                if (sl.contains("boundsMin")) {
                    data.boundsMin = glm::vec3(sl["boundsMin"][0], sl["boundsMin"][1], sl["boundsMin"][2]);
                }
                if (sl.contains("boundsMax")) {
                    data.boundsMax = glm::vec3(sl["boundsMax"][0], sl["boundsMax"][1], sl["boundsMax"][2]);
                }
                data.loadDistance = sl.value("loadDistance", 1000.0f);
                data.alwaysLoaded = sl.value("alwaysLoaded", false);
                streamingLevels.push_back(std::move(data));
            }
        }
        
        std::cout << "[World] Loaded: " << displayName << " (" << objects.size() << " objects)" << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "[World] Parse error: " << e.what() << std::endl;
        return false;
    }
}

bool WorldAsset::save(const std::string& diskPath) const {
    nlohmann::json j;
    j["name"] = m_name;
    j["displayName"] = displayName;
    
    j["sun"] = {
        {"direction", {sunDirection.x, sunDirection.y, sunDirection.z}},
        {"color", {sunColor.x, sunColor.y, sunColor.z}},
        {"intensity", sunIntensity}
    };
    
    j["ambient"] = {ambientColor.x, ambientColor.y, ambientColor.z};
    
    j["fog"] = {
        {"color", {fogColor.x, fogColor.y, fogColor.z}},
        {"density", fogDensity}
    };
    
    j["terrain"] = terrainPath;
    j["navMesh"] = navMeshPath;
    j["splines"] = splinePaths;
    
    nlohmann::json objs = nlohmann::json::array();
    for (const auto& obj : objects) {
        nlohmann::json o;
        o["name"] = obj.name;
        if (!obj.prefabPath.empty()) o["prefab"] = obj.prefabPath;
        if (!obj.meshPath.empty()) o["mesh"] = obj.meshPath;
        if (!obj.materialPath.empty()) o["material"] = obj.materialPath;
        o["position"] = {obj.position.x, obj.position.y, obj.position.z};
        o["rotation"] = {obj.rotation.x, obj.rotation.y, obj.rotation.z, obj.rotation.w};
        o["scale"] = {obj.scale.x, obj.scale.y, obj.scale.z};
        o["static"] = obj.isStatic;
        o["tags"] = obj.tags;
        objs.push_back(o);
    }
    j["objects"] = objs;
    
    nlohmann::json levels = nlohmann::json::array();
    for (const auto& sl : streamingLevels) {
        nlohmann::json l;
        l["path"] = sl.levelPath;
        l["boundsMin"] = {sl.boundsMin.x, sl.boundsMin.y, sl.boundsMin.z};
        l["boundsMax"] = {sl.boundsMax.x, sl.boundsMax.y, sl.boundsMax.z};
        l["loadDistance"] = sl.loadDistance;
        l["alwaysLoaded"] = sl.alwaysLoaded;
        levels.push_back(l);
    }
    j["streamingLevels"] = levels;
    
    std::ofstream file(diskPath);
    if (file.is_open()) {
        file << j.dump(2);
        return true;
    }
    return false;
}

// ============================================================================
// MESH ASSET
// ============================================================================
bool MeshAsset::load(const std::string& diskPath) {
    // Check extension for format
    std::string ext = SanicPaths::get().getExtension(diskPath);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    if (ext == ".obj") {
        return importFromOBJ(diskPath);
    } else if (ext == ".smesh") {
        // Binary format - TODO
        std::cerr << "[Mesh] Binary format not yet implemented" << std::endl;
        return false;
    }
    
    return false;
}

bool MeshAsset::save(const std::string& diskPath) const {
    // TODO: Implement binary mesh saving
    return false;
}

bool MeshAsset::importFromOBJ(const std::string& objPath) {
    // This would use tiny_obj_loader - simplified version here
    std::cout << "[Mesh] OBJ import: " << objPath << std::endl;
    m_name = SanicPaths::get().getFilename(objPath);
    // Actual loading delegated to existing Mesh class
    return true;
}

bool MeshAsset::importFromFBX(const std::string& fbxPath) {
    // TODO: FBX import
    return false;
}

bool MeshAsset::importFromGLTF(const std::string& gltfPath) {
    // TODO: GLTF import
    return false;
}

// ============================================================================
// MATERIAL ASSET
// ============================================================================
bool MaterialAsset::load(const std::string& diskPath) {
    std::ifstream file(diskPath);
    if (!file.is_open()) {
        return false;
    }
    
    try {
        nlohmann::json j;
        file >> j;
        
        m_name = j.value("name", "Material");
        
        if (j.contains("baseColor")) {
            baseColor = glm::vec4(j["baseColor"][0], j["baseColor"][1], 
                                  j["baseColor"][2], j["baseColor"][3]);
        }
        metallic = j.value("metallic", 0.0f);
        roughness = j.value("roughness", 0.5f);
        specular = j.value("specular", 0.5f);
        
        if (j.contains("emissive")) {
            emissive = glm::vec3(j["emissive"][0], j["emissive"][1], j["emissive"][2]);
        }
        emissiveStrength = j.value("emissiveStrength", 0.0f);
        
        albedoTexture = j.value("albedoTexture", "");
        normalTexture = j.value("normalTexture", "");
        metallicRoughnessTexture = j.value("metallicRoughnessTexture", "");
        aoTexture = j.value("aoTexture", "");
        emissiveTexture = j.value("emissiveTexture", "");
        
        customShaderPath = j.value("shader", "");
        doubleSided = j.value("doubleSided", false);
        transparent = j.value("transparent", false);
        opacity = j.value("opacity", 1.0f);
        
        return true;
    }
    catch (...) {
        return false;
    }
}

bool MaterialAsset::save(const std::string& diskPath) const {
    nlohmann::json j;
    j["name"] = m_name;
    j["baseColor"] = {baseColor.x, baseColor.y, baseColor.z, baseColor.w};
    j["metallic"] = metallic;
    j["roughness"] = roughness;
    j["specular"] = specular;
    j["emissive"] = {emissive.x, emissive.y, emissive.z};
    j["emissiveStrength"] = emissiveStrength;
    j["albedoTexture"] = albedoTexture;
    j["normalTexture"] = normalTexture;
    j["metallicRoughnessTexture"] = metallicRoughnessTexture;
    j["aoTexture"] = aoTexture;
    j["emissiveTexture"] = emissiveTexture;
    j["shader"] = customShaderPath;
    j["doubleSided"] = doubleSided;
    j["transparent"] = transparent;
    j["opacity"] = opacity;
    
    std::ofstream file(diskPath);
    if (file.is_open()) {
        file << j.dump(2);
        return true;
    }
    return false;
}

// ============================================================================
// TERRAIN ASSET
// ============================================================================
bool TerrainAsset::load(const std::string& diskPath) {
    // TODO: Binary terrain format
    return false;
}

bool TerrainAsset::save(const std::string& diskPath) const {
    return false;
}

// ============================================================================
// SPLINE ASSET
// ============================================================================
bool SplineAsset::load(const std::string& diskPath) {
    std::ifstream file(diskPath);
    if (!file.is_open()) return false;
    
    try {
        nlohmann::json j;
        file >> j;
        
        m_name = j.value("name", "Spline");
        closed = j.value("closed", false);
        isRail = j.value("isRail", false);
        isRoad = j.value("isRoad", false);
        isRiver = j.value("isRiver", false);
        meshPath = j.value("mesh", "");
        materialPath = j.value("material", "");
        
        std::string typeStr = j.value("type", "catmullrom");
        if (typeStr == "linear") type = SplineType::Linear;
        else if (typeStr == "bezier") type = SplineType::Bezier;
        else if (typeStr == "hermite") type = SplineType::Hermite;
        else type = SplineType::CatmullRom;
        
        if (j.contains("points")) {
            for (const auto& pt : j["points"]) {
                ControlPoint cp;
                cp.position = glm::vec3(pt["position"][0], pt["position"][1], pt["position"][2]);
                if (pt.contains("tangentIn")) {
                    cp.tangentIn = glm::vec3(pt["tangentIn"][0], pt["tangentIn"][1], pt["tangentIn"][2]);
                }
                if (pt.contains("tangentOut")) {
                    cp.tangentOut = glm::vec3(pt["tangentOut"][0], pt["tangentOut"][1], pt["tangentOut"][2]);
                }
                cp.width = pt.value("width", 1.0f);
                cp.bankAngle = pt.value("bankAngle", 0.0f);
                cp.speedModifier = pt.value("speedModifier", 1.0f);
                points.push_back(cp);
            }
        }
        
        return true;
    }
    catch (...) {
        return false;
    }
}

bool SplineAsset::save(const std::string& diskPath) const {
    nlohmann::json j;
    j["name"] = m_name;
    j["closed"] = closed;
    j["isRail"] = isRail;
    j["isRoad"] = isRoad;
    j["isRiver"] = isRiver;
    j["mesh"] = meshPath;
    j["material"] = materialPath;
    
    switch (type) {
        case SplineType::Linear: j["type"] = "linear"; break;
        case SplineType::Bezier: j["type"] = "bezier"; break;
        case SplineType::Hermite: j["type"] = "hermite"; break;
        default: j["type"] = "catmullrom"; break;
    }
    
    nlohmann::json pts = nlohmann::json::array();
    for (const auto& cp : points) {
        nlohmann::json p;
        p["position"] = {cp.position.x, cp.position.y, cp.position.z};
        p["tangentIn"] = {cp.tangentIn.x, cp.tangentIn.y, cp.tangentIn.z};
        p["tangentOut"] = {cp.tangentOut.x, cp.tangentOut.y, cp.tangentOut.z};
        p["width"] = cp.width;
        p["bankAngle"] = cp.bankAngle;
        p["speedModifier"] = cp.speedModifier;
        pts.push_back(p);
    }
    j["points"] = pts;
    
    std::ofstream file(diskPath);
    if (file.is_open()) {
        file << j.dump(2);
        return true;
    }
    return false;
}

// ============================================================================
// PREFAB ASSET
// ============================================================================
bool PrefabAsset::load(const std::string& diskPath) {
    std::ifstream file(diskPath);
    if (!file.is_open()) return false;
    
    try {
        nlohmann::json j;
        file >> j;
        
        m_name = j.value("name", "Prefab");
        
        auto loadObjectData = [](const nlohmann::json& obj) -> WorldObjectData {
            WorldObjectData data;
            data.name = obj.value("name", "Object");
            data.meshPath = obj.value("mesh", "");
            data.materialPath = obj.value("material", "");
            if (obj.contains("position")) {
                data.position = glm::vec3(obj["position"][0], obj["position"][1], obj["position"][2]);
            }
            if (obj.contains("rotation")) {
                data.rotation = glm::quat(obj["rotation"][3], obj["rotation"][0], 
                                          obj["rotation"][1], obj["rotation"][2]);
            }
            if (obj.contains("scale")) {
                data.scale = glm::vec3(obj["scale"][0], obj["scale"][1], obj["scale"][2]);
            }
            data.isStatic = obj.value("static", true);
            return data;
        };
        
        if (j.contains("root")) {
            rootObject = loadObjectData(j["root"]);
        }
        
        if (j.contains("children")) {
            for (const auto& c : j["children"]) {
                children.push_back(loadObjectData(c));
            }
        }
        
        if (j.contains("components")) {
            for (const auto& comp : j["components"]) {
                ComponentData cd;
                cd.type = comp["type"];
                if (comp.contains("properties")) {
                    for (auto& [k, v] : comp["properties"].items()) {
                        cd.properties[k] = v.get<std::string>();
                    }
                }
                components.push_back(cd);
            }
        }
        
        return true;
    }
    catch (...) {
        return false;
    }
}

bool PrefabAsset::save(const std::string& diskPath) const {
    // TODO: Implement
    return false;
}

} // namespace Sanic
