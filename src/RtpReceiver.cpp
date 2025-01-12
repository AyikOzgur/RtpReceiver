#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "RtpReceiver.h"
#include "RtpReceiverVersion.h"

#define RTP_HEADER_SIZE         12
#define MAX_RTP_PAYLOAD_SIZE   1420 //1460  1500-20-12-8
#define RTP_VERSION             2

struct RtpHeader 
{
    uint8_t byte1{0};
    uint8_t byte2{0};
    uint16_t seq;
    uint32_t ts;
    uint32_t ssrc;
} __attribute__((packed));

struct RtpPacket
{
    RtpPacket()
    {
        type = 0;
        size = 0;
        timeStamp = 0;
        last = 0;
        data = new uint8_t[1600];
    }

    ~RtpPacket()
    {
        delete[] data;
    }

    uint8_t* data{nullptr};
    uint32_t size{0};
    uint32_t timeStamp{0};
    uint8_t  type;
    uint8_t  last;
};

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

    return false;
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
    int receivedFrameSize = 0;
    while (!m_stopThread.load())
    {
        // Handle receiving data and prepare Rtp packet.
        int receivedPacketSize = receiveUdpData(buffer, bufferSize);
        if (receivedPacketSize <= 0)
            continue;


        m_receivedFrameMutex.lock();
        memcpy(m_receivedFrame.data, buffer, receivedFrameSize);
        m_receivedFrame.size = receivedFrameSize;
        m_receivedFrameMutex.unlock();

        // Notify that new frame is ready.
        std::unique_lock lk(m_condVarMtx);
        m_condVarFlag.store(true);
        m_condVar.notify_one();
        lk.unlock();
    }
}