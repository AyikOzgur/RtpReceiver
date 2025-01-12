#include <iostream>
#include <thread>
#include <opencv2/opencv.hpp>
#include "RtpReceiver.h"
#include "VideoCodec.h"


void rtpSenderThreadFunc();

int main()
{
    std::cout << "==============================" << std::endl;
    std::cout << "RtpReceiver v" << RtpReceiver::getVersion() << " test" << std::endl;
    std::cout << "==============================" << std::endl;

    // Start rtp sender thread
    std::thread rtpSenderThread(rtpSenderThreadFunc);
    rtpSenderThread.detach();

    RtpReceiver rtpReceiver;
    VideoCodec videoCodec;

    cr::video::Frame receivedFrame(640, 480, cr::video::Fourcc::H264);
    cr::video::Frame decodedFrame(640, 480, cr::video::Fourcc::RGB24);

    while(true)
    {
        if (!rtpReceiver.getFrame(receivedFrame))
        {
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
        cv::waitKey(1);
    }

    return 0;
}

void rtpSenderThreadFunc()
{
    while (true)
    {
        // Prepare rtp packet and send it
    }
}