#pragma once
#include <cstdint>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "Frame.h"


/**
 * @brief Rtp receiver class.
 */
class RtpReceiver
{
public:

    /**
     * @brief Class destructor.
     */
    virtual ~RtpReceiver();

    /**
     * @brief Class constructor
     */
    RtpReceiver() = default;

    /**
     * @brief Video codec is not copyable.
     */
    RtpReceiver(RtpReceiver&) = delete;
    void operator=(RtpReceiver&) = delete;

    /**
     * @brief Get string of current class version.
     * @return String of current class version "Major.Minor.Patch"
     */
    static std::string getVersion();

    /**
     * @brief Initialize rtp receiver.
     * @param ip IP address of rtp stream.
     * @param port Port of rtp stream.
     * @return TRUE if initialized or FALSE.
     */
    bool init(std::string ip, int port);

    /**
     * @brief Get frame from rtp stream.
     * @param frame Frame to be filled with data.
     * @return TRUE if frame received or FALSE.
     */
    bool getFrame(cr::video::Frame& frame);

    /**
     * @brief Close rtp receiver.
     */
    void close();

private:

    /// Socket descriptor.
    int m_socket = -1;
    /// Initialization flag.
    bool m_init = false;

    /**
     * @brief Initialize socket.
     * @param ip IP address of rtp stream.
     * @param port Port of rtp stream.
     * @return TRUE if socket initialized or FALSE.
     */
    bool initSocket(std::string ip, int port);

    /**
     * @brief Receive udp data.
     * @param buffer Buffer to store data.
     * @param bufferSize Size of buffer.
     * @return Size of received data.
     */
    int receiveUdpData(uint8_t* buffer, int bufferSize);

    /**
     * @brief Receive thread function.
     */
    void receiveThreadFunc();

    /// Receive thread.
    std::thread m_receiveThread;
    /// Stop thread flag.
    std::atomic<bool> m_stopThread{false};
    /// Shared frame.
    cr::video::Frame m_receivedFrame;
    /// Shared frame mutex.
    std::mutex m_receivedFrameMutex;
    /// Sync cond variable for putting frame.
    std::condition_variable m_condVar;
    /// Cond variable mutex.
    std::mutex m_condVarMtx;
    /// Cond variable flag for read frames.
    std::atomic<bool> m_condVarFlag{false};
};