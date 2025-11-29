/**
 * AssetCookerTool.cpp
 * 
 * Command-line tool for cooking assets offline.
 * Reads .obj/.gltf files and outputs .sanic_mesh files.
 * 
 * Usage:
 *   sanic_cooker input.obj -o output.sanic_mesh
 *   sanic_cooker input_dir/ --batch -o output_dir/
 *   sanic_cooker input.obj --lod-levels 8 --sdf-resolution 128
 */

#include "engine/AssetCooker.h"
#include "engine/SanicAssetFormat.h"
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <algorithm>

namespace fs = std::filesystem;

// ============================================================================
// COMMAND LINE PARSING
// ============================================================================

struct CookerOptions {
    std::vector<std::string> inputPaths;
    std::string outputPath;
    bool batchMode = false;
    bool recursive = false;
    bool verbose = false;
    bool force = false;  // Overwrite existing
    bool dryRun = false;
    
    // Cook settings
    Sanic::CookerConfig config;
};

void printUsage(const char* programName) {
    std::cout << "Sanic Asset Cooker - Offline processing tool\n";
    std::cout << "\nUsage:\n";
    std::cout << "  " << programName << " <input> [options]\n";
    std::cout << "  " << programName << " <input_dir> --batch [options]\n";
    std::cout << "\nOptions:\n";
    std::cout << "  -o, --output <path>       Output file or directory\n";
    std::cout << "  -b, --batch               Batch mode - process entire directory\n";
    std::cout << "  -r, --recursive           Recursive directory search\n";
    std::cout << "  -f, --force               Overwrite existing output files\n";
    std::cout << "  -v, --verbose             Verbose output\n";
    std::cout << "  --dry-run                 Print what would be done\n";
    std::cout << "\nCooking Options:\n";
    std::cout << "  --lod-levels <n>          Max LOD levels (default: 8)\n";
    std::cout << "  --lod-threshold <f>       LOD error threshold (default: 1.0)\n";
    std::cout << "  --sdf-resolution <n>      SDF volume resolution (default: 64)\n";
    std::cout << "  --sdf-padding <f>         SDF padding (default: 0.1)\n";
    std::cout << "  --no-physics              Skip physics data generation\n";
    std::cout << "  --no-compress             Skip compression\n";
    std::cout << "  --threads <n>             Number of processing threads\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << programName << " model.obj -o model.sanic_mesh\n";
    std::cout << "  " << programName << " assets/raw/ --batch -o assets/cooked/ -r\n";
    std::cout << "\n";
}

bool parseArgs(int argc, char* argv[], CookerOptions& options) {
    if (argc < 2) {
        return false;
    }
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            return false;
        } else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --output requires an argument\n";
                return false;
            }
            options.outputPath = argv[++i];
        } else if (arg == "-b" || arg == "--batch") {
            options.batchMode = true;
        } else if (arg == "-r" || arg == "--recursive") {
            options.recursive = true;
        } else if (arg == "-f" || arg == "--force") {
            options.force = true;
        } else if (arg == "-v" || arg == "--verbose") {
            options.verbose = true;
            options.config.verbose = true;
        } else if (arg == "--dry-run") {
            options.dryRun = true;
            options.config.dryRun = true;
        } else if (arg == "--lod-levels") {
            if (i + 1 >= argc) return false;
            options.config.maxLodLevels = std::stoi(argv[++i]);
        } else if (arg == "--lod-threshold") {
            if (i + 1 >= argc) return false;
            options.config.lodErrorThreshold = std::stof(argv[++i]);
        } else if (arg == "--sdf-resolution") {
            if (i + 1 >= argc) return false;
            options.config.sdfResolution = std::stoi(argv[++i]);
        } else if (arg == "--sdf-padding") {
            if (i + 1 >= argc) return false;
            options.config.sdfPadding = std::stof(argv[++i]);
        } else if (arg == "--no-physics") {
            options.config.generateConvexHulls = false;
            options.config.generateTriangleMesh = false;
        } else if (arg == "--no-compress") {
            options.config.compressPages = false;
        } else if (arg == "--threads") {
            if (i + 1 >= argc) return false;
            // Threading handled by batch cook
        } else if (arg[0] != '-') {
            options.inputPaths.push_back(arg);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
    }
    
    if (options.inputPaths.empty()) {
        std::cerr << "Error: No input files specified\n";
        return false;
    }
    
    return true;
}

// ============================================================================
// FILE DISCOVERY
// ============================================================================

