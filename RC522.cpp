#include "RC522.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <future>

//--------- SPI Pin aliases -----------//
#define MFRC522_NSS GPIO_NUM_27
#define MFRC522_SCK GPIO_NUM_32
#define MFRC522_MOSI GPIO_NUM_25
#define MFRC522_MISO GPIO_NUM_34

using namespace std;

RC522::RC522()
{
    gpio_reset_pin(MFRC522_NSS);
    gpio_reset_pin(MFRC522_MISO);
    gpio_reset_pin(MFRC522_MOSI);
    gpio_reset_pin(MFRC522_SCK);

    gpio_set_direction(MFRC522_NSS, GPIO_MODE_OUTPUT);
    gpio_set_direction(MFRC522_MOSI, GPIO_MODE_OUTPUT);
    gpio_set_direction(MFRC522_SCK, GPIO_MODE_OUTPUT);
    gpio_set_direction(MFRC522_MISO, GPIO_MODE_INPUT);

    // set levels SCK = 0, NSS = 1
    gpio_set_level(MFRC522_SCK, 0);
    gpio_set_level(MFRC522_NSS, 1);

    // soft reset
    write_command(RC522Commands::SoftReset);

    // reset modwidthreg to default - mostly a redundant action because of softreset above
    write_byte_to_register(RC522Registers::ModWidthReg, 0x26);

    // init mode reg  CRCPreset = 01 for 6363h refer 6.1.6 CRC_A http://www.emutag.com/iso/14443-3.pdf
    write_byte_to_register(RC522Registers::ModeReg, 0x3d);

    // -------- TxAsk ---------------//
    // bit 6 = 1, others X. forces a 100 % ASK modulation independent of the ModGsPReg register setting
    write_byte_to_register(RC522Registers::TxASKReg, 0x40);

    // ---------- antenna ON ---------------//

    read_register(RC522Registers::TxControlReg);

    write_byte_to_register(RC522Registers::TxControlReg, _dataMISO[0] | 0x03);
}

inline void RC522::delay_millis(uint8_t millis)
{
    vTaskDelay(millis / portTICK_PERIOD_MS);
}

void RC522::write_data_to_SPI()
{
    assert(_dataMOSI.size() > 1);

    _dataMISO.clear();

    // start transaction, set NSS to low
    gpio_set_level(MFRC522_NSS, 0);

    // wait 1 millisecond
    delay_millis(1);

    // write data now
    for (vector<uint8_t>::iterator it = _dataMOSI.begin(); it != _dataMOSI.end(); it++)
    {
        uint8_t byte = *it;

        uint8_t read = 0x0;

        uint8_t one = 0x80;

        // write bit by bit
        for (uint8_t n = 0; n < 8; n++)
        {
            // clock should be zero at the start here
            assert(0 == gpio_get_level(MFRC522_SCK));

            // msb goes first
            gpio_set_level(MFRC522_MOSI, ((byte & one) ? 1 : 0));

            one >>= 1;

            // wait 1 millisecond, allow data to stabilize
            delay_millis(1);

            // clock to high
            gpio_set_level(MFRC522_SCK, 1);

            // wait 1 millisecond, allow slave to write
            delay_millis(1);

            read <<= 1;

            read |= ((uint8_t)gpio_get_level(MFRC522_MISO));

            // wait 1 millisecond
            delay_millis(1);

            // clock to low
            gpio_set_level(MFRC522_SCK, 0);

            // stay low for 1 millisecond
            delay_millis(1);
        }

        if (it != _dataMOSI.begin())
        {
            // write back
            _dataMISO.push_back(read);
        }
    }

    // wait 1 millisecond
    delay_millis(1);

    // end transaction, set NSS to high
    gpio_set_level(MFRC522_NSS, 1);

    // allow high 1 millisecond
    delay_millis(1);
}

void RC522::write_byte_to_register(uint8_t reg, uint8_t data)
{
    _dataMOSI.clear();

    _dataMOSI.push_back(reg);

    _dataMOSI.push_back(data);

    write_data_to_SPI();
}

void RC522::write_command(RC522Commands command)
{
    write_byte_to_register((uint8_t)CommandReg, command);
}

void RC522::read_register(RC522Registers reg)
{
    write_byte_to_register((((uint8_t)reg) | 0x80), 0x0);
}

void RC522::print_last_response(const char *heading)
{
    writeDebugLog("====BEGIN %s====", heading);

    for (int i = 0; i < _dataMISO.size(); i++)
    {
        writeDebugLog("data[%d] = 0x%02x", i, _dataMISO[i]);
    }

    writeDebugLog("====ENDOF %s====", heading);
}

