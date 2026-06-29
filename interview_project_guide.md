# Nandadeep Hospital Video Server — Interview Preparation Guide

This guide is designed to help you explain this project clearly and confidently to an interviewer. It highlights the architecture, design choices, technical challenges you solved, and deep-dives into GStreamer and edge/server communication.

---

## 1. The Elevator Pitch (High-Level Overview)
> **"I built a distributed, low-latency video streaming and recording system designed for clinical/hospital environments (such as recording surgeries or patient sessions). The system consists of C++ GStreamer edge-node observers (deployed on hardware like NVIDIA Jetson Orin Nano and Raspberry Pis) that automatically register with a central C++ GStreamer server over UDP, stream high-definition H.264 video, and record them dynamically on the server as robust, instantly-playable MKV files while simultaneously multicasting the stream for low-overhead live viewing across the hospital network."**

---

## 2. System Architecture

```mermaid
graph TD
    subgraph Edge Nodes [Edge Nodes (e.g., Jetson Orin Nano, Raspberry Pi)]
        CSI[CSI Camera] -->|nvarguscamerasrc| JE[Jetson HW Encoder NVENC]
        USB[USB Camera] -->|v4l2src| SE[Software / V4L2 Encoder]
        JE --> MUX1[mpegtsmux]
        SE --> MUX1
        MUX1 -->|UDP Stream| NET((Network))
    end

    subgraph C++ Server [Central Server (C++ & GStreamer)]
        NET -->|UDP Stream on Port X| UDPS[udpsrc]
        UDPS --> DEMUX[tsdemux]
        DEMUX --> PARSE[h264parse]
        PARSE --> TEE{tee}
        
        %% Path 1: Disk Storage
        TEE -->|Queue 1| MKVMUX[matroskamux]
        MKVMUX -->|filesink| DISK[(Disk Storage)]
        
        %% Path 2: Multicast Live Feed
        TEE -->|Queue 2| MUX2[mpegtsmux]
        MUX2 -->|udpsink| MC[Multicast Groups e.g., 239.0.0.100:5000]
        
        %% Management Services
        USOD[UDP Discovery Port 5001] -.->|Assigns Port X| NET
        WS[Web Server Port 8080] <--->|Control Panel / APIs| HTML[Web Browser Control Panel]
    end
    
    %% Connections
    MC -->|Live Feed| VLC[VLC / HTML5 Players]
```

---

## 3. Core Tech Stack & Design Choices

| Tech / Component | What is Used | Why it was Chosen |
| :--- | :--- | :--- |
| **Language** | Modern C++ (C++17/20) | High performance, memory-efficient multithreading, and native integration with GStreamer and system APIs. |
| **Media Framework** | GStreamer (1.0) | The industry standard for low-latency pipeline building. Provides hardware acceleration wrapper plugins (like NVENC on Jetson) and robust media muxers out-of-the-box. |
| **Concurrency / Event Loop** | `GMainLoop` (GLib) | Handles single-threaded asynchronous networking and GStreamer state transitions, avoiding thread-spawning overhead. |
| **Multithreading Protection** | `std::shared_mutex` | Allows concurrent reads (e.g. checking session statuses) while protecting writes (e.g. registering nodes or starting/stopping streams) with a writer lock. |
| **Edge Hardware** | NVIDIA Jetson Orin Nano / Raspberry Pi | Highly cost-efficient edge platforms with hardware H.264 video encoders (NVENC and V4L2). |
| **Container Format** | MKV (Matroska) | Unlike MP4, MKV doesn't require index serialization at the end of the file to be playable. Combined with the `streamable=true` flag, files are playable even if the server crashes or power is lost. |
| **Live View Protocol** | UDP Multicast (MPEG-TS) | Distributes live video to unlimited client players on the network with zero additional CPU/network overhead on the server. |

---

## 4. Technical Challenges & Smart Solutions (Great Interview Stories!)

