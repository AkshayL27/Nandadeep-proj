#include "StreamEngine.hpp"
#include <iostream>
#include "Logger.hpp"

StreamEngine::StreamEngine(VideoStorage& storage) : storage_ref(storage) {}

StreamEngine::~StreamEngine() {
    if (loop) g_main_loop_quit(loop);
    // Clean up any active recordings
    std::lock_guard<std::mutex> lock(engine_mutex);
    for (auto const& [doc, pipeline] : active_recorders) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }
}

void StreamEngine::init() {
    gst_init(nullptr, nullptr);
    loop = g_main_loop_new(nullptr, FALSE);
    
    // Create the RTSP Server
    server = gst_rtsp_server_new();
    gst_rtsp_server_set_service(server, "8554"); // Standard RTSP port
    
    mounts = gst_rtsp_server_get_mount_points(server);
    factory = gst_rtsp_media_factory_new();
    
    // THE PIPELINE:
    // 1. Receives UDP stream from Raspi (port 5000)
    // 2. Payloads it for RTSP clients
    // Note: We use 'shared' so multiple clients see the same stream
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    
    // Input: Expecting H264 UDP stream from Pi on Port 5000
    // Output: RTSP Clients connect to this server
    std::string launch_cmd = 
        "( udpsrc port=5000 multicast-group=239.0.0.1 auto-multicast=true buffer-size=10000000 do-timestamp=true ! application/x-rtp, encoding-name=H264, payload=96 ! "
        "rtph264depay ! h264parse ! rtph264pay name=pay0 pt=96 )";
        
    gst_rtsp_media_factory_set_launch(factory, launch_cmd.c_str());
    
    // Attach to /live endpoint
    gst_rtsp_mount_points_add_factory(mounts, "/live", factory);
    
    Logger::info("[StreamEngine] RTSP Server ready at rtsp://<server_ip>:8554/live");

    // Check storage every 10 seconds
    g_timeout_add_seconds(10, (GSourceFunc)check_storage_callback, this);
}

void StreamEngine::run() {
    Logger::info("[StreamEngine] Running...");
    gst_rtsp_server_attach(server, nullptr);
    g_main_loop_run(loop);
}

// Independent Recording Pipeline
// Note: In production, we would use 'interpipes' or 'tee' to record the exact 
// same buffer as the live stream. For simplicity here, we open a parallel socket listener
// or assume the Pi sends to a multicast group.
// 
// Simplified approach: We spin up a separate GStreamer pipeline that listens 
// to the SAME UDP port (using multicast) or a separate port if the Pi splits it.
// Here we assume the Pi sends to a Multicast Address (e.g., 224.1.1.1) so both 
// the RTSP server and Recorder can read it.
void StreamEngine::start_recording(const std::string& doctor_name, int port) {
    std::lock_guard<std::mutex> lock(engine_mutex);

    // Check for minimum 500MB space
    if (storage_ref.get_available_space() < 500ULL * 1024 * 1024) {
        Logger::error("[StreamEngine] Not enough disk space to start recording!");
        return;
    }

    if (active_recorders.find(doctor_name) != active_recorders.end()) {
        Logger::info("[StreamEngine] Already recording for " + doctor_name);
        return;
    }

    std::string filename = storage_ref.create_filename(doctor_name);
    Logger::info("[StreamEngine] Recording Port " + std::to_string(port) + " to: " + filename);

    // Pipeline: Listen UDP -> Parse -> Mux -> File
    // We use 'matroskamux' (MKV) because it is resilient to power failure.
    std::string pipeline_str = 
        "udpsrc port=" + std::to_string(port) + " multicast-group=239.0.0.1 buffer-size=10000000 do-timestamp=true ! application/x-rtp, encoding-name=H264 ! "
        "rtph264depay ! h264parse ! matroskamux ! filesink location=" + filename;

    GError* error = nullptr;
    GstElement* new_pipeline = gst_parse_launch(pipeline_str.c_str(), &error);

    if (error) {
        Logger::error(std::string("Pipeline error: ") + error->message);
        return;
    }

    gst_element_set_state(new_pipeline, GST_STATE_PLAYING);
    active_recorders[doctor_name] = new_pipeline;
}

void StreamEngine::stop_recording(const std::string& doctor_name) {
    std::lock_guard<std::mutex> lock(engine_mutex);
    auto it = active_recorders.find(doctor_name);
    if (it == active_recorders.end()) return;

    Logger::info("[StreamEngine] Stopping recording for " + doctor_name + "...");
    GstElement* pipeline = it->second;
    
    // Send EOS (End of Stream) to close file cleanly if possible
    gst_element_send_event(pipeline, gst_event_new_eos());
    
    // Wait a moment for EOS to process
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    active_recorders.erase(it);
}

gboolean StreamEngine::check_storage_callback(gpointer user_data) {
    StreamEngine* engine = static_cast<StreamEngine*>(user_data);
    
    // 500 MB Threshold
    if (engine->storage_ref.get_available_space() < 500ULL * 1024 * 1024) {
        std::lock_guard<std::mutex> lock(engine->engine_mutex);
        if (!engine->active_recorders.empty()) {
            Logger::error("[StreamEngine] Disk full! Stopping all recordings.");
            for (auto const& [doc, pipeline] : engine->active_recorders) {
                gst_element_send_event(pipeline, gst_event_new_eos());
                // We can't wait here inside the main loop, so we just set NULL
                gst_element_set_state(pipeline, GST_STATE_NULL);
                gst_object_unref(pipeline);
            }
            engine->active_recorders.clear();
        }
    }
    return TRUE; // Continue calling this
}