# Remote controller using SmartThings Direct Connected Devices SDK

[![License](https://img.shields.io/badge/licence-Apache%202.0-brightgreen.svg?style=flat)](LICENSE)

This project converts esp32c3 super mini with an IR receiver, which is easily affordable in forms of clone boards, into a SmartThings device with multiple buttons.
In regard with the wiring, the Signal pin of IR receiver should be on pin 20, but you can easily change the pin number defined as `IR_RX_GPIO_NUM` in `main.c`. Just be careful about strapping pins.

The code is based from `switch_example` in[SmartThings SDK Reference for Direct Connected Devices for C](https://github.com/SmartThingsCommunity/st-device-sdk-c-ref), using [SmartThings SDK for Direct Connected Devices for C](https://github.com/SmartThingsCommunity/st-device-sdk-c) version 2.3.1.

To use, you should change `WIFI_SSID` and `WIFI_PSWD` to each of yours in `sdkconfig` and `Kconfig.projbuild`, and put `device_info.json` / `onboarding_config.json` into `/main/` directory, then build/flash this project using SmartThings SDK.

I'm not explaining further details as there's [a official github repository dedicated for reference](https://github.com/SmartThingsCommunity/st-device-sdk-c-ref), and if you haven't checked it out I'd recommend so, unless you have experience in [SmartThings SDK for Direct Connected Devices for C](https://github.com/SmartThingsCommunity/st-device-sdk-c).