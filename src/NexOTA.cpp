#include "NexOTA.h"

const char NEXTION_FF_FF[3] = {0xFF, 0xFF, 0x00};

String statusMessage = "";

NexOTA::NexOTA(uint32_t baudrate, uint32_t file_size) {
    this->baudrate = baudrate;

    this->file_size = file_size;
    this->file_section_total = file_size / NEX_OTA_SECTION_SIZE;
    if (file_size % NEX_OTA_SECTION_SIZE) {
        this->file_section_total++;
    }
}

bool NexOTA::connect() {
    yield();

    String response = "";

    this->sendCommand("DRAKJHSUYDGBNCJHGJKSHBDN");
    this->sendCommand("", true, true); // 0x00 0xFF 0xFF 0xFF

    this->sendCommand("connect");

    this->recvRetString(response);
    if(response.indexOf("comok") != -1) {
        return true;
    }

    response = "";
    delay(110); // based on serial analyser from Nextion editor V0.58 to Nextion display NX4024T032_011R
    this->sendCommand(NEXTION_FF_FF, false);

    this->sendCommand("connect"); // second attempt
    this->recvRetString(response);
    if (response.indexOf("comok") == -1 && response[0] != 0x1A) {
        return false;
    }

    return true;
}

bool NexOTA::begin() {
    yield();

    if (!this->connect()) {
        return false;
    }

    this->setRunningMode();

    if(!this->echoTest("mystop_yesABC")) {
        statusMessage = "echo test failed";
        dbSerialPrintln(statusMessage);
        return false;
    }

    if(!this->handlingSleepAndDim()) {
        statusMessage = "handling sleep and dim settings failed";
        dbSerialPrintln(statusMessage);
        return false;
    }

    if(!this->setPrepareForFirmwareUpdate()) {
        statusMessage = "modifybaudrate error";
        dbSerialPrintln(statusMessage);
        return false;
    }

    return true;
}

void NexOTA::sendCommand(const char* cmd, bool tail, bool null_head) {
    yield();

    if (null_head) {
        nexSerial.write((byte)0x00);
    }

    while (nexSerial.available()) {
        nexSerial.read();
    }

    nexSerial.print(cmd);
    if (tail) {
        nexSerial.write((byte)0xFF);
        nexSerial.write((byte)0xFF);
        nexSerial.write((byte)0xFF);
    }
}

uint32_t NexOTA::recvRetString(String &response, bool recv_flag) {
    yield();

    uint32_t ret = 0;
    uint8_t c = 0;
    uint8_t nr_of_FF_bytes = 0;
    long start;
    bool exit_flag = false;
    bool ff_flag = false;

    start = millis();

    while (millis() - start <= NEX_OTA_TIMEOUT) {
        while (nexSerial.available()) {
            c = nexSerial.read();
            if (c == 0) {
                continue;
            }

            if (c == 0xFF) {
                nr_of_FF_bytes++;
            } else {
                nr_of_FF_bytes=0;
                ff_flag = false;
            }

            if (nr_of_FF_bytes >= 3) {
                ff_flag = true;
            }

            response += (char) c;

            if (recv_flag && response.indexOf(0x05) != -1) {
                exit_flag = true;
            }
        }

        if (exit_flag || ff_flag) {
            break;
        }
    }

    if (ff_flag) {
        response = response.substring(0, response.length() -3); // Remove last 3 0xFF
    }

    ret = response.length();
    return ret;
}

uint32_t NexOTA::recvRetForUpdate(byte *response) {
    yield();

    long start = millis();
    uint32_t cursor = 0;

    while (millis() - start <= NEX_OTA_TIMEOUT) {
        while (nexSerial.available()) {
            uint8_t c = nexSerial.read();
            response[cursor++] = c;

            if (cursor == 1) {
                if (response[0] == 0x05) {
                    return cursor;
                }

                if (response[0] != 0x08) {
                    return 0;
                }
            }

            if (cursor == 5) {
                return cursor;
            }
        }
    }

    return cursor;
}

bool NexOTA::setPrepareForFirmwareUpdate() {
    yield();

    String response = "";
    String cmd = "";

    cmd = "00";
    this->sendCommand(cmd.c_str());
    delay(1);

    this->recvRetString(response, true);

    // TODO: add a fallback for v1.1 protocol
    String filesize_str = String(this->file_size, 10);
    String baudrate_str = String(this->baudrate, 10);
    cmd = "whmi-wris " + filesize_str + "," + baudrate_str + ",1";

    this->sendCommand(cmd.c_str());

    // Without flush, the whmi command will NOT transmitted by the ESP in the current baudrate
    // because switching to another baudrate (nexSerialBegin command) has an higher prio.
    // The ESP will first jump to the new 'upload_baudrate' and than process the serial 'transmit buffer'
    // The flush command forced the ESP to wait until the 'transmit buffer' is empty
    nexSerial.flush();

    this->recvRetString(response, true);

    return response.indexOf(0x05) != -1;
}

void NexOTA::setUpdateProgressCallback(ProgressUpdateFunction value) {
    this->update_progress_callback = value;
}

