#include "alert.hpp"
#include "Logger.hpp"
#include <iostream>

namespace Alert {
    void out_of_space_alert(const std::string& message) {
        Logger::error(message);
        std::cerr << "CRITICAL ALERT: " << message << std::endl;
        // In the future this could trigger SMS or UI triggers
    }
}
