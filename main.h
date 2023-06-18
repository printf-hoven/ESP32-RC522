
#include <inttypes.h>

//----------- message map --------//
enum APP_MESSAGES : uint16_t
{
    MSG_NULL = 0x0,
    MSG_WIFI_CONNECTED = 0x01,
    MSG_WIFI_FAILED = 0x02,
    MSG_NTP_TIME_SYNCED = 0x03,
};

//--------------forward declarations --------//

void queue_message(uint16_t, uint16_t);

void do_shutdown();
