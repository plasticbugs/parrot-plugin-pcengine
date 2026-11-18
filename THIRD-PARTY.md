# What in this repository came from elsewhere

The plugin's own code, in `src/`, is under the GNU GPL, version 3
(`LICENSE`). The copy of the cart's SDK in `sdk/` is under the MIT license
(`sdk/LICENSE`). These parts came from elsewhere and keep their own terms.

| Where | What | License |
|---|---|---|
| `vendor/pce-go/` | PCE-GO by Alex Duchesne and Macs75, from [retro-go](https://github.com/ducalex/retro-go). It comes from HuExpress by Alex von Gluck IV, Hu-Go! by Zeograd (Olivier Jolly) and others, and Bero's PCE emulator (`vendor/pce-go/CREDITS`). This copy is from [pico-pcePlus](https://github.com/PicoPlus-devel/pico-pcePlus), with the changes listed in `vendor/pce-go/VENDORED.md` | GPL-2.0 (`vendor/pce-go/COPYING`), and pico-pcePlus as a whole GPL-3.0 (`vendor/pce-go/LICENSE-pico-pcePlus`) |
| `sdk/firmware/revc/sdk/tinyusb/` | [TinyUSB](https://github.com/hathach/tinyusb) (Ha Thach and contributors), with the cart's fixes, listed in its `VENDORED.md` | MIT (`sdk/firmware/revc/sdk/tinyusb/LICENSE`) |
| `cmake/pico_sdk_import.cmake` | from the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | BSD 3-Clause |

PCE-GO's files have no license notice in them. `COPYING` beside them is the
text of the GPL, version 2, and HuExpress and Hu-Go! were under the GPL,
version 2 or later. pico-pcePlus, which this copy and its fixes come from,
is under the GPL, version 3, so the plugin as a whole is too.

`src/nescv.c` and `src/nescv.h` are the picture encoder from the cart's NES
plugin (<https://github.com/plasticbugs/parrot-plugin-nes>), by Parrot Cart.

The built plugin also contains the Raspberry Pi Pico SDK's runtime (BSD
3-Clause) and TinyUSB (MIT).

The 8x8 screen font's letter shapes come from the 1982 arcade game Mr. Do!
