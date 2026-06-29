#include "StreamEngine.hpp"
#include "Logger.hpp"
#include "alert.hpp"
#include <algorithm>
#include <iostream>

StreamEngine::StreamEngine(VideoStorage &storage) : storage_ref(storage) {}

StreamEngine::~StreamEngine() {
  if (loop)
    g_main_loop_quit(loop);
  // Clean up any active recordings
  std::unique_lock<std::shared_mutex> lock(engine_mutex);
  for (auto &[doc, session] : sessions) {
    gst_element_set_state(session.pipeline, GST_STATE_NULL);
    gst_object_unref(session.pipeline);
  }
  sessions.clear();

  // Clean up splitter
  if (splitter_pipeline) {
    gst_element_set_state(splitter_pipeline, GST_STATE_NULL);
    gst_object_unref(splitter_pipeline);
  }
}

void StreamEngine::update_session_activity(const std::string &doctor_name) {
  // Legacy/Unused if probe updates directly, but keeping if needed
}

std::string
StreamEngine::get_active_multicast_ip(const std::string &doctor_name) {
  std::shared_lock<std::shared_mutex> lock(engine_mutex);
  if (sessions.count(doctor_name)) {
    return sessions[doctor_name].multicast_ip;
  }
  return "";
}

GstPadProbeReturn StreamEngine::data_probe_cb(GstPad *pad,
                                              GstPadProbeInfo *info,
                                              gpointer user_data) {
  // Cast directly to the session struct
  RecordingSession *session = static_cast<RecordingSession *>(user_data);
  if (session) {
    session->last_activity = std::time(nullptr);
  }
  return GST_PAD_PROBE_OK;
}

void StreamEngine::init() {
  gst_init(nullptr, nullptr);
  loop = g_main_loop_new(nullptr, FALSE);

  // Initial storage check, subsequent checks are scheduled dynamically
  g_timeout_add_seconds(10, (GSourceFunc)check_storage_callback, this);

  // Monitor connectivity every 1 second (faster detection)
  g_timeout_add_seconds(1, (GSourceFunc)monitor_stream_callback, this);
}

void StreamEngine::run() {
  Logger::info("[StreamEngine] Running GStreamer main loop...");
  g_main_loop_run(loop);
}

// Independent Recording Pipeline
// Note: In production, we would use 'interpipes' or 'tee' to record the exact
// same buffer as the live stream. For simplicity here, we open a parallel
// socket listener or assume the Pi sends to a multicast group.
//
// Simplified approach: We spin up a separate GStreamer pipeline that listens
// to the SAME UDP port (using multicast) or a separate port if the Pi splits
// it. Here we assume the Pi sends to a Multicast Address (e.g., 224.1.1.1) so
// both the live stream and Recorder can read it.
void StreamEngine::start_recording(const std::string &doctor_name, int port) {
  std::unique_lock<std::shared_mutex> lock(engine_mutex);

  if (storage_ref.get_available_space() < 500ULL * 1024 * 1024) {
    Logger::error("[StreamEngine] Not enough disk space to start recording!");
    return;
  }

  if (sessions.find(doctor_name) != sessions.end()) {
    Logger::info("[StreamEngine] Already recording for " + doctor_name);
    return;
  }

  std::string filename = storage_ref.create_filename(doctor_name);

  // Assign a Multicast IP
  std::string multicast_ip =
      "239.0.0." + std::to_string(next_multicast_last_octet++);
  if (next_multicast_last_octet > 250)
    next_multicast_last_octet = 100;

  Logger::info("[StreamEngine] Recording Port " + std::to_string(port) +
               " to: " + filename);
  Logger::info("[StreamEngine] Multicast Live View: udp://@" + multicast_ip +
               ":5000");

  // Generic Source for any port
  std::string source_cmd = "udpsrc name=src port=" + std::to_string(port);

  // Pipeline: Listen UDP -> Demux -> Parse -> Tee -> (File) + (Multicast)
  //
  // Key options:
  //   sync=false on udpsrc        — prevents clock stalls when no data arrives
  //   streamable=true on mkvmux   — writes the MKV header immediately so the
  //                                  file is playable even if EOS is never sent
  //   async=false on file queue   — avoids preroll deadlock with matroskamux
  std::string pipeline_str =
      source_cmd +
      " buffer-size=10000000 sync=false ! tsdemux ! h264parse "
      "config-interval=-1 ! tee name=t "
      "t. ! queue max-size-buffers=0 max-size-time=0 async=false ! matroskamux "
      "streamable=true ! filesink location=" +
      filename +
      " sync=false "
      "t. ! queue ! mpegtsmux ! udpsink host=" +
      multicast_ip + " port=5000 auto-multicast=true sync=false";

  GError *error = nullptr;
  GstElement *new_pipeline = gst_parse_launch(pipeline_str.c_str(), &error);

  if (error) {
    Logger::error(std::string("Pipeline error: ") + error->message);
    return;
  }

  // CREATE SESSION & ATTACH PROBE
  // We insert into map first to get a stable pointer for the probe
  // Use operator[] to creating default entry, then fill it
  RecordingSession &session = sessions[doctor_name];
  session.pipeline = new_pipeline;
  session.multicast_ip = multicast_ip;
  session.last_activity.store(0);

  RecordingSession *session_ptr = &session;

  GstElement *src = gst_bin_get_by_name(GST_BIN(new_pipeline), "src");
  if (src) {
    GstPad *pad = gst_element_get_static_pad(src, "src");
    if (pad) {
      gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, data_probe_cb,
                        session_ptr, NULL);
      gst_object_unref(pad);
    }
    gst_object_unref(src);
  } else {
    Logger::error("[StreamEngine] Failed to get src element for probe!");
  }

  gst_element_set_state(new_pipeline, GST_STATE_PLAYING);

  // --- STARTUP VERIFICATION ---
  // Wait up to 2 seconds for data
  Logger::info("[StreamEngine] Verifying connection...");
  bool connected = false;
  for (int i = 0; i < 20; ++i) { // 20 * 100ms = 2s
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (session_ptr->last_activity > 0) {
      connected = true;
      break;
    }
  }

  if (!connected) {
    Logger::error("[StreamEngine] Cannot start recording: Camera not connected "
                  "or not streaming!");
    gst_element_set_state(new_pipeline, GST_STATE_NULL);
    gst_object_unref(new_pipeline);
    sessions.erase(doctor_name);
    return;
  }

  // Update timestamp to now to prevent immediate timeout
  session_ptr->last_activity = std::time(nullptr);
  Logger::info("[StreamEngine] Connection verified. Recording started.");
}