### Challenge 1: Avoid Corrupted / Non-Playable Video Files on Hard Crashes
* **The Problem:** Video recorders (like `mp4mux` or standard `matroskamux`) write metadata indexes at the end of recording. If the power cuts, the file is corrupted.
* **The Solution:** We configured the server's GStreamer recording pipeline with `matroskamux streamable=true` and configured the file queue with `async=false`. We also implement clean shutdown by injecting an `EOS` (End-Of-Stream) event and blocking for up to 3 seconds before tearing down the pipeline, allowing the muxer to write clean index indexes (cues) for perfect video seekability when shut down normally.

### Challenge 2: Spurious/Empty Recordings from Disconnected Cameras
* **The Problem:** If a camera node goes offline or fails to start streaming, the server might sit idle, creating empty `0-byte` files and consuming system sockets.
* **The Solution:** 
  1. **Startup Verification:** When a session starts, we block for up to 2 seconds and check if any buffers have arrived using GStreamer **Pad Probes** (`data_probe_cb` on `udpsrc`). If no buffers arrive, we immediately abort the session and delete the file.
  2. **Active Stream Health Monitoring:** A periodic GSource timeout runs every 1 second. If a camera node hasn't sent a buffer in 5 seconds (monitored via the Pad Probe timestamp), the server automatically shuts down the session, logs an error, and closes the GStreamer pipeline.

### Challenge 3: Disk I/O Overhead from Constant Disk Space Checks
* **The Problem:** Periodically checking disk space using `std::filesystem::space` is a blocking system call. Doing this too frequently under high-load streaming degrades performance.
* **The Solution:** Implemented **Dynamic Storage Monitoring**. If there are no sessions active, the timer checks disk space every 10 seconds. When streams are running, the server dynamically calculates the consumption rate (assuming 2MB/s per stream) and schedules the next check at half the time it would take to reach the 500MB safety threshold (bounded between 2 and 300 seconds).

---

## 5. C++ Server Code walkthrough

### A. Non-blocking Multi-protocol Event Loop (`main.cpp`)
The C++ server does not run heavy multi-threaded select/poll systems. Instead, it hooks the **UDP Discovery socket** (Port 5001) and **TCP Web socket** (Port 8080) directly into the GLib `GMainLoop` using `GIOChannel`. 
* When a node sends a UDP packet starting with `DISCOVER <NodeID>`, the server:
  1. Auto-assigns a dedicated port (starting at 5000) for that camera.
  2. Registers the camera node in the `SessionManager`.
  3. Sends back `ASSIGN <port>` to the client node.
* When a browser connects to port 8080, it serves a Control Panel (`index.html`) and responds to endpoints:
  - `/api/nodes`: JSON list of all active cameras and current recording sessions.
  - `/api/start?doc=<Name>&id=<CamID>`: Starts the GStreamer recording pipeline.
  - `/api/stop?doc=<Name>`: Stops the pipeline cleanly.

### B. Dynamic GStreamer Pipelines (`StreamEngine.cpp`)
The recording pipeline dynamically generates a GStreamer graph using `gst_parse_launch`:

```text
udpsrc port=<port> buffer-size=10000000 sync=false ! 
tsdemux ! 
h264parse config-interval=-1 ! 
tee name=t
  t. ! queue max-size-buffers=0 max-size-time=0 async=false ! matroskamux streamable=true ! filesink location=<path>.mkv sync=false
  t. ! queue ! mpegtsmux ! udpsink host=<multicast_ip> port=5000 auto-multicast=true sync=false
```

* **Pipeline Highlights for the Interviewer:**
  * **`buffer-size=10000000`** (10MB kernel socket buffer) avoids packet drops during high action.
  * **`sync=false`** on `udpsrc` prevents clock synchronization issues between the camera clock and server clock.
  * **`config-interval=-1`** on `h264parse` forces SPS/PPS sequence parameters to be sent on every keyframe. This allows a client (e.g. VLC player joining a multicast group late) to instantly decode and display the video stream instead of waiting for a random keyframe sequence.

