/**
 * ShaderHotReload.cpp
 * 
 * Implementation of the shader hot-reload file watcher.
 */

#include "ShaderHotReload.h"
#include <iostream>
#include <algorithm>

namespace Sanic {

ShaderHotReload::ShaderHotReload() {
    // Default extension filters for shader files
    extensionFilters_ = {
        ".glsl", ".hlsl", ".vert", ".frag", ".comp",
        ".geom", ".tesc", ".tese", ".mesh", ".task",
        ".rgen", ".rmiss", ".rchit", ".rahit", ".rint",
        ".rcall", ".glsli", ".h", ".inc"
    };
}

ShaderHotReload::~ShaderHotReload() {
    stop();
}

bool ShaderHotReload::start(const std::vector<std::filesystem::path>& watchPaths, bool recursive) {
    if (running_.load()) {
        stop();
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    watchPaths_ = watchPaths;
    recursive_ = recursive;
    fileTimestamps_.clear();
    
    // Initial scan to record timestamps
    for (const auto& path : watchPaths_) {
        if (std::filesystem::exists(path)) {
            scanDirectory(path, recursive);
        }
    }
    
    stats_.directoriesWatched = static_cast<uint32_t>(watchPaths_.size());
    stats_.filesWatched = static_cast<uint32_t>(fileTimestamps_.size());
    
    std::cout << "ShaderHotReload: Watching " << stats_.filesWatched 
              << " files in " << stats_.directoriesWatched << " directories" << std::endl;
    
    // Start watch thread
    running_.store(true);
    stopRequested_.store(false);
    watchThread_ = std::thread(&ShaderHotReload::watchThread, this);
    
    return true;
}

void ShaderHotReload::stop() {
    if (!running_.load()) {
        return;
    }
    
    stopRequested_.store(true);
    
    if (watchThread_.joinable()) {
        watchThread_.join();
    }
    
    running_.store(false);
    stopRequested_.store(false);
    
    std::cout << "ShaderHotReload: Stopped" << std::endl;
}

uint32_t ShaderHotReload::onReload(ReloadCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t id = nextCallbackId_++;
    callbacks_.push_back({id, std::move(callback)});
    return id;
}

uint32_t ShaderHotReload::onBatchReload(BatchCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint32_t id = nextCallbackId_++;
    callbacks_.push_back({id, std::move(callback)});
    return id;
}

void ShaderHotReload::removeCallback(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.erase(
        std::remove_if(callbacks_.begin(), callbacks_.end(),
            [id](const CallbackEntry& entry) { return entry.id == id; }),
        callbacks_.end()
    );
}

void ShaderHotReload::watchThread() {
    while (!stopRequested_.load()) {
        checkForChanges();
        
        // Process debounced changes
        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            auto now = std::chrono::steady_clock::now();
            
            std::vector<ShaderFileEvent> readyEvents;
            
            for (auto it = pendingChanges_.begin(); it != pendingChanges_.end();) {
                if (now - it->second >= debounceDelay_) {
                    // Find the event for this path
                    for (const auto& event : pendingEvents_) {
                        if (event.path.string() == it->first) {
                            readyEvents.push_back(event);
                            break;
                        }
                    }
                    it = pendingChanges_.erase(it);
                } else {
                    ++it;
                }
            }
            
            // Remove processed events from pending
            pendingEvents_.erase(
                std::remove_if(pendingEvents_.begin(), pendingEvents_.end(),
                    [&readyEvents](const ShaderFileEvent& event) {
                        return std::find_if(readyEvents.begin(), readyEvents.end(),
                            [&event](const ShaderFileEvent& e) {
                                return e.path == event.path;
                            }) != readyEvents.end();
                    }),
                pendingEvents_.end()
            );
            
            if (!readyEvents.empty()) {
                notifyCallbacks(readyEvents);
            }
        }
        
        std::this_thread::sleep_for(POLL_INTERVAL);
    }
}

void ShaderHotReload::checkForChanges() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<ShaderFileEvent> events;
    
    // Check existing files for modifications
    for (auto& [path, lastTime] : fileTimestamps_) {
        std::filesystem::path filePath(path);
        
        if (!std::filesystem::exists(filePath)) {
            // File was deleted
            ShaderFileEvent event;
            event.type = ShaderFileEvent::Type::Deleted;
            event.path = filePath;
            event.timestamp = std::chrono::system_clock::now();
            events.push_back(event);
        } else {
            try {
                auto currentTime = std::filesystem::last_write_time(filePath);
                if (currentTime != lastTime) {
                    lastTime = currentTime;
                    
                    ShaderFileEvent event;
                    event.type = ShaderFileEvent::Type::Modified;
                    event.path = filePath;
                    event.timestamp = std::chrono::system_clock::now();
                    events.push_back(event);
                }
            } catch (const std::filesystem::filesystem_error&) {
                // File might be locked or inaccessible
            }
        }
    }
    
    // Remove deleted files from tracking
    for (const auto& event : events) {
        if (event.type == ShaderFileEvent::Type::Deleted) {
            fileTimestamps_.erase(event.path.string());
        }
    }
    
