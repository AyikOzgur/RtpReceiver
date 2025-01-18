#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "H264Parser.h"
#include "RtpReceiver.h"
#include "RtpReceiverVersion.h"


#define RTP_HEADER_SIZE         12
#define RTP_VERSION             2

struct RtpHeader 
{
    uint8_t byte1{0};
    uint8_t byte2{0};
    uint16_t seq;
    uint32_t ts;
    uint32_t ssrc;
}__attribute__((packed));



RtpReceiver::~RtpReceiver()
{
    close();
}

std::string RtpReceiver::getVersion()
{
    return RTP_RECEIVER_VERSION;
}

bool RtpReceiver::init(std::string ip, int port)
{
    if (!initSocket(ip, port))
        return false;

    m_stopThread.store(false);
    if (!m_receiveThread.joinable())
        m_receiveThread = std::thread(&RtpReceiver::receiveThreadFunc, this);

    m_init = true;
    return true;
}

bool RtpReceiver::getFrame(cr::video::Frame& frame)
{
    if (!m_init)
        return false;

    // Check if there is a new frame.
    std::unique_lock lk(m_condVarMtx);
    if (!m_condVarFlag.load())
    {
        while (!m_condVarFlag.load())
        {
            m_condVar.wait(lk);
        }
    }

    // Copy frame.
    m_receivedFrameMutex.lock();
    frame = m_receivedFrame;
    m_receivedFrameMutex.unlock();

    // Reset flag.
    m_condVarFlag.store(false);

    return true;
}

void RtpReceiver::close()
{
    m_stopThread.store(true);
    // Wake thread up in case it is waiting.
    std::unique_lock lk(m_condVarMtx);
    m_condVarFlag.store(true);
    m_condVar.notify_one();
    lk.unlock();
    if (m_receiveThread.joinable())
        m_receiveThread.join();

    if (m_socket != -1)
        ::close(m_socket);
}

bool RtpReceiver::initSocket(std::string ip, int port)
{
    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == -1)
    {
        return false;
    }

    struct sockaddr_in hostAddr;
    memset(&hostAddr, 0, sizeof(sockaddr_in));
    hostAddr.sin_family = AF_INET;
    if (!inet_pton(AF_INET, ip.c_str(), &hostAddr.sin_addr))
        return false;
    hostAddr.sin_port = htons(port);

    if (::bind(m_socket, (struct sockaddr*)&hostAddr, sizeof(hostAddr)) < 0)
    {
        ::close(m_socket);
        return false;
    }

    int timeoutMsec = 1000;
    timeval timeparams;
    timeparams.tv_sec = timeoutMsec / 1000;
    timeparams.tv_usec = timeoutMsec % 1000;
    if (timeoutMsec != 0)
    {
        // Close socket in case error
        if (setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO,
                       (const char*)&timeparams, sizeof(timeval)) < 0)
        {
            ::close(m_socket);
            return false;
        }
    }

    return true;
}

int RtpReceiver::receiveUdpData(uint8_t* buffer, int bufferSize)
{
    int receivedSize = recvfrom(m_socket, buffer, bufferSize, 0, NULL, NULL);
    return receivedSize;
}

