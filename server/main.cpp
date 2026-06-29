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
#include <csignal>
#include <string_view>
#include <mutex>
#include <shared_mutex>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define close closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <ifaddrs.h>
    #include <netdb.h>
    #include <fcntl.h>
#endif

// Include glib for event loop integration
#include <glib.h>

// ServerContext encapsulates all the global state efficiently
struct ServerContext {
    SessionManager sessionMgr;
    VideoStorage storage;
    StreamEngine engine;
    
    std::shared_mutex node_ports_mutex;
    std::unordered_map<std::string, int> node_ports;
    int next_port = 5000; // Start assigning ports from 5000

    ServerContext() : storage("./recordings"), engine(storage) {}
};

// ----------------------------------------------------
// Console Commands (Running in separate thread since 
// std::cin.operator>> blocks heavily for user interaction)
// ----------------------------------------------------
void command_listener(ServerContext* ctx) {
    std::string cmd;
    while (true) {
        std::cout << "\nCommands: [start <DocName> <Port>] [stop <DocName>] [list] [nodes] > ";
        std::cin >> cmd;

        if (cmd == "start") {
            std::string doc;
            int port;
            std::cin >> doc >> port;
            ctx->sessionMgr.start_session(doc);
            ctx->engine.start_recording(doc, port);
        } 
        else if (cmd == "stop") {
            std::string doc;
            std::cin >> doc;
            ctx->sessionMgr.stop_session();
            ctx->engine.stop_recording(doc);
        } 
        else if (cmd == "list") {
            auto files = ctx->storage.list_videos();
            std::cout << "--- Saved Videos ---" << std::endl;
            for (const auto& f : files) std::cout << f << std::endl;
        }
        else if (cmd == "nodes") {
            ctx->sessionMgr.register_node("TV_Room_1", "192.168.1.50");
        }
        else if (cmd == "quit") {
            Logger::info("Shutting down server...");
            exit(0);
        }
    }
}

// ----------------------------------------------------
// Discovery UDP Callbacks (GMainLoop integrated)
// ----------------------------------------------------
gboolean discovery_cb(GIOChannel *source, GIOCondition condition, gpointer data) {
    ServerContext* ctx = static_cast<ServerContext*>(data);
    int sockfd = g_io_channel_unix_get_fd(source);
    
    struct sockaddr_in cliaddr;
    socklen_t len = sizeof(cliaddr);
    char buffer[1024];

    int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr *)&cliaddr, &len);
    if (n > 0) {
        std::string_view msg(buffer, n);
        if (msg.rfind("DISCOVER ", 0) == 0) {
            std::string id(msg.substr(9));
            id.erase(std::remove(id.begin(), id.end(), '\n'), id.end());
            id.erase(std::remove(id.begin(), id.end(), '\r'), id.end());

            int assigned_port = 0;
            {
                std::unique_lock<std::shared_mutex> lock(ctx->node_ports_mutex);
                if (ctx->node_ports.count(id)) {
                    assigned_port = ctx->node_ports[id];
                } else {
                    assigned_port = ctx->next_port++;
                    ctx->node_ports[id] = assigned_port;
                    Logger::info("[Discovery] Assigned Port " + std::to_string(assigned_port) + " to " + id);
                }
            }
            
            ctx->sessionMgr.register_node(id, inet_ntoa(cliaddr.sin_addr));
            std::string reply = "ASSIGN " + std::to_string(assigned_port);
            sendto(sockfd, reply.c_str(), reply.length(), 0, (const struct sockaddr *)&cliaddr, len);
        }
    }
    return TRUE; // keep watching
}

