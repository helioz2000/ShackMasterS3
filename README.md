| Supported Targets | ESP32-S3 |
| ----------------- | -------- |

# RigExpert ShackMaster Power 600 
Control Software

## Overview

This project is designed run on a [Espressif ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)  board which is equipped with WiFi and 2 USB ports.

* The main USB-C port (COM) is used for programming and debugging the board.
* The ShackMaster 600 is connected to the seconday USB-C port (USB) interfaced and acts as a HID device.
* The USB 2 port on the board is configured as a HID host.

## IDE
This project is develped in Visual Studio Code with Expressif ESP-IDF extension installed.

### Configure the project

Open the project configuration menu (`idf.py menuconfig`).

In the `Example Configuration` menu:

* Set the Wi-Fi configuration.
    * Set `WiFi SSID`.
    * Set `WiFi Password`.

Optional: If you need, change the other options according to your requirements.

### Build and Flash

Build the project and flash it to the board, then run the monitor tool to view the serial output:

Run `idf.py -p PORT flash monitor` to build, flash and monitor the project.

(To exit the serial monitor, type ``Ctrl-]``.)

See the Getting Started Guide for all the steps to configure and use the ESP-IDF to build projects.

* [ESP-IDF Getting Started Guide on ESP32](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)
* [ESP-IDF Getting Started Guide on ESP32-S2](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/get-started/index.html)
* [ESP-IDF Getting Started Guide on ESP32-C3](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/index.html)

## Example Output

There is the console output for this example:

```
I (917) phy: phy_version: 3960, 5211945, Jul 18 2018, 10:40:07, 0, 0
I (917) wifi: mode : softAP (30:ae:a4:80:45:69)
I (917) wifi softAP: wifi_init_softap finished.SSID:myssid password:mypassword
I (26457) wifi: n:1 0, o:1 0, ap:1 1, sta:255 255, prof:1
I (26457) wifi: station: 70:ef:00:43:96:67 join, AID=1, bg, 20
I (26467) wifi softAP: station:70:ef:00:43:96:67 join, AID=1
I (27657) esp_netif_lwip: DHCP server assigned IP to a station, IP is: 192.168.4.2
```

## Running the example on ESP Chips without Wi-Fi

This example can run on ESP Chips without Wi-Fi using ESP-Hosted. See the [Two-Chip Solution](../../README.md#wi-fi-examples-with-two-chip-solution) section in the upper level `README.md` for information.

## Troubleshooting

For any technical queries, please open an [issue](https://github.com/espressif/esp-idf/issues) on GitHub. We will get back to you soon.
