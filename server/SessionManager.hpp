#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <iostream>

struct ObserverNode {
    std::string id;
    std::string ip_address;
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
    std::vector<ObserverNode> get_active_observers();

private:
    std::mutex session_mutex;
    std::string current_doctor;
    bool recording_active;
    std::vector<ObserverNode> observers;
};