# Third-party notices

The application source in `firmware/src`, excluding the Barlow font data, is licensed under MIT as stated in LICENSE.

The firmware binaries also incorporate components of Espressif ESP-IDF 5.5 and Apache Mynewt NimBLE, FreeRTOS, newlib and cJSON. Their licenses are retained in `licenses/`; the Barlow font license is in `Barlow-OFL.txt`. Espressif controller and PHY libraries are distributed with ESP-IDF under Apache-2.0.

Upstream sources and additional component notices can be obtained from:

- https://github.com/espressif/esp-idf/tree/v5.5
- https://github.com/espressif/esp-nimble
- https://github.com/apache/mynewt-nimble
- https://github.com/google/fonts/tree/main/ofl/barlow

No claim of exclusive ownership is made over these dependencies. A hardware-specific compiled binary is supplied for convenience; the build configuration is in `firmware/platformio.ini` and `firmware/sdkconfig.defaults`.
