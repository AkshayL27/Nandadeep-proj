#pragma once
#include <string>
#include <vector>
#include <shared_mutex>
#include <mutex>
#include <unordered_map>
#include <iostream>

struct ObserverNode {
    std::string ip_address;
    std::string session_name; // Track which doctor/session this camera is recording for
    bool is_online;
};

class SessionManager {
public:
    SessionManager();
    
    // Returns true if a session is currently active (Doctor logged in)
    bool is_recording_active();
    
    void start_session(const std::string& doctor_name);
    void stop_session();
    std::string get_current_doctor();

    // Node Management
    void register_node(const std::string& id, const std::string& ip);
    void set_session_for_node(const std::string& node_id, const std::string& session_name);
    std::vector<std::pair<std::string, ObserverNode>> get_active_observers();

private:
    mutable std::shared_mutex session_mutex;
    std::string current_doctor;
    bool recording_active;
    std::unordered_map<std::string, ObserverNode> observers;
};