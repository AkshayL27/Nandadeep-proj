#include "VideoStorage.hpp"
#include <filesystem>
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

VideoStorage::VideoStorage(const std::string& root_dir) : storage_dir(root_dir) {
    // Check if directory exists, if not create it
    if (!fs::exists(storage_dir)) {
        try {
            fs::create_directory(storage_dir);
        } catch (const fs::filesystem_error& e) {
            std::cerr << "Error creating directory: " << e.what() << std::endl;
        }
    }
}

std::string VideoStorage::create_filename(const std::string& doctor_name) {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << storage_dir << "/";
    // Format: YYYY-MM-DD_HH-MM-SS
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d_%H-%M-%S_");
    
    std::string base_path = ss.str() + doctor_name;
    std::string final_path = base_path + ".mkv";

    // Check if file exists, append counter if needed to prevent overwrite
    int counter = 1;
    while (fs::exists(final_path)) {
        final_path = base_path + "_" + std::to_string(counter++) + ".mkv";
    }
    
    return final_path;
}

std::vector<std::string> VideoStorage::list_videos() {
    std::vector<std::string> files;
    if (fs::exists(storage_dir) && fs::is_directory(storage_dir)) {
        for (const auto& entry : fs::directory_iterator(storage_dir)) {
            if (entry.path().extension() == ".mkv") {
                files.push_back(entry.path().string());
            }
        }
    }
    return files;
}

std::uintmax_t VideoStorage::get_available_space() {
    try {
        return fs::space(storage_dir).available;
    } catch (...) {
        return 0;
    }
}