std::vector<std::string> findSourceFiles(const std::string& path, bool recursive) {
    std::vector<std::string> files;
    
    if (!fs::exists(path)) {
        return files;
    }
    
    if (fs::is_regular_file(path)) {
        // Single file
        std::string ext = fs::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
            files.push_back(path);
        }
        return files;
    }
    
    // Directory
    auto iterator = recursive ? 
        fs::recursive_directory_iterator(path) : 
        fs::recursive_directory_iterator(path, fs::directory_options::none);
    
    if (recursive) {
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
                    files.push_back(entry.path().string());
                }
            }
        }
    } else {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx") {
                    files.push_back(entry.path().string());
                }
            }
        }
    }
    
    return files;
}

std::string getOutputPath(const std::string& inputPath, const std::string& outputDir) {
    fs::path input(inputPath);
    fs::path output(outputDir);
    
    if (fs::is_directory(outputDir) || outputDir.back() == '/' || outputDir.back() == '\\') {
        // Output is a directory - use input filename with new extension
        output /= input.stem();
        output += ".sanic_mesh";
    }
    
    return output.string();
}

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char* argv[]) {
    CookerOptions options;
    
    if (!parseArgs(argc, argv, options)) {
        printUsage(argv[0]);
        return 1;
    }
    
    // Gather all files to process
    std::vector<std::string> sourceFiles;
    for (const auto& inputPath : options.inputPaths) {
        auto files = findSourceFiles(inputPath, options.recursive);
        sourceFiles.insert(sourceFiles.end(), files.begin(), files.end());
    }
    
    if (sourceFiles.empty()) {
        std::cerr << "Error: No source files found\n";
        return 1;
    }
    
    std::cout << "Sanic Asset Cooker\n";
    std::cout << "==================\n";
    std::cout << "Found " << sourceFiles.size() << " file(s) to process\n\n";
    
    // Create cooker
    Sanic::AssetCooker cooker;
    cooker.setConfig(options.config);
    
    // Set up progress callback
    if (options.verbose) {
        cooker.setProgressCallback([](const std::string& stage, float progress) {
            std::cout << "  " << stage << ": " << int(progress * 100) << "%\r" << std::flush;
        });
    }
    
    // Process each file
    auto startTime = std::chrono::high_resolution_clock::now();
    uint32_t successCount = 0;
    uint32_t failCount = 0;
    
    for (const auto& sourcePath : sourceFiles) {
        // Determine output path
        std::string outputPath;
        if (options.outputPath.empty()) {
            // Use source path with new extension
            fs::path p(sourcePath);
            p.replace_extension(".sanic_mesh");
            outputPath = p.string();
        } else if (options.batchMode) {
            outputPath = getOutputPath(sourcePath, options.outputPath);
        } else {
            outputPath = options.outputPath;
        }
        
        // Check if output exists
        if (fs::exists(outputPath) && !options.force) {
            if (options.verbose) {
                std::cout << "Skipping (exists): " << sourcePath << "\n";
            }
            continue;
        }
        
        if (options.dryRun) {
            std::cout << "Would cook: " << sourcePath << " -> " << outputPath << "\n";
            continue;
        }
        
        if (options.verbose) {
            std::cout << "Cooking: " << sourcePath << "\n";
        }
        
        // Create output directory if needed
        fs::path outputDir = fs::path(outputPath).parent_path();
        if (!outputDir.empty() && !fs::exists(outputDir)) {
            fs::create_directories(outputDir);
        }
        
        // Cook the file
        if (!cooker.cookFile(sourcePath, outputPath)) {
            std::cerr << "Error cooking: " << sourcePath << "\n";
            std::cerr << "  Reason: " << cooker.getLastError() << "\n";
            failCount++;
            continue;
        }
        
        if (options.verbose) {
            std::cout << "  -> " << outputPath << "\n";
            const auto& stats = cooker.getStats();
            std::cout << "  Stats: " << stats.inputTriangles << " tris -> "
                      << stats.outputClusters << " clusters, "
                      << stats.outputMeshlets << " meshlets\n";
            std::cout << "  Size: " << (stats.totalSize / 1024) << " KB\n";
        }
        
        successCount++;
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    double totalSeconds = std::chrono::duration<double>(endTime - startTime).count();
    
    std::cout << "\n";
    std::cout << "==================\n";
    std::cout << "Cooking complete!\n";
    std::cout << "  Success: " << successCount << "\n";
    std::cout << "  Failed:  " << failCount << "\n";
    std::cout << "  Time:    " << totalSeconds << "s\n";
    
    return failCount > 0 ? 1 : 0;
}
