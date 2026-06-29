#pragma once
#include <gst/gst.h>
#include <string>
#include <thread>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include "VideoStorage.hpp"

class StreamEngine {
public:
    StreamEngine(VideoStorage& storage);
    ~StreamEngine();

    // Initialize GStreamer
    void init();
    
    // Starts the main GStreamer loop
    void run();

    // Dynamically starts/stops recording to disk
    void start_recording(const std::string& doctor_name, int port);
    void stop_recording(const std::string& doctor_name);
    
    // Returns the multicast IP assigned to a specific active recording, or empty string if not found
    std::string get_active_multicast_ip(const std::string& doctor_name);

    GMainLoop* get_loop() const { return loop; }

    // Helper for probe
    void update_session_activity(const std::string& doctor_name);

private:
    struct RecordingSession {
        GstElement* pipeline;
        std::string multicast_ip;
        std::atomic<time_t> last_activity;
    };

    VideoStorage& storage_ref;
    GMainLoop* loop;
    
    GstElement* splitter_pipeline = nullptr;
    
    // Map: DoctorName -> Session Data
    std::map<std::string, RecordingSession> sessions;
    int next_multicast_last_octet = 100; // Start at .100

    mutable std::shared_mutex engine_mutex;
    
    // Stream Health Monitoring
    static gboolean check_storage_callback(gpointer user_data);
    static gboolean monitor_stream_callback(gpointer user_data);
    static GstPadProbeReturn data_probe_cb(GstPad *pad, GstPadProbeInfo *info, gpointer user_data);
};