uint8_t RC522::GetRC522Version()
{
    read_register(RC522Registers::VersionReg);

    return _dataMISO[0];
}

bool RC522::execute_PICC_command(PICCCommands piccCommand)
{
    // set ValuesAfterColl bit = 1 of the CollReg 0EH register
    read_register(RC522Registers::CollReg);

    write_byte_to_register(RC522Registers::CollReg, _dataMISO[0] & 0x7f);

    // set idle
    write_command(RC522Commands::Idle);

    // clear interrupts
    write_byte_to_register(RC522Registers::ComIrqReg, 0x7f);

    // clear fifo level register
    write_byte_to_register(RC522Registers::FIFOLevelReg, 0x80);

    // write to FIFODataReg now
    if (piccCommand == PICCCommands::REQA)
    {
        write_byte_to_register(RC522Registers::FIFODataReg, piccCommand);
    }
    else
    {
        _dataMOSI.clear();

        _dataMOSI.push_back(FIFODataReg);

        _dataMOSI.push_back(piccCommand);

        // NVB number of valid bytes in MSB - 2 or 7
        _dataMOSI.push_back(_anticollisionDataBits.empty() ? 0x20 : 0x70);

        if (!_anticollisionDataBits.empty())
        {
            // append anti-collision bits
            _dataMOSI.insert(_dataMOSI.end(), _anticollisionDataBits.begin(), _anticollisionDataBits.end());
        }

        write_data_to_SPI();
    }

    // set transmission
    write_command(RC522Commands::Transceive);

    // execute the command, set MSB of BitFramingReg to 1
    // see short frames for 7-bit REQA -  http://www.emutag.com/iso/14443-3.pdf
    write_byte_to_register(RC522Registers::BitFramingReg, /*1000 xxxx*/ 0x80 + ((PICCCommands::REQA == piccCommand) ? 7 : 0));

    /** start asynchronous polling. this async is not necessary if we have
     * a loop already running on a different thread, like we have done
     * in this project (see start_rc522_loop in the main.cpp file).
     * but if (for example,) a button is used to start the detection function, and that function
     * runs on the main thread, then this async would be necessary
     */

    future<bool> result =
        async(launch::async,
              [&]()
              {
                  int counter = 0;

                  do
                  {
                      // wait 100 milliseconds
                      // we are using the timer of the OS, could have equally
                      // used the timers on RC522 module
                      delay_millis(100);

                      read_register(RC522Registers::ComIrqReg);

                      // check any of the bits 4[IdleRq] and 5[RxIRq] of ComIrqReg
                      if ((_dataMISO[0] & 0x30))
                          return true;

                  } while (counter++ < 50);
                  // while (counter++ < ((PICCCommands::REQA == piccCommand) ? 5 : 50));

                  return false;
              });

    if (!result.get())
        return false;

    read_register(RC522Registers::FIFOLevelReg);

    uint8_t bytesAvailable = _dataMISO[0];

    _dataMOSI.clear();

    // we have to repeatedly send read requests to FIFODataReg for each byte
    _dataMOSI.assign(bytesAvailable, FIFODataReg | 0x80);

    _dataMOSI.push_back(0x0);

    write_data_to_SPI();

    // now _dataMISO contains the response from the RC522 module

    return true;
}

bool RC522::send_REQA_command()
{
    bool result = execute_PICC_command(PICCCommands::REQA);

    if (result)
    {
        print_last_response("ATQA Response");
    }

    // clear any anti-collision bits
    _anticollisionDataBits.clear();

    return result;
}

