#pragma once

// functions that should be customized for windows or other environments
// now customized for ESP32
#define CUSTOMIZED

#include <vector>
#include "driver/gpio.h"

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"

CUSTOMIZED
#define writeDebugLog(format, ...) ESP_LOGD("RC522", format __VA_OPT__(, ) __VA_ARGS__)

void queue_message(uint16_t, uint16_t);

class RC522
{

public:
    CUSTOMIZED RC522();

public:
    uint8_t GetRC522Version();

/**
 * returns false if (1) more than one cards respond or (2) any other error occurs
 * in conclusion: collision is NOT supported
 * the input parameter is at least 21 byte array. 
 * on success, the array contains UID of 4 x 2 hexadecimal numbers or 7 x 2 or 10 x 2
 * the string is NULL terminated by this function
*/
    bool GetUID(/*input at least 21 chars*/char*);

private:
    // buffer to send data to the module
    std::vector<uint8_t> _dataMOSI;

    // buffer for reply from the module
    std::vector<uint8_t> _dataMISO;

    // after anti-collision command, it stores - 4 uid known bytes + 1 bcc + 2 crc_a
    std::vector<uint8_t> _anticollisionDataBits;

private:
    //-------- rc522 registers --------------//
    enum RC522Registers : uint8_t
    {
        CommandReg = (0x01 << 1),
        ComIEnReg = (0x02 << 1),
        DivIEnReg = (0x03 << 1),
        ComIrqReg = (0x04 << 1),
        DivIrqReg = (0x05 << 1),
        ErrorReg = (0x06 << 1),
        FIFODataReg = (0x09 << 1),
        FIFOLevelReg = (0x0A << 1),
        BitFramingReg = (0x0D << 1),
        CollReg = (0x0E << 1),
        ModeReg = (0x11 << 1),
        TxControlReg = (0x14 << 1),
        TxASKReg = (0x15 << 1),
        CRCResultRegMSB = (0x21 << 1),
        CRCResultRegLSB = (0x22 << 1),
        ModWidthReg = (0x24 << 1),
        VersionReg = (0x37 << 1)
    };

    //-------------- RC522 commands --------//
    enum RC522Commands : uint8_t
    {
        Idle = 0x0,
        Mem = 0x01,
        GenerateRandomID = 0x02,
        CalcCRC = 0x03,
        Transmit = 0x04,
        NoCmdChange = 0x07,
        Receive = 0x08,
        Transceive = 0x0C,
        Reserved = 0x0D,
        MFAuthent = 0x0E,
        SoftReset = 0x0F
    };

    //-------------- PICC commands --------//

    enum PICCCommands : uint8_t
    {
        REQA = 0x26,
        SEL1 = 0x93,
        SEL2 = 0x95,
        SEL3 = 0x97
    };

    enum PICCCascadeLevels : uint8_t
    {
        CascadeLevel1,
        CascadeLevel2,
        CascadeLevel3
    };

private:
    void write_byte_to_register(uint8_t, uint8_t);

    void write_command(RC522Commands);

    void read_register(RC522Registers);

    void print_last_response(const char *);

private:
    bool execute_PICC_command(PICCCommands);

    bool send_REQA_command();

    /**
     * 1. RC522: sends 0x93 0x20 
     * 2. PICC: responds with a uid0-3 + bcc
     * 3. RC522: sends 0x93 0x70 uid0-3 bcc crc_a crc_a
     * 4. PICC: sak
     * 5. if third bit of sak is NOT set then uid is complete.
     * 6. RC522: 0x95 0x70  uid0-3 bcc [last uid, but no crc]
     * 7. PICC: responds with a uid0-3 + bcc 
     * 8. RC522: sends 0x95 0x70 uid0-3 bcc crc_a crc_a 
     * 9. same steps for 0x97
    */

    bool get_sak(PICCCascadeLevels);

private:
    CUSTOMIZED void write_data_to_SPI();

    CUSTOMIZED void delay_millis(uint8_t);
};

