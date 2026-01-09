#include <iostream>
#include <thread>
#include <map>
#include <sstream>
#include <fstream>
#include "SessionManager.hpp"
#include "VideoStorage.hpp"
#include "StreamEngine.hpp"
#include <cstring>
#include <algorithm>
#include "Logger.hpp"
#include <vector>
#include <queue>
#include <functional>
#include <condition_variable>
#include <csignal>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define close closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

// Global objects for simplified thread access
SessionManager sessionMgr;
VideoStorage storage("./recordings");
StreamEngine engine(storage);

// Map to store which port each camera is using (ID -> Port)
std::map<std::string, int> node_ports;
std::mutex node_ports_mutex;

void command_listener() {
    // Simple Console Interface to simulate API calls (Mobile/Fingerprint)
    std::string cmd;
    while (true) {
        std::cout << "\nCommands: [start <DocName> <Port>] [stop <DocName>] [list] [nodes] > ";
        std::cin >> cmd;

        if (cmd == "start") {
            std::string doc;
            int port;
            std::cin >> doc >> port;
            sessionMgr.start_session(doc);
            engine.start_recording(doc, port);
        } 
        else if (cmd == "stop") {
            std::string doc;
            std::cin >> doc;
            sessionMgr.stop_session();
            engine.stop_recording(doc);
        } 
        else if (cmd == "list") {
            auto files = storage.list_videos();
            std::cout << "--- Saved Videos ---" << std::endl;
            for (const auto& f : files) std::cout << f << std::endl;
        }
        else if (cmd == "nodes") {
            // Simulate adding a node (in real app, this comes from network discovery)
            sessionMgr.register_node("TV_Room_1", "192.168.1.50");
        }
        else if (cmd == "quit") {
            Logger::info("Shutting down server...");
            exit(0); // Force exit since engine.run() is blocking
        }
    }
}

void discovery_listener() {
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    char buffer[1024];

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) return;

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(5001); // Discovery Port

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        close(sockfd);
        return;
    }

    Logger::info("[Discovery] Listening on UDP 5001...");
    while (true) {
        socklen_t len = sizeof(cliaddr);
        // Use 0 instead of MSG_WAITALL for better cross-platform UDP compatibility
        int n = recvfrom(sockfd, (char *)buffer, 1024, 0, (struct sockaddr *)&cliaddr, &len);
        if (n > 0) {
            buffer[n] = '\0';
            std::string msg(buffer);
            // Expected format: "REGISTER <NAME> <PORT>"
            if (msg.rfind("REGISTER ", 0) == 0) {
                std::stringstream ss(msg.substr(9));
                std::string id;
                int port = 5000; // Default if not specified
                ss >> id;
                if (!(ss >> port)) port = 5000;

                id.erase(std::remove(id.begin(), id.end(), '\n'), id.end());
                
                std::lock_guard<std::mutex> lock(node_ports_mutex);
                node_ports[id] = port;
                
                sessionMgr.register_node(id, inet_ntoa(cliaddr.sin_addr));
            }
        }
    }
}

