#include <gtest/gtest.h>
#include "tools/KinectDataViewer.h"
#include <thread>
#include <chrono>
#include <memory>

using caldera::backend::tools::KinectDataViewer;

class KinectDataViewerTest : public ::testing::Test {
protected:
    void SetUp() override {
        viewer = std::make_unique<KinectDataViewer>();
    }

    void TearDown() override {
        if (viewer->isRunning()) {
            viewer->stop();
        }
        viewer.reset();
    }

    std::unique_ptr<KinectDataViewer> viewer;
};

TEST_F(KinectDataViewerTest, ConstructorCreateValidViewer) {
    EXPECT_FALSE(viewer->isRunning());
}

TEST_F(KinectDataViewerTest, StartStopCycle) {
    // This test will skip if no physical device
    bool started = viewer->start();
    
    if (!started) {
        GTEST_SKIP() << "No Kinect V2 device available for testing";
    }
    
    EXPECT_TRUE(viewer->isRunning());
    
    // Let it run briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    viewer->stop();
    EXPECT_FALSE(viewer->isRunning());
}

TEST_F(KinectDataViewerTest, DISABLED_ShortDataCapture) {
    // Disabled by default - requires physical device and takes time
    bool started = viewer->start();
    
    if (!started) {
        GTEST_SKIP() << "No Kinect V2 device available";
    }
    
    // Run for 2 seconds and verify no crashes
    viewer->runFor(2);
    
    EXPECT_FALSE(viewer->isRunning()); // Should have stopped automatically
}