/*

NOTES:

ref https://www.rfidhy.com/tag-rfid-mifare-introduction/

1. before reading the card selection command (inclusive), the communication rate can only be 106Kbps.
    After reading the card selection, the Tag RFID MIFARE and reader can negotiate which rate to use.

2.  When the Tag RFID MIFARE does not enter the RF field, it is called the Power-Off state.
    After entering the RF field, it enters the Idle state after receiving the power reset.
    After receiving the call command from the reader, it enters the Ready state.
    After the anti-collision cycle is selected, it enters the active state,
    and enters the sleep state after receiving the sleep command or the unfamiliar command
    in the active state.

3. Tag RFID MIFAREs have a globally unique serial number, which may be 4 bytes, 7 bytes or 10 bytes.
    When multiple Tag RFID MIFAREs enter the RF field of the reader at the same time,
    the cards follow the bit-oriented anti-collision mechanism, and the card reader
    selects a unique Tag RFID MIFARE to operate.
    After the reader has finished operating a Tag RFID MIFARE,
    it can send a sleep command to put the card to sleep, and the reader continues
    to operate on other Tag RFID MIFAREs.

4. Steps of communication: REQA [for a card in idle state], WAKE-UP for all cases including halted.
    Card responds with  ATQA.

    see short frames for 7-bit REQA -  http://www.emutag.com/iso/14443-3.pdf

ATQA
====
ATQA has two bytes. The value of the first byte is not specified (RFU).
The upper two bits of the second byte b7b6 indicate the length of the card serial number
("00" is 4 bytes, and "01" is 7 words. "10" is 10 bytes),
the value of b5 bit is not specified (RFU),
b4-b0 indicates whether or not the bit-oriented anti-collision mechanism is observed.
If it is observed, b4-b0 must have one and only 1 bit is 1.

b8b7: 00 UID size: single, 01 UID size: double, 10  UID size: triple, 11 RFU
any of b1-b5 should be 1 if the card supports anti-collision bitframes

ATQA Usually [standard doesn't say so]:
    0004H = MIFARES50,
    0002H = MIFARE S70,
    0044H = MIFAREUltraLight,
    0010H = MIFARE Light,
    0434H = MIFAREDesfire

ISO 14443A stipulates that the role of ATQA is to indicate whether the card complies with the
bit-oriented anti-collision mechanism and the length of its own card serial number,
and does not indicate which type of card.

https://www.nxp.com/docs/en/application-note/AN10833.pdf
[seems the case when more than one cards respond]
The content of the ATQA should be ignored in a real application, even though according
to the ISO/IEC 14443 it indicates that the PICC supports the anti-collision scheme.

UID
===

uid0 = 08 means uid1-3 are randomly generated.
uid0 = 88 means CT

SAK
===

https://www.nxp.com/docs/en/application-note/AN10833.pdf

X X X X X 1 X X | x4 | UID not complete

X X 1 X X 0 X X |    | UID complete, PICC compliant with ISO/IEC 14443-4

X X 0 X X 0 X X |    | UID complete, PICC NOT compliant with ISO/IEC 14443-4

X 1 X X X 0 X X |    | UID complete, PICC compliant with ISO 18092 (NFC)

X 0 X X X 0 X X |    | UID complete, PICC NOT compliant with ISO 18092 (NFC)

Card                        UID    SAK
========================    ===    ===
MIFARE Ultralight C CL2     double 00
MIFARE Classic 1K           single 08
MIFARE Classic 4K           single 18
MIFARE Classic 1K CL2       double 08
MIFARE Classic 4K CL2       double 18
MIFARE Plus                 single 08
MIFARE Plus                 single 18
MIFARE Plus CL2             double 08
MIFARE Plus CL2             double 18
MIFARE Plus                 single 10
MIFARE Plus                 single 11
MIFARE Plus CL2             double 10
MIFARE Plus CL2             double 11
MIFARE Plus                 single 20
MIFARE Plus                 single 20
MIFARE Plus CL2             double 20
MIFARE Plus CL2             double 20


*/