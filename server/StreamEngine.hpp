#pragma once
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <string>
#include <thread>
#include <map>
#include <mutex>
#include "VideoStorage.hpp"

class StreamEngine {
public:
    StreamEngine(VideoStorage& storage);
    ~StreamEngine();

    // Initialize GStreamer
    void init();
    
    // Starts the main RTSP Server loop
    void run();

    // Dynamically starts/stops recording to disk
    void start_recording(const std::string& doctor_name, int port);
    void stop_recording(const std::string& doctor_name);

private:
    VideoStorage& storage_ref;
    GMainLoop* loop;
    GstRTSPServer* server;
    GstRTSPMountPoints* mounts;
    GstRTSPMediaFactory* factory;
    
    // Recording Pipeline Elements
    std::map<std::string, GstElement*> active_recorders;
    std::mutex engine_mutex;
    
    static void media_configure_callback(GstRTSPMediaFactory* factory, GstRTSPMedia* media, gpointer user_data);
    static gboolean check_storage_callback(gpointer user_data);
};