// ----------------------------------------------------
// Web API Callbacks (GMainLoop integrated)
// ----------------------------------------------------
gboolean client_cb(GIOChannel *source, GIOCondition condition, gpointer data) {
    ServerContext* ctx = static_cast<ServerContext*>(data);
    int new_socket = g_io_channel_unix_get_fd(source);
    
    char buffer[4096] = {0};
    int bytes_read = recv(new_socket, buffer, sizeof(buffer), 0);
    
    if (bytes_read > 0) {
        std::string_view request(buffer, bytes_read);
        std::string response;

        // --- API: Get List of Nodes ---
        if (request.find("GET /api/nodes") != std::string_view::npos) {
            auto nodes = ctx->sessionMgr.get_active_observers();
            std::stringstream json;
            json << "[";
            for (size_t i = 0; i < nodes.size(); ++i) {
                std::string mcast = ctx->engine.get_active_multicast_ip(nodes[i].second.session_name);
                json << "{\"id\":\"" << nodes[i].first 
                     << "\", \"ip\":\"" << nodes[i].second.ip_address 
                     << "\", \"session\":\"" << nodes[i].second.session_name 
                     << "\", \"multicast\":\"" << mcast << "\"}";
                if (i < nodes.size() - 1) json << ",";
            }
            json << "]";
            std::string body = json.str();
            response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.length()) + "\r\n\r\n" + body;
        }
        // --- API: Start Recording ---
        else if (request.find("GET /api/start") != std::string_view::npos) {
            std::string doc = "Unknown";
            std::string cam_id = "";
            int port = -1;
            
            size_t docPos = request.find("doc=");
            if (docPos != std::string_view::npos) {
                size_t end = request.find("&", docPos);
                if (end == std::string_view::npos) end = request.find(" ", docPos);
                doc = std::string(request.substr(docPos + 4, end - (docPos + 4)));
            }
            size_t idPos = request.find("id=");
            if (idPos != std::string_view::npos) {
                size_t end = request.find(" ", idPos);
                cam_id = std::string(request.substr(idPos + 3, end - (idPos + 3)));
                
                std::shared_lock<std::shared_mutex> lock(ctx->node_ports_mutex);
                if (ctx->node_ports.count(cam_id)) port = ctx->node_ports[cam_id];
            }

            if (port != -1) {
                ctx->sessionMgr.start_session(doc);
                ctx->sessionMgr.set_session_for_node(cam_id, doc);
                ctx->engine.start_recording(doc, port);
                response = "HTTP/1.1 200 OK\r\n\r\nStarted";
            } else {
                Logger::error("Web API: Failed to start. Camera ID '" + cam_id + "' not found.");
                response = "HTTP/1.1 400 Bad Request\r\n\r\nError: Camera not found. Please refresh list.";
            }
        }
        // --- API: Stop Recording ---
        else if (request.find("GET /api/stop") != std::string_view::npos) {
            std::string doc = "Unknown";
            size_t docPos = request.find("doc=");
            if (docPos != std::string_view::npos) {
                size_t end = request.find(" ", docPos);
                doc = std::string(request.substr(docPos + 4, end - (docPos + 4)));
            }

            if (ctx->sessionMgr.is_recording_active()) {
                ctx->sessionMgr.stop_session();
                ctx->engine.stop_recording(doc);
                response = "HTTP/1.1 200 OK\r\n\r\nStopped";
            } else {
                response = "HTTP/1.1 400 Bad Request\r\n\r\nError: No active recording to stop.";
            }
        }
        // --- Serve HTML Page ---
        else {
            std::ifstream file("index.html");
            if (!file.is_open()) file.open("../index.html");

            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                std::string html = buffer.str();
                response = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: " + std::to_string(html.length()) + "\r\n\r\n" + html;
            } else {
                std::string msg = "404 Not Found: index.html missing.";
                response = "HTTP/1.1 404 Not Found\r\nContent-Length: " + std::to_string(msg.length()) + "\r\n\r\n" + msg;
            }
        }

        send(new_socket, response.c_str(), response.length(), 0);
    }
    
    close(new_socket);
    return FALSE; // Destroy the GIOChannel after handling the request
}

gboolean web_server_cb(GIOChannel *source, GIOCondition condition, gpointer data) {
    ServerContext* ctx = static_cast<ServerContext*>(data);
    int server_fd = g_io_channel_unix_get_fd(source);
    
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
    
    if (new_socket >= 0) {
        GIOChannel* client_channel = g_io_channel_unix_new(new_socket);
        g_io_add_watch(client_channel, G_IO_IN, client_cb, ctx);
        g_io_channel_unref(client_channel);
    }
    return TRUE; // keep listening
}

// ----------------------------------------------------
// Network Initialization
// ----------------------------------------------------
void init_network_listeners(ServerContext* ctx) {
    // 1. Setup UDP Discovery
    int udp_fd;
    struct sockaddr_in servaddr;
    if ((udp_fd = socket(AF_INET, SOCK_DGRAM, 0)) >= 0) {
        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_addr.s_addr = INADDR_ANY;
        servaddr.sin_port = htons(5001);
        if (bind(udp_fd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) == 0) {
            Logger::info("[Discovery] Listening on UDP 5001 mapped to GMainLoop...");
            GIOChannel* udp_channel = g_io_channel_unix_new(udp_fd);
            g_io_add_watch(udp_channel, G_IO_IN, discovery_cb, ctx);
            g_io_channel_unref(udp_channel);
        }
    }

    // 2. Setup TCP Web Server
    int tcp_fd;
    struct sockaddr_in address;
    int opt = 1;
    if ((tcp_fd = socket(AF_INET, SOCK_STREAM, 0)) >= 0) {
        #ifdef _WIN32
            setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
        #else
            setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));
            fcntl(tcp_fd, F_SETFL, O_NONBLOCK);
        #endif
        
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(8080);
        
        if (bind(tcp_fd, (struct sockaddr *)&address, sizeof(address)) == 0 && listen(tcp_fd, 20) == 0) {
            Logger::info("[Web] Control Panel running at port 8080 mapped to GMainLoop...");
            GIOChannel* tcp_channel = g_io_channel_unix_new(tcp_fd);
            g_io_add_watch(tcp_channel, G_IO_IN, web_server_cb, ctx);
            g_io_channel_unref(tcp_channel);
        } else {
            Logger::error("[Web] Error: Could not bind to port 8080.");
        }
    }
}

// ----------------------------------------------------
void log_available_ips() {
    #ifndef _WIN32
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) return;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        if (ifa->ifa_addr->sa_family == AF_INET) {
            char host[NI_MAXHOST];
            if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) == 0) {
                if (std::string(host) != "127.0.0.1") {
                    Logger::info("[Network] Detected IP: " + std::string(host));
                    Logger::info("[Network] Web URL: http://" + std::string(host) + ":8080");
                }
            }
        }
    }
    freeifaddrs(ifaddr);
    #endif
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

    #ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
    #endif

    log_available_ips();

    // Context allocation replaces globals
    ServerContext ctx;
    ctx.engine.init();

    // Map Networking onto GMainLoop
    init_network_listeners(&ctx);

    // API Thread for CLI
    std::thread api_thread(command_listener, &ctx);

    // Enter GMainLoop, blocking execution efficiently on a single thread
    ctx.engine.run();

    if (api_thread.joinable()) api_thread.join();
    return 0;
}