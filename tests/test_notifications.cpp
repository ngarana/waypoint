// test_notifications.cpp - Notification monitor/log.
// Split verbatim from tests/unit_tests.cpp; bodies unchanged.
#include "test_framework.hpp"

TEST(NotificationMonitor) {
    qypr::EventLoop loop;
    qypr::NotificationMonitor mon(loop);

    EXPECT_TRUE(mon.start(false));
    EXPECT_TRUE(mon.notifications().empty());

    // Direct invocation to get coverage on teardown and other internal calls
    mon.teardown();
}
TEST(NotificationLog) {
    qypr::EventLoop loop;
    qypr::NotificationMonitor const mon(loop);
    qypr::NotificationLog log(loop, mon);

    EXPECT_TRUE(log.start());
    log.drain();
}
