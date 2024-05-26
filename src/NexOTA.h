#ifndef __NEX_OTA_H__
#define __NEX_OTA_H__

#include <Arduino.h>
#include <Nextion.h>

#define NEX_OTA_TIMEOUT 2000
#define NEX_OTA_SECTION_SIZE 4096

/**
 *
 * Provides the API for nextion to upload the ftf file.
 */
class NexOTA
{
public: /* methods */
    NexOTA();

    /**
     * configure and prepare upload, set the file size
     *
     * @return true if success, false for failure.
     */
    bool configure(uint32_t file_size);

    /**
     * start update tft file to nextion.
     *
     * @return true if success, false for failure.
     */
    bool upload(Stream *stream);

    /**
     * Send reset command to Nextion over serial
     *
     * @return none.
     */
    void softReset(void);

    /**
     * Send reset, end serial, reset this->sent_packets & update status message
     *
     * @return none.
     */
    void end(void);

private: /* methods */
    /*
     * connect to Nextion.
     *
     * @return true if success, false for failure.
     */
    bool connect();

    /*
     * set download baudrate.
     *
     * @param baudrate - set download baudrate.
     *
     * @return true if success, false for failure.
     */
    bool setPrepareForFirmwareUpdate();

    /*
     * set Nextion running mode.
     *
     * Undocumented feature of the Nextion protocol.
     * It's used by the 'upload to Nextion device' feature of the Nextion Editor V0.58
     *
     * The nextion display doesn't send any response
     *
     */
    void setRunningMode(void);

    /*
     * Test UART nextion connection availability
     *
     * @param input  - echo string,
     *
     * @return true when the 'echo string' that is send is equal to the received string
     *
     * This test is used by the 'upload to Nextion device' feature of the Nextion Editor V0.58
     *
     */
    bool echoTest(String input);

    /*
     * This function get the sleep and dim value from the Nextion display.
     *
     * If sleep = 1 meaning: sleep is enabled
     *                action : sleep will be disabled
     * If dim = 0,  meaning: the display backlight is turned off
     *              action : dim will be set to 100 (percent)
     *
     */
    bool handlingSleepAndDim(void);

    /*
     * Send command to Nextion.
     *
     * @param cmd       - the string of command.
     * @param tail      - end the string with tripple 0xFF byte
     * @param null_head - start the string with a single 0x00 byte
     *
     * @return none.
     */
    void sendCommand(const char* cmd, bool tail = true, bool null_head = false);

    /*
     * Receive string data.
     *
     * @param buffer - save string data.
     * @param timeout - set timeout time.
     * @param recv_flag - if recv_flag is true,will braak when receive 0x05.
     *
     * @return the length of string buffer.
     *
     */
    uint32_t recvRetString(String &string, bool recv_flag = false);

    uint32_t recvRetForUpdate(byte *response);

    uint32_t fetchSection(Stream *stream, uint32_t section, char *buff);

    void skipToSection(Stream *stream, uint32_t section, uin32_t new_section);

private: /* data */
    uint32_t file_size = 0;
    uint32_t file_section_total = 0;

    THandlerFunction update_progress_callback;
};

#endif
