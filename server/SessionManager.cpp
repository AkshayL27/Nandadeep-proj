#include "SessionManager.hpp"
#include "Logger.hpp"

SessionManager::SessionManager() : recording_active(false) {}

bool SessionManager::is_recording_active() {
    std::lock_guard<std::mutex> lock(session_mutex);
    return recording_active;
}

void SessionManager::start_session(const std::string& doctor_name) {
    std::lock_guard<std::mutex> lock(session_mutex);
    current_doctor = doctor_name;
    recording_active = true;
    Logger::info("[Session] Started for Doctor: " + doctor_name);
}

void SessionManager::stop_session() {
    std::lock_guard<std::mutex> lock(session_mutex);
    recording_active = false;
    current_doctor = "";
    Logger::info("[Session] Stopped.");
}

std::string SessionManager::get_current_doctor() {
    std::lock_guard<std::mutex> lock(session_mutex);
    return current_doctor;
}

void SessionManager::register_node(const std::string& id, const std::string& ip) {
    std::lock_guard<std::mutex> lock(session_mutex);
    
    // Check for duplicates to prevent spamming
    for (const auto& node : observers) {
        if (node.id == id && node.ip_address == ip) return;
    }

    observers.push_back({id, ip, true});
    Logger::info("[Node] Registered: " + id + " (" + ip + ")");
}

std::vector<ObserverNode> SessionManager::get_active_observers() {
    std::lock_guard<std::mutex> lock(session_mutex);
    return observers; 
}