---

## 6. Observer Edge Client walkthrough

The scripts under `/observer` are optimized for resource-constrained edge platforms. The flagship script is `observer_jetson.sh` targeting the **NVIDIA Jetson Orin Nano**.

### A. Hardware-Aware Codec Selection
The script inspects `/sys/firmware/devicetree/base/model` to identify the host system:
1. **NVIDIA Jetson**: Uses the dedicated hardware NVENC encoder (`nvv4l2h264enc`).
2. **Raspberry Pi 3/4**: Uses `v4l2h264enc` (Broadcom hardware H.264 encoder).
3. **Raspberry Pi 5 / Generic Linux**: Falls back to the software encoder (`x264enc`) optimized with `tune=zerolatency speed-preset=ultrafast` to minimize latency.

### B. CSI vs USB Camera Pipelines
* **CSI Cameras** (e.g., IMX219 connected directly to the Jetson MIPI port):
  * **Pipeline:** `nvarguscamerasrc ! video/x-raw(memory:NVMM),format=NV12 ! nvvidconv ! video/x-raw,format=I420`
  * *Why:* `nvarguscamerasrc` loads buffers directly into **NVIDIA NVMM hardware memory** (zero-copy ISP processing). We then convert the frame format using the Jetson hardware converter `nvvidconv` for H.264 compression.
* **USB Cameras**:
  * **Pipeline:** `v4l2src ! image/jpeg ! jpegdec ! videoconvert ! video/x-raw,format=I420`
  * *Why:* Decodes MJPEG frames using `jpegdec` and standardizes the color space.

---

## 7. Potential Interview Questions & Mock Answers

### Q: Why didn't you use RTSP (Real-Time Streaming Protocol) instead of raw UDP/MPEG-TS?
* **Answer:** *"RTSP requires a handshake connection setup (TCP control channel) and constant session negotiation which adds latency and protocol overhead. In a closed hospital network where cameras are fixed and dedicated, raw UDP over MPEG-TS provides the absolute lowest latency. Additionally, MPEG-TS encapsulates the H.264 stream natively with timing markers, making it highly robust against random packet drops and perfect for instant UDP multicasting."*

### Q: How do you handle frame drops or network congestion on the server side?
* **Answer:** *"First, we configure the `udpsrc` kernel buffer size to 10MB to handle traffic spikes. Second, we use `h264parse config-interval=-1` which constantly injects H.264 SPS/PPS meta-frames into the stream. If network congestion drops frames, as soon as the network clears, the decoder receives the metadata and recovers immediately, without waiting for the next Group of Pictures (GOP) boundary."*

### Q: What is the benefit of a single-threaded GMainLoop for networking and streaming?
* **Answer:** *"Spawning a thread per socket connection (like traditional blocking TCP socket servers) introduces scheduling overhead and race conditions that require heavy mutex locks. By integrating the UDP discovery and TCP HTTP sockets into the GLib/GStreamer `GMainLoop` via `GIOChannel`, the entire application runs reactively on a single thread. When a socket receives data, GMainLoop triggers the callback, processes it instantly, and returns, keeping the server lightweight, resource-efficient, and easy to debug."*

### Q: How would you scale this system to support 100+ cameras?
* **Answer:** *"To scale to 100+ cameras, the primary bottlenecks are CPU/GPU encoding (handled at the edge, so it scales naturally), network bandwidth (about 2-10Mbps per stream, which a 10Gbps hospital core switch can easily route), and disk I/O write speeds on the server. I would implement a Distributed Storage model where the GStreamer server writes files to a high-speed NVMe array or distributes writes across secondary network-attached storage nodes. I would also replace `gst_parse_launch` with a clean C API pipeline generator to manage GStreamer components programmatically."*