bool RC522::get_sak(PICCCascadeLevels level)
{
    PICCCommands piccCommand = ((PICCCascadeLevels::CascadeLevel1 == level) ? PICCCommands::SEL1 : ((PICCCascadeLevels::CascadeLevel2 == level) ? PICCCommands::SEL2 : PICCCommands::SEL3));

    if (PICCCascadeLevels::CascadeLevel1 != level)
    {
        // if we are here _anticollisionDataBits SHOULD BE WITH CRC_A due to the previous cascade level
        assert(_anticollisionDataBits.size() > 5);

        // remove last two crc_a bytes, keep uid0-3 + bcc = 5 bytes
        _anticollisionDataBits.erase(_anticollisionDataBits.begin() + 5, _anticollisionDataBits.end());
    }

    // (1) send anti-collision command (2) get uid + BCC (3) verify BCC if it is valid XOR
    if (!execute_PICC_command(piccCommand) || (_dataMISO.size() < 5) || (_dataMISO[4] != (_dataMISO[0] ^ _dataMISO[1] ^ _dataMISO[2] ^ _dataMISO[3])))
    {
        return false;
    }

    // copy uid bytes + BCC to anticollisionbits vector
    _anticollisionDataBits.assign(_dataMISO.begin(), _dataMISO.begin() + 5);

    // do we have a collision? or any other errors
    read_register(RC522Registers::ErrorReg);

    // WrErr - TempErr - reserved - BufferOvfl - CollErr[1] - CRCErr[x] - ParityErr[1] - ProtocolErr[1]
    //  1       1           0           1           1           1           1               1
    if (0 != (_dataMISO[0] & 0xdf))
    {
        return false;
    }

    // so far _antiCollision vector contains 4 UID + checksum

    // we have to append crc 2 bytes, so calculate crc...now

    write_command(RC522Commands::Idle);

    // clear CRC Interrupt
    write_byte_to_register(RC522Registers::DivIrqReg, 0x04);

    // clear fifo level register
    write_byte_to_register(RC522Registers::FIFOLevelReg, 0x80);

    _dataMOSI.clear();

    _dataMOSI.push_back(FIFODataReg);

    // write to FIFODataReg: piccCOMMAND [0x93 | 0x95 | 0x97] - NVB 0x70 - UID0 - UID1 - UID2- UID3 - BCC

    _dataMOSI.push_back(piccCommand);

    _dataMOSI.push_back(/*nvb always 0x70 for SEL*/ 0x70);

    // append anti-collision bits
    _dataMOSI.insert(_dataMOSI.end(), _anticollisionDataBits.begin(), _anticollisionDataBits.end());

    // move to internal buffer
    write_data_to_SPI();

    // calculate CRC
    write_command(RC522Commands::CalcCRC);

    // start asynchronous polling, wait for execution
    future<bool> result =
        async(launch::async,
              [&]()
              {
                  int counter = 0;

                  do
                  {
                      // wait 100 milliseconds
                      delay_millis(100);

                      read_register(RC522Registers::DivIrqReg);

                      // is calculation done?
                      if ((_dataMISO[0] & 0x04))
                          return true;

                  } while (counter++ < 50);

                  return false;
              });

    if (!result.get())
        return false;

    write_command(RC522Commands::Idle);

    read_register(RC522Registers::CRCResultRegLSB);

    _anticollisionDataBits.push_back(_dataMISO[0]);

    read_register(RC522Registers::CRCResultRegMSB);

    _anticollisionDataBits.push_back(_dataMISO[0]);

    // we have the CRC, so execute same command as SELECT command now
    return execute_PICC_command(piccCommand);
}

bool RC522::GetUID(char uidString[20 + 1])
{
    if (!send_REQA_command())
    {
        writeDebugLog("PICCsendREQACommand waiting for card...");

        return false;
    }

    if (!get_sak(PICCCascadeLevels::CascadeLevel1))
    {
        writeDebugLog("PICCdoCascadeLevel1 failed");

        return false;
    }
    else
    {
        uint8_t sak = _dataMISO[0];

        // cascade bit is set
        if (4 & sak)
        {
            // get the three bytes, leaving the first CT
            sprintf(uidString, "%02x%02x%02x", _anticollisionDataBits[1], _anticollisionDataBits[2], _anticollisionDataBits[3]);

            // increase cascade level
            if (!get_sak(PICCCascadeLevels::CascadeLevel2))
            {
                writeDebugLog("PICCdoCascadeLevel2 failed");

                return false;
            }
            else
            {
                uint8_t sak = _dataMISO[0];

                // cascade bit is set
                if (4 & sak)
                {
                    // get the three bytes, leaving the first CT
                    sprintf(uidString + 6, "%02x%02x%02x", _anticollisionDataBits[1], _anticollisionDataBits[2], _anticollisionDataBits[3]);

                    // raise cascade level
                    if (!get_sak(PICCCascadeLevels::CascadeLevel3))
                    {
                        writeDebugLog("PICCdoCascadeLevel3 failed");

                        return false;
                    }
                    else
                    {
                        sprintf(uidString + 12, "%02x%02x%02x%02x", _anticollisionDataBits[0], _anticollisionDataBits[1], _anticollisionDataBits[2], _anticollisionDataBits[3]);

                        return true;
                    }
                }
                else
                {
                    sprintf(uidString + 6, "%02x%02x%02x%02x", _anticollisionDataBits[0], _anticollisionDataBits[1], _anticollisionDataBits[2], _anticollisionDataBits[3]);

                    return true;
                }
            }
        }
        else
        {
            // serial number
            sprintf(uidString, "%02x%02x%02x%02x", _anticollisionDataBits[0], _anticollisionDataBits[1], _anticollisionDataBits[2], _anticollisionDataBits[3]);

            return true;
        }
    }
}