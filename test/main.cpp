#include <iostream>
#include <thread>
#include <opencv2/opencv.hpp>
#include "RtpReceiver.h"
#include "VideoCodec.h"


void rtpSenderThreadFunc();
std::atomic<bool> stopThread(false);

int main()
{
    std::cout << "==============================" << std::endl;
    std::cout << "RtpReceiver v" << RtpReceiver::getVersion() << " test" << std::endl;
    std::cout << "==============================" << std::endl;

    // Start rtp sender thread
    std::thread rtpSenderThread(rtpSenderThreadFunc);
    rtpSenderThread.detach();

    RtpReceiver rtpReceiver;
    if (!rtpReceiver.init("127.0.0.1", 5004))
    {
        std::cout << "Failed to initialize rtp receiver" << std::endl;
        return -1;
    }

    VideoCodec videoCodec;

    cr::video::Frame receivedFrame;
    cr::video::Frame decodedFrame;
    decodedFrame.fourcc = cr::video::Fourcc::BGR24;

    while(true)
    {
        if (!rtpReceiver.getFrame(receivedFrame))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::cout << "Failed to get frame from rtp stream" << std::endl;
            continue;
        }

        if (!videoCodec.decode(receivedFrame, decodedFrame))
        {
            std::cout << "Failed to decode frame" << std::endl;
            continue;
        }

        // Display decoded frame
        cv::Mat mat(decodedFrame.height, decodedFrame.width, CV_8UC3, decodedFrame.data);
        cv::imshow("Decoded frame", mat);
        if (cv::waitKey(1) == 27)
        {
            stopThread.store(true);
            break;
        }
    }

    std::cout << "Exiting..." << std::endl;
    rtpReceiver.close();

    // Join rtp sender thread
    if (rtpSenderThread.joinable())
        rtpSenderThread.join();

    return 0;
}

void rtpSenderThreadFunc()
{
    // Open video file
    std::string inputFile = "../../test/test.mp4";
    cv::VideoCapture cap(inputFile);
    
    if (!cap.isOpened()) 
    {
        std::cerr << "Error: Could not open video file " << inputFile << std::endl;
        exit(-1);
    }

    // Get video properties
    int width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    int height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = cap.get(cv::CAP_PROP_FPS);

    // GStreamer pipeline for RTP streaming
    std::string pipeline = "appsrc ! "
                           "videoconvert !  video/x-raw,format=I420,width=" + std::to_string(width) +
                           ",height=" + std::to_string(height) + //",framerate=" + std::to_string(int(fps)) + "/1 ! "
                           " ! x264enc bitrate=3000 sliced-threads=false threads=1 key-int-max=30 speed-preset=ultrafast tune=zerolatency ! "
                           "rtph264pay config-interval=1 pt=96 ! "
                           "udpsink host=127.0.0.1 port=5004";

    // Open the video writer with the GStreamer pipeline
    cv::VideoWriter writer(pipeline, cv::CAP_GSTREAMER, 0, fps, cv::Size(width, height), true);
    
    if (!writer.isOpened()) 
    {
        std::cerr << "Error: Could not open video writer with GStreamer pipeline" << std::endl;
        exit(-1);
    }

    std::cout << "Streaming " << inputFile << " over RTP..." << std::endl;

    cv::Mat frame;
    while (!stopThread.load())
    {
        // Read frame from video file
        cap >> frame;
        if (frame.empty()) 
        {
            cap.set(cv::CAP_PROP_POS_FRAMES, 0);
            std::cout << "End of video file, restarting..." << std::endl;
            continue;
        }

        // Write frame to GStreamer pipeline
        writer.write(frame);
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    // Cleanup
    cap.release();
    writer.release();
}