void handle_http_client(int new_socket) {
    char buffer[4096] = {0};
    recv(new_socket, buffer, 4096, 0); // recv works on both Windows and Linux
    std::string request(buffer);
    std::string response;

    // --- API: Get List of Nodes ---
    if (request.find("GET /api/nodes") != std::string::npos) {
        auto nodes = sessionMgr.get_active_observers();
        std::stringstream json;
        json << "[";
        for (size_t i = 0; i < nodes.size(); ++i) {
            json << "{\"id\":\"" << nodes[i].id << "\", \"ip\":\"" << nodes[i].ip_address << "\"}";
            if (i < nodes.size() - 1) json << ",";
        }
        json << "]";
        std::string body = json.str();
        response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.length()) + "\r\n\r\n" + body;
    }
    // --- API: Start Recording ---
    else if (request.find("GET /api/start") != std::string::npos) {
        // Parse: /api/start?doc=Name&id=CameraID
        std::string doc = "Unknown";
        std::string cam_id = "";
        int port = -1; // Default to invalid to ensure we find a registered camera
        
        size_t docPos = request.find("doc=");
        if (docPos != std::string::npos) {
            size_t end = request.find("&", docPos);
            if (end == std::string::npos) end = request.find(" ", docPos);
            doc = request.substr(docPos + 4, end - (docPos + 4));
        }
        size_t idPos = request.find("id=");
        if (idPos != std::string::npos) {
            size_t end = request.find(" ", idPos);
            cam_id = request.substr(idPos + 3, end - (idPos + 3));
            std::lock_guard<std::mutex> lock(node_ports_mutex);
            if (node_ports.count(cam_id)) port = node_ports[cam_id];
        }

        if (port != -1) {
            sessionMgr.start_session(doc);
            engine.start_recording(doc, port);
            response = "HTTP/1.1 200 OK\r\n\r\nStarted";
        } else {
            Logger::error("Web API: Failed to start. Camera ID '" + cam_id + "' not found.");
            response = "HTTP/1.1 400 Bad Request\r\n\r\nError: Camera not found. Please refresh list.";
        }
    }
    // --- API: Stop Recording ---
    else if (request.find("GET /api/stop") != std::string::npos) {
        std::string doc = "Unknown";
        size_t docPos = request.find("doc=");
        if (docPos != std::string::npos) {
            size_t end = request.find(" ", docPos);
            doc = request.substr(docPos + 4, end - (docPos + 4));
        }

        if (sessionMgr.is_recording_active()) {
            sessionMgr.stop_session();
            engine.stop_recording(doc);
            response = "HTTP/1.1 200 OK\r\n\r\nStopped";
        } else {
            response = "HTTP/1.1 400 Bad Request\r\n\r\nError: No active recording to stop.";
        }
    }
    // --- Serve HTML Page ---
    else {
        std::ifstream file("index.html");
        if (!file.is_open()) {
            // Fallback: Try parent directory (useful if running from build/ folder)
            file.open("../index.html");
        }

        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string html = buffer.str();
            response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " + std::to_string(html.length()) + "\r\n\r\n" + html;
        } else {
            std::string msg = "404 Not Found: index.html missing. Please place it next to the executable.";
            response = "HTTP/1.1 404 Not Found\r\nContent-Length: " + std::to_string(msg.length()) + "\r\n\r\n" + msg;
        }
    }

    send(new_socket, response.c_str(), response.length(), 0);
    close(new_socket);
}

// Simple ThreadPool to prevent creating too many threads
class ThreadPool {
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop = false;
public:
    ThreadPool(size_t threads) {
        for(size_t i = 0; i < threads; ++i)
            workers.emplace_back([this] {
                while(true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this]{ return this->stop || !this->tasks.empty(); });
                        if(this->stop && this->tasks.empty()) return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
    }
    ~ThreadPool() {
        { std::unique_lock<std::mutex> lock(queue_mutex); stop = true; }
        condition.notify_all();
        for(std::thread &worker: workers) worker.join();
    }
    void enqueue(std::function<void()> task) {
        { std::unique_lock<std::mutex> lock(queue_mutex); tasks.push(task); }
        condition.notify_one();
    }
};

void web_server() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Create TCP Socket for HTTP
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) return;

    // Force attach to port 8080
    #ifdef _WIN32
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    #else
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
    #endif
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        Logger::error("[Web] Error: Could not bind to port 8080. Is it already in use?");
        return;
    }
    if (listen(server_fd, 3) < 0) {
        Logger::error("[Web] Error: Could not listen on port 8080.");
        return;
    }

    Logger::info("[Web] Control Panel running at http://<server_ip>:8080");

    // Auto-detect CPU cores to size the pool efficiently (Min 4 threads)
    unsigned int cores = std::thread::hardware_concurrency();
    ThreadPool pool(cores > 0 ? cores : 4);

    while (true) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) continue;

        pool.enqueue([new_socket] { handle_http_client(new_socket); });
    }
}

int main() {
    Logger::info("--- Hospital Video Server Starting ---");

    #ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        Logger::error("WSAStartup failed");
        return 1;
    }
    #endif

    // Prevent server crash on Linux if a client disconnects abruptly
    #ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
    #endif

    // 1. Initialize Engine
    engine.init();

    // 2. Start Command Listener (Simulating the API Thread)
    std::thread api_thread(command_listener);

    // 3. Start Discovery Listener
    std::thread discovery_thread(discovery_listener);
    discovery_thread.detach();

    // 4. Start Web Server (Port 8080)
    std::thread web_thread(web_server);
    web_thread.detach();

    // 5. Start RTSP Loop (Blocking)
    engine.run();

    if (api_thread.joinable()) api_thread.join();
    return 0;
}