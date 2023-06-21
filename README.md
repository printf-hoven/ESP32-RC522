# ESP32-RC522

Project Page = https://hoven.in/cpp-projects/esp32-rfid-rc522-attendance.html

Github Link = https://github.com/printf-hoven/ESP32-RC522

ESP32 Page Link = https://hoven.in/cpp-projects/esp32-getting-started.html

Android App = https://play.google.com/store/apps/details?id=in.hoven.electrator

Code Customization = SALES.HOVEN@GMAIL.COM 

The following are some of the features of this project -

-ESP32 Module is automatically detected on the wifi network.

-ESP32 stores UID of the Card + Swipe Time in Universal Coordinated Time (UTC), but the Android Phone shows it on the local time according to the current culture of the Android Phone! Timestamp is formatted according to the LOCALE of your device. Wait for a few seconds for the clock to get synchronised to the international clock (see the video in Step 3 below).

-ESP32 device has been programmed to hold card-timestamp data in memory, not in its NVS storage. The code can be modified and customized accordingly. Write to us for any help.

-The data shows the Card UID, NOT a human readable name. Usually, the UID-to-Name mapping is stored in a company database. It is indeed possible to interface this app to a WebApi server, but that's beyond the scope of this project. We can customize it for you if that is a requirement.