void RtpReceiver::receiveThreadFunc()
{
    constexpr int bufferSize = 1280 * 720 * 3;
    uint8_t *buffer = new uint8_t[bufferSize]; // Enough for 720p frame
    uint8_t *frameBuffer = new uint8_t[bufferSize]; // Enough for 720p frame
    int receivedFrameSize = 0;

    uint8_t *sps = new uint8_t[1024];
    uint8_t *pps = new uint8_t[1024];
    int spsSize = 0;
    int ppsSize = 0;

    uint8_t startCode[] = {0, 0, 1};

    int width{0}, height{0};

    while (!m_stopThread.load())
    {
        // Handle receiving data and prepare Rtp packet.
        int receivedPacketSize = receiveUdpData(buffer, bufferSize);
        if (receivedPacketSize <= 0)
            continue;
        //std::cout << "Received packet size: " << receivedPacketSize << std::endl;

        // Check if it is RTP packet.
        if (receivedPacketSize < RTP_HEADER_SIZE)
            continue;

        RtpHeader* rtpHeader = reinterpret_cast<RtpHeader*>(buffer);

        // Check if it is RTP packet.
        uint8_t version = (rtpHeader->byte1 & 0xC0) >> 6;
        if (version != RTP_VERSION)
            continue;

        // Check if it is last packet. 1 is last, 0 is not last.
        uint8_t last = (rtpHeader->byte2 & 0x80) >> 7;
        //std::cout << "Last: " << (int)last << std::endl;

        // Get payload type.
        uint8_t type = rtpHeader->byte2 & 0x7F;
        if (type != 96) // H264
            continue;

        // Get sequence number.
        uint16_t seq = ntohs(rtpHeader->seq);
        uint8_t* payload = buffer + RTP_HEADER_SIZE; // After header

        // Get payload size.
        int payloadSize = receivedPacketSize - RTP_HEADER_SIZE;

        //std::cout << "Seq: " << seq << " Payload size: " << payloadSize << std::endl;

        // Check nal type.
        uint8_t nalType = payload[0] & 0x1F;
        // Check sps or pps.
        if (nalType == 7)
        {
            H264Parser::parseSps(payload, payloadSize, width, height);

            if (width > 0 && height > 0)
            {
                m_receivedFrameMutex.lock();
                if (m_receivedFrame.width != width || m_receivedFrame.height != height)
                {
                    // Reset frame.
                    m_receivedFrame.release();
                    m_receivedFrame = cr::video::Frame(width, height, cr::video::Fourcc::H264);
                }
                m_receivedFrameMutex.unlock();
            }

            //std::cout << "SPS" << std::endl;
            spsSize = payloadSize;
            memcpy(sps, payload, spsSize);
            continue;
    
            // Copy start code and sps.
            int pos = 0;
            memcpy(frameBuffer, startCode, sizeof(startCode));
            pos += sizeof(startCode);
            memcpy(frameBuffer + pos, sps, spsSize);
            receivedFrameSize = pos + spsSize;
        }
        else if (nalType == 8)
        {
            //std::cout << "PPS" << std::endl;
            ppsSize = payloadSize;
            memcpy(pps, payload, ppsSize);
            continue;
            
            // Cpoy start code and pps.
            int pos = 0;
            memcpy(frameBuffer, startCode, sizeof(startCode));
            pos += sizeof(startCode);
            memcpy(frameBuffer + pos, pps, ppsSize);
            receivedFrameSize = pos + ppsSize;
        }
        else if (nalType == 5)
        {
            // It is full frame not truncated.
            //std::cout << "IDR" << std::endl;
            // Copy start code sps pps and frame.
            int pos = 0;
            memcpy(frameBuffer, startCode, sizeof(startCode));
            pos += sizeof(startCode);
            memcpy(frameBuffer + pos, sps, spsSize);
            pos += spsSize;
            memcpy(frameBuffer + pos, startCode, sizeof(startCode));
            pos += sizeof(startCode);
            memcpy(frameBuffer + pos, pps, ppsSize);
            pos += ppsSize;
            memcpy(frameBuffer + pos, startCode, sizeof(startCode));
            pos += sizeof(startCode);
            memcpy(frameBuffer + pos, payload, payloadSize);
            receivedFrameSize = pos + payloadSize;
        }
        else if (nalType == 1)
        {
            //std::cout << "NON-IDR" << std::endl;
            // Copy start code and frame.
            int pos = 0;
            memcpy(frameBuffer, startCode, sizeof(startCode));
            pos += sizeof(startCode);
            memcpy(frameBuffer + pos, payload, payloadSize);
            receivedFrameSize = pos + payloadSize;
        }
        else if (nalType == 28)  // FU-A Fragmentation Unit
        {
            uint8_t fuHeader = payload[1];
            uint8_t startBit = fuHeader & 0x80;
            uint8_t endBit = fuHeader & 0x40;
            uint8_t nalUnitType = fuHeader & 0x1F;
            
            uint8_t reconstructedNALHeader = (payload[0] & 0xE0) | nalUnitType; // Restore NAL header
            
            if (startBit)
            {
                receivedFrameSize = 0; // Reset for new frame

                if (nalUnitType == 5)  // IDR Frame
                {
                    // Prepend SPS and PPS for IDR
                    int pos = 0;
                    memcpy(frameBuffer, startCode, sizeof(startCode));
                    pos += sizeof(startCode);
                    memcpy(frameBuffer + pos, sps, spsSize);
                    pos += spsSize;
                    memcpy(frameBuffer + pos, startCode, sizeof(startCode));
                    pos += sizeof(startCode);
                    memcpy(frameBuffer + pos, pps, ppsSize);
                    pos += ppsSize;
                    receivedFrameSize = pos;
                }

                memcpy(frameBuffer + receivedFrameSize, startCode, sizeof(startCode));
                receivedFrameSize += sizeof(startCode);
                frameBuffer[receivedFrameSize] = reconstructedNALHeader;  // Insert reconstructed header
                receivedFrameSize += 1;

                memcpy(frameBuffer + receivedFrameSize, payload + 2, payloadSize - 2);
                receivedFrameSize += payloadSize - 2;
            }
            else // Middle or end
            {
                memcpy(frameBuffer + receivedFrameSize, payload + 2, payloadSize - 2);
                receivedFrameSize += payloadSize - 2;
            }
        }
        else
        {
            // Unknown NAL type, skip for now.
            continue;
        }

        // Do not process until width and height are set.
        if (width == 0 || height == 0)
            continue;

        // Copy frame to shared frame.
        m_receivedFrameMutex.lock();
        memcpy(m_receivedFrame.data, frameBuffer, receivedFrameSize);
        m_receivedFrame.size = receivedFrameSize;
        m_receivedFrameMutex.unlock();

        // Notify that new frame is ready.
        std::unique_lock lk(m_condVarMtx);
        m_condVarFlag.store(true);
        m_condVar.notify_one();
        lk.unlock();
    }
}