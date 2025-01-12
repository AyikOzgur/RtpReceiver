#include "RtpReceiver.h"
#include "RtpReceiverVersion.h"

RtpReceiver::~RtpReceiver()
{
}

std::string RtpReceiver::getVersion()
{
    return RTP_RECEIVER_VERSION;
}

bool RtpReceiver::init(std::string ip, int port)
{
    return false;
}

bool RtpReceiver::getFrame(cr::video::Frame& frame)
{
    return false;
}