uint32_t NexOTA::fetchSectionFromStream(Stream *stream, uint32_t section, char *buff) {
    uint32_t size_left = min(this->file_size - section * NEX_OTA_SECTION_SIZE, NEX_OTA_SECTION_SIZE);

    uint32_t buff_size = 0;
    while (buff_size < size_left) {
        uint32_t size_available = stream->available();
        if (!size_available) {
            delay(1);
            continue;
        }

        uint32_t size_requested = min(size_left - buff_size, size_available);

        // read up to NEX_OTA_SECTION_SIZE bytes into the buffer
        buff_size += stream->readBytes(&buff[buff_size], size_requested);
    }

    return size_left;
}

void NexOTA::skipToSectionForStream(Stream *stream, uint32_t section, uint32_t new_section) {
    uint32_t total_size = new_section * NEX_OTA_SECTION_SIZE;

    for (uint32_t i = 0; i < total_size; i++) {
        while (!stream->available()) {
            delay(1);
        }

        stream->read();
    }
}


uint32_t NexOTA::uploadSection(uint32_t section, char *buff, uint32_t size) {
    yield();

    dbSerialPrintln("uploading section " + String(section) + " of " + String(this->file_section_total) + " with size " + String(size));
    nexSerial.write(buff, size);

    uint32_t skip_amount = 0;

    byte recv_buf[5] = { 0 };
    if (!this->recvRetForUpdate(recv_buf)) {
        return 0;
    }

    if (recv_buf[0] == 0x08) {
        skip_amount += recv_buf[1];
        skip_amount += recv_buf[2] << 8;
        skip_amount += recv_buf[3] << 16;
        skip_amount += recv_buf[4] << 24;
    }

    uint32_t new_section = skip_amount == 0 ? section + 1 : skip_amount / NEX_OTA_SECTION_SIZE;

    if (this->update_progress_callback) {
        this->update_progress_callback(section);
    }

    return new_section;
}

bool NexOTA::upload(Stream *stream) {
    yield();

    uint32_t section = 0;
    while (section < this->file_section_total) {
        char buff[NEX_OTA_SECTION_SIZE] = { 0 };

        uint32_t size = this->fetchSectionFromStream(stream, section, buff);

        uint32_t new_section = this->uploadSection(section, buff, size);
        if (new_section == 0) {
            return false;
        }
        if (new_section - section > 1) {
            this->skipToSectionForStream(stream, section, new_section);
        }
        section = new_section;
    }

    return true;
}

bool NexOTA::upload(SectionFetchFunction fetcher) {
    yield();

    uint32_t section = 0;
    while (section < this->file_section_total) {
        char buff[NEX_OTA_SECTION_SIZE] = { 0 };

        uint32_t size = fetcher(section, buff);

        uint32_t new_section = this->uploadSection(section, buff, size);
        if (new_section == 0) {
            return false;
        }
        section = new_section;
    }

    return true;
}


void NexOTA::softReset(void) {
    // soft reset nextion device
    this->sendCommand("rest");
}

void NexOTA::end() {
    // wait for the nextion to finish internal processes
    delay(1600);

    // soft reset the nextion
    this->softReset();

    // end Serial connection
    nexSerial.end();

    statusMessage = "upload ok";
    dbSerialPrintln(statusMessage);
}

void NexOTA::setRunningMode(void) {
    String cmd = "";
    delay (100);
    cmd = "runmod=2";
    this->sendCommand(cmd.c_str());
    delay(60);
}

bool NexOTA::echoTest(String input) {
    String cmd = "";
    String response = "";

    cmd = "print \"" + input + "\"";
    this->sendCommand(cmd.c_str());

    this->recvRetString(response);

    return (response.indexOf(input) != -1);
}



bool NexOTA::handlingSleepAndDim() {
    String cmd = "";
    String response = "";
    bool set_sleep = false;
    bool set_dim = false;

    cmd = "get sleep";
    this->sendCommand(cmd.c_str());

    this->recvRetString(response);

    if (response[0] != 0x71) {
        statusMessage = "unknown response from 'get sleep' request";
        dbSerialPrintln(statusMessage);
        return false;
    }

    if (response[1] != 0x00) {
        dbSerialPrintln("sleep enabled");
        set_sleep = true;
    } else {
        dbSerialPrintln("sleep disabled");
    }

    response = "";
    cmd = "get dim";
    this->sendCommand(cmd.c_str());

    this->recvRetString(response);

    if (response[0] != 0x71) {
        statusMessage = "unknown response from 'get dim' request";
        dbSerialPrintln(statusMessage);
        return false;
    }

    if (response[1] == 0x00) {
        dbSerialPrintln("dim is 0%, backlight from display is turned off");
        set_dim = true;
    } else {
        dbSerialPrintln();
        dbSerialPrint("dim ");
        dbSerialPrint((uint8_t) response[1] );
        dbSerialPrintln("%");
    }

    if (!this->echoTest("ABC")) {
        statusMessage = "echo test in 'handling sleep and dim' failed";
        dbSerialPrintln(statusMessage);
        return false;
    }

    if (set_sleep) {
        cmd = "sleep=0";
        this->sendCommand(cmd.c_str());
        // Unfortunately the display doesn't send any respone on the wake up request (sleep=0)
        // Let the ESP wait for one second, this is based on serial analyser from Nextion editor V0.58 to Nextion display NX4024T032_011R
        // This gives the Nextion display some time to wake up
        delay(1000);
    }

    if (set_dim) {
        cmd = "dim=100";
        this->sendCommand(cmd.c_str());
        delay(15);
    }

    return true;
}