    // Scan for new files
    for (const auto& watchPath : watchPaths_) {
        if (!std::filesystem::exists(watchPath)) continue;
        
        try {
            auto scanPath = [&](const std::filesystem::path& p) {
                if (shouldWatch(p)) {
                    std::string pathStr = p.string();
                    if (fileTimestamps_.find(pathStr) == fileTimestamps_.end()) {
                        fileTimestamps_[pathStr] = std::filesystem::last_write_time(p);
                        
                        ShaderFileEvent event;
                        event.type = ShaderFileEvent::Type::Created;
                        event.path = p;
                        event.timestamp = std::chrono::system_clock::now();
                        events.push_back(event);
                    }
                }
            };
            
            if (recursive_) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(watchPath)) {
                    if (entry.is_regular_file()) {
                        scanPath(entry.path());
                    }
                }
            } else {
                for (const auto& entry : std::filesystem::directory_iterator(watchPath)) {
                    if (entry.is_regular_file()) {
                        scanPath(entry.path());
                    }
                }
            }
        } catch (const std::filesystem::filesystem_error&) {
            // Directory access error
        }
    }
    
    // Add to pending with debounce
    if (!events.empty()) {
        std::lock_guard<std::mutex> pendingLock(pendingMutex_);
        auto now = std::chrono::steady_clock::now();
        
        for (const auto& event : events) {
            std::string pathStr = event.path.string();
            pendingChanges_[pathStr] = now;
            
            // Update or add pending event
            auto it = std::find_if(pendingEvents_.begin(), pendingEvents_.end(),
                [&pathStr](const ShaderFileEvent& e) { return e.path.string() == pathStr; });
            
            if (it != pendingEvents_.end()) {
                *it = event;
            } else {
                pendingEvents_.push_back(event);
            }
        }
    }
    
    stats_.filesWatched = static_cast<uint32_t>(fileTimestamps_.size());
}

void ShaderHotReload::processPendingChanges() {
    std::vector<ShaderFileEvent> events;
    
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        events = std::move(pendingEvents_);
        pendingEvents_.clear();
        pendingChanges_.clear();
    }
    
    if (!events.empty()) {
        notifyCallbacks(events);
    }
}

std::vector<std::string> ShaderHotReload::getModifiedShaders() {
    std::vector<std::string> result;
    
    std::lock_guard<std::mutex> lock(pendingMutex_);
    for (const auto& event : pendingEvents_) {
        if (event.type == ShaderFileEvent::Type::Modified) {
            result.push_back(event.path.string());
        }
    }
    
    return result;
}

void ShaderHotReload::scanDirectory(const std::filesystem::path& dir, bool recursive) {
    try {
        if (recursive) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
                if (entry.is_regular_file() && shouldWatch(entry.path())) {
                    std::string pathStr = entry.path().string();
                    fileTimestamps_[pathStr] = std::filesystem::last_write_time(entry.path());
                }
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file() && shouldWatch(entry.path())) {
                    std::string pathStr = entry.path().string();
                    fileTimestamps_[pathStr] = std::filesystem::last_write_time(entry.path());
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "ShaderHotReload: Error scanning directory " << dir << ": " << e.what() << std::endl;
    }
}

bool ShaderHotReload::shouldWatch(const std::filesystem::path& path) const {
    if (extensionFilters_.empty()) {
        return true;
    }
    
    std::string ext = path.extension().string();
    // Convert to lowercase for comparison
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    return extensionFilters_.find(ext) != extensionFilters_.end();
}

void ShaderHotReload::notifyCallbacks(const std::vector<ShaderFileEvent>& events) {
    if (events.empty()) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    stats_.reloadsTriggered += static_cast<uint32_t>(events.size());
    stats_.lastReload = std::chrono::system_clock::now();
    
    for (const auto& event : events) {
        std::cout << "ShaderHotReload: " 
                  << (event.type == ShaderFileEvent::Type::Modified ? "Modified" :
                      event.type == ShaderFileEvent::Type::Created ? "Created" : "Deleted")
                  << " " << event.path.filename().string() << std::endl;
    }
    
    for (const auto& entry : callbacks_) {
        std::visit([&events](auto&& callback) {
            using T = std::decay_t<decltype(callback)>;
            if constexpr (std::is_same_v<T, ReloadCallback>) {
                for (const auto& event : events) {
                    callback(event.path.string());
                }
            } else if constexpr (std::is_same_v<T, BatchCallback>) {
                callback(events);
            }
        }, entry.callback);
    }
}

void ShaderHotReload::addExtensionFilter(const std::string& extension) {
    std::string ext = extension;
    if (!ext.empty() && ext[0] != '.') {
        ext = "." + ext;
    }
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    extensionFilters_.insert(ext);
}

void ShaderHotReload::clearExtensionFilters() {
    extensionFilters_.clear();
}

void ShaderHotReload::setDebounceDelay(std::chrono::milliseconds delay) {
    debounceDelay_ = delay;
}

void ShaderHotReload::forceReload(const std::filesystem::path& path) {
    ShaderFileEvent event;
    event.type = ShaderFileEvent::Type::Modified;
    event.path = path;
    event.timestamp = std::chrono::system_clock::now();
    
    notifyCallbacks({event});
}

ShaderHotReload::Stats ShaderHotReload::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

// Global instance
static ShaderHotReload g_shaderHotReload;

ShaderHotReload& GetShaderHotReload() {
    return g_shaderHotReload;
}

} // namespace Sanic
