#pragma once
#include <string>
#include <vector>
#include <cstdint>

class VideoStorage {
public:
    VideoStorage(const std::string& root_dir);

    // Creates a filename: root_dir/YYYY-MM-DD_HH-MM-SS_DrName.mkv
    std::string create_filename(const std::string& doctor_name);

    // Returns a list of all .mkv files in the directory
    std::vector<std::string> list_videos();

    // Returns available disk space in bytes
    std::uintmax_t get_available_space();

private:
    std::string storage_dir;
};