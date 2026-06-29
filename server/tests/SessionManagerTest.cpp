#include <gtest/gtest.h>
#include "../SessionManager.hpp"

TEST(SessionManagerTest, InitialSessionState) {
    SessionManager manager;
    EXPECT_FALSE(manager.is_recording_active());
    EXPECT_EQ(manager.get_current_doctor(), "NONE");
}

TEST(SessionManagerTest, StartAndStopSession) {
    SessionManager manager;
    manager.start_session("DrSmith");
    
    EXPECT_TRUE(manager.is_recording_active());
    EXPECT_EQ(manager.get_current_doctor(), "DrSmith");
    
    manager.stop_session();
    EXPECT_FALSE(manager.is_recording_active());
    EXPECT_EQ(manager.get_current_doctor(), "NONE");
}

TEST(SessionManagerTest, NodeRegistration) {
    SessionManager manager;
    manager.register_node("Camera1", "192.168.1.100");
    
    auto nodes = manager.get_active_observers();
    ASSERT_EQ(nodes.size(), 1);
    EXPECT_EQ(nodes[0].first, "Camera1");
    EXPECT_EQ(nodes[0].second.ip_address, "192.168.1.100");
    EXPECT_TRUE(nodes[0].second.is_online);
    
    // Assign session to node
    manager.set_session_for_node("Camera1", "Surgery_A");
    auto nodes_updated = manager.get_active_observers();
    EXPECT_EQ(nodes_updated[0].second.session_name, "Surgery_A");
}
