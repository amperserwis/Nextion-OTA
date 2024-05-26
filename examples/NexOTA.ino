#include <SD.h>
#include <Nextion.h>
#include <NexOTA.h>

#define SD_CS 10

void setup() {
    Serial.begin(9600);
    while (!Serial);

    nexInit(9600);

    if (!SD.begin(SD_CS)) {
        Serial.println("SD card initialization failed");
        while (1) {};
    }

    File file = SD.open("nextion.tft", FILE_READ);

    NexOTA ota;
    if (!ota.configure(file.size())) {
        Serial.println("OTA configuration failed");
        while (1) {};
    }

    if (!ota.upload(&file)) {
        Serial.println("OTA upload failed");
        while (1) {};
    }
    ota.end();
}

void loop() {

}