void StreamEngine::stop_recording(const std::string &doctor_name) {
  std::unique_lock<std::shared_mutex> lock(engine_mutex);
  auto it = sessions.find(doctor_name);
  if (it == sessions.end())
    return;

  Logger::info("[StreamEngine] Stopping recording for " + doctor_name + "...");
  GstElement *pipeline = it->second.pipeline;

  // Send EOS and wait for it to propagate through the pipeline.
  // matroskamux needs time to write the final index/cues to disk so the
  // file is fully seekable. 3 seconds is a safe upper bound.
  gst_element_send_event(pipeline, gst_event_new_eos());

  // Wait for the EOS message on the bus (up to 3 seconds) so mkvmux
  // can finalize the container before we set the pipeline to NULL.
  GstBus *bus = gst_element_get_bus(pipeline);
  if (bus) {
    GstMessage *msg = gst_bus_timed_pop_filtered(
        bus, 3 * GST_SECOND,
        static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    if (msg)
      gst_message_unref(msg);
    gst_object_unref(bus);
  }

  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);

  sessions.erase(it);
}

gboolean StreamEngine::check_storage_callback(gpointer user_data) {
  StreamEngine *engine = static_cast<StreamEngine *>(user_data);

  unsigned long long current_space = engine->storage_ref.get_available_space();
  const unsigned long long threshold = 500ULL * 1024 * 1024; // 500 MB

  int num_sessions = 0;
  {
    std::shared_lock<std::shared_mutex> lock(engine->engine_mutex);
    num_sessions = engine->sessions.size();

    if (current_space < threshold) {
      Alert::out_of_space_alert(
          "Disk storage is critically low! Under 500MB left.");
      /*
      if (!engine->sessions.empty()) {
        Logger::error("[StreamEngine] Disk full! Stopping all recordings.");
        for (auto const &[doc, session] : engine->sessions) {
          gst_element_send_event(session.pipeline, gst_event_new_eos());
          gst_element_set_state(session.pipeline, GST_STATE_NULL);
          gst_object_unref(session.pipeline);
        }
        engine->sessions.clear();
      }
      */
    }
  }

  // Dynamic Interval Calculation
  guint next_check_seconds = 10;
  if (num_sessions == 0 || current_space < threshold) {
    next_check_seconds = 10; // Idle or full
  } else {
    // Assume max 2 MB/s per active stream to be safe
    unsigned long long consumption_rate = num_sessions * 2ULL * 1024 * 1024;
    unsigned long long safe_space = current_space - threshold;
    unsigned long long time_to_threshold = safe_space / consumption_rate;

    // Check halfway to threshold, bound between 2s and 300s (5 mins)
    next_check_seconds =
        std::max(2ULL, std::min(time_to_threshold / 2, 300ULL));
  }

  // Schedule next check manually
  g_timeout_add_seconds(next_check_seconds, (GSourceFunc)check_storage_callback,
                        engine);

  return FALSE; // Return FALSE to remove the current source, as we scheduled a
                // new one
}

gboolean StreamEngine::monitor_stream_callback(gpointer user_data) {
  StreamEngine *engine = static_cast<StreamEngine *>(user_data);

  std::vector<std::string> dead_sessions;
  {
    std::shared_lock<std::shared_mutex> lock(engine->engine_mutex);
    if (engine->sessions.empty())
      return TRUE;

    time_t now = std::time(nullptr);
    for (auto &[doc, session] : engine->sessions) {
      // If no data for 5 seconds, mark as dead
      if (now - session.last_activity > 5) {
        dead_sessions.push_back(doc);
      }
    }
  } // Unlock here to avoid deadlock when calling stop_recording

  for (const auto &doc : dead_sessions) {
    Logger::error("[StreamEngine] ALERT: Camera for " + doc +
                  " disconnected! Stopping recording.");
    engine->stop_recording(doc);
  }

  return TRUE;
}