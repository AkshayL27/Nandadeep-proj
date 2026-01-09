#pragma once
#include <iostream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <sstream>

class Logger {
public:
    enum Level { INFO, ERR };

    static void log(Level level, const std::string& message) {
        std::lock_guard<std::mutex> lock(log_mutex);
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream ss;
        ss << "[" << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << "] ";
        ss << (level == INFO ? "[INFO] " : "[ERROR] ");
        ss << message;
        
        std::string log_str = ss.str();
        
        // Output to console
        if (level == ERR) std::cerr << log_str << std::endl;
        else std::cout << log_str << std::endl;
        
        // Output to file
        std::ofstream outfile("server.log", std::ios_base::app);
        if (outfile.is_open()) {
            outfile << log_str << std::endl;
        }
    }
    
    static void info(const std::string& message) { log(INFO, message); }
    static void error(const std::string& message) { log(ERR, message); }

private:
    static inline std::mutex log_mutex;
};
