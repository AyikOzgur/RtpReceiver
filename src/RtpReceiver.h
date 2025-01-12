#pragma once
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
     * @brief Get frame from rtp stream.
     * @param frame Frame to be filled with data.
     * @return TRUE if frame received or FALSE.
     */
    bool getFrame(cr::video::Frame& frame);

private:

   
};