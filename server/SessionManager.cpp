#include "SessionManager.hpp"
#include "Logger.hpp"

SessionManager::SessionManager() : recording_active(false) {}

bool SessionManager::is_recording_active() {
    std::shared_lock<std::shared_mutex> lock(session_mutex);
    return recording_active;
}

void SessionManager::start_session(const std::string& doctor_name) {
    std::unique_lock<std::shared_mutex> lock(session_mutex);
    current_doctor = doctor_name;
    recording_active = true;
    Logger::info("[Session] Started for Doctor: " + doctor_name);
}

void SessionManager::set_session_for_node(const std::string& node_id, const std::string& session_name) {
    std::unique_lock<std::shared_mutex> lock(session_mutex);
    auto it = observers.find(node_id);
    if (it != observers.end()) {
        it->second.session_name = session_name;
    }
}

void SessionManager::stop_session() {
    std::unique_lock<std::shared_mutex> lock(session_mutex);
    recording_active = false;
    current_doctor = "";
    Logger::info("[Session] Stopped.");
}

std::string SessionManager::get_current_doctor() {
    std::shared_lock<std::shared_mutex> lock(session_mutex);
    return current_doctor;
}

void SessionManager::register_node(const std::string& id, const std::string& ip) {
    std::unique_lock<std::shared_mutex> lock(session_mutex);
    
    // Check for duplicates to prevent spamming
    if (observers.count(id) && observers[id].ip_address == ip) return;

    observers[id] = {ip, "", true};
    Logger::info("[Node] Registered: " + id + " (" + ip + ")");
}

std::vector<std::pair<std::string, ObserverNode>> SessionManager::get_active_observers() {
    std::shared_lock<std::shared_mutex> lock(session_mutex);
    return std::vector<std::pair<std::string, ObserverNode>>(observers.begin(), observers.end()); 
}