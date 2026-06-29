#include <gtest/gtest.h>
#include "../VideoStorage.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class VideoStorageTest : public ::testing::Test {
protected:
    std::string test_dir = "./test_storage";

    void SetUp() override {
        fs::create_directory(test_dir);
    }

    void TearDown() override {
        fs::remove_all(test_dir);
    }
};

TEST_F(VideoStorageTest, FilenameCreation) {
    VideoStorage storage(test_dir);
    std::string filename = storage.create_filename("TestDoctor");
    
    EXPECT_NE(filename.find(test_dir), std::string::npos);
    EXPECT_NE(filename.find("TestDoctor"), std::string::npos);
    EXPECT_NE(filename.find(".mkv"), std::string::npos);
}

TEST_F(VideoStorageTest, ListVideos) {
    VideoStorage storage(test_dir);
    
    // Create a dummy mkv file
    std::ofstream dummy(test_dir + "/test.mkv");
    dummy << "data";
    dummy.close();
    
    auto files = storage.list_videos();
    ASSERT_EQ(files.size(), 1);
    EXPECT_NE(files[0].find("test.mkv"), std::string::npos);
}

TEST_F(VideoStorageTest, GetAvailableSpace) {
    VideoStorage storage(test_dir);
    std::uintmax_t space = storage.get_available_space();
    EXPECT_GT(space, 0); // Disk size should be > 0
}
