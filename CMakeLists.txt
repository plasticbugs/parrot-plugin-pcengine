# PC Engine (PCE.CVP): PC Engine / TurboGrafx-16 HuCard games on the
# ColecoVision. Pick a .pce in the menu and press A: pce-go
# (vendor/pce-go, from pico-pcePlus) runs it on the cart, and pcecv and
# nescv send each picture and the sound to the console as the Game Boy
# kernel's lists. RESET goes back to the menu.
#
#   cmake -B build -G Ninja -DPICO_SDK_PATH=$HOME/pico-sdk
#   ninja -C build                      -> build/PCE.CVP
#
# Copy PCE.CVP into the card's /PLUGINS folder.
cmake_minimum_required(VERSION 3.13)

# the cart's SDK: the copy in sdk/ (sdk/README.md)
set(PARROTCART_SDK ${CMAKE_CURRENT_LIST_DIR}/sdk/firmware/revc)
set(PICO_BOARD_HEADER_DIRS ${PARROTCART_SDK}/cvcart_revc/boards)
set(PICO_BOARD cvcart_revc)
set(PICO_PLATFORM rp2350-arm-s)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# the USB host: sdk/tinyusb (TinyUSB 0.21 and its fixes since), not the pico-sdk's 0.18
include(${PARROTCART_SDK}/sdk/cvpad_tinyusb.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/pico_sdk_import.cmake)

project(pce C CXX ASM)
pico_sdk_init()
include(${PARROTCART_SDK}/sdk/cvplugin.cmake)

# The same source makes the same file, whenever it's built: the compiler's
# __DATE__ and __TIME__ (the build date in the binary info) come from
# SOURCE_DATE_EPOCH, fixed here at the build's day, 2026-11-23.
set(BUILD_EPOCH 1795435200)
set(CMAKE_C_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})
set(CMAKE_CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${BUILD_EPOCH})

set(S ${CMAKE_CURRENT_LIST_DIR}/src)
set(V ${CMAKE_CURRENT_LIST_DIR}/vendor/pce-go)
set(CORE ${V}/pce-go.c ${V}/pce.c ${V}/gfx.c ${V}/h6280.c ${V}/psg.c)
# vendored: its own warnings aren't ours to fix. Its memory comes from
# pcecore.c (the plugin's free RAM): the plugin has no heap.
# PCE_NO_SGX, CD_MAX_TRACKS_=1: no SuperGrafx renderer, no CD track table
# (vendor/pce-go/VENDORED.md).
set_source_files_properties(${CORE} PROPERTIES
    COMPILE_OPTIONS "-w"
    COMPILE_DEFINITIONS "malloc=pce_malloc;calloc=pce_calloc;free=pce_free;PCE_NO_SGX;CD_MAX_TRACKS_=1")
set_source_files_properties(${S}/pcecv.c ${S}/pcecore.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
# RAM is the limit (the plugin runs from SRAM, beside 64K of video RAM and a
# copy of it): what runs once or seldom is built for size. The SDK's own
# files are built as every plugin builds them.
set_source_files_properties(${S}/pce.c ${S}/port/nocd.c PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Os")
set_source_files_properties(${V}/pce-go.c ${V}/psg.c PROPERTIES COMPILE_OPTIONS "-w;-Os")
set_source_files_properties(${S}/pcecore.c ${S}/port/nocd.c PROPERTIES COMPILE_DEFINITIONS "CD_MAX_TRACKS_=1")

add_executable(pce ${S}/pce.c ${S}/pcecore.c ${S}/pcecv.c ${S}/port/nocd.c ${S}/nescv.c ${CORE})
target_include_directories(pce PRIVATE ${S} ${S}/port ${V} ${PARROTCART_SDK}/midihello)
# NESCV_BESIDE: nescv_cells() and nescv_offer_cells(), a picture made beside the encoder (pcecv.h)
# NESCV_MEMO_BITS=9: the encoder remembers 512 rows, not 4096: 28K of RAM for
# about 1 ms more a busy picture (80% of rows found in place of 92%)
target_compile_definitions(pce PRIVATE NESCV_BESIDE NESCV_MEMO_BITS=9)
# the SDK's printf without floats (nothing here prints one): 2K
target_compile_definitions(pce PRIVATE PICO_PRINTF_SUPPORT_FLOAT=0 PICO_PRINTF_SUPPORT_EXPONENTIAL=0)
cvframes_use(pce)
cvframes_serve2(pce)   # core 1's bus loop acts on two samples of the pins that agree (sdk/cvfserve.S)
cvsd_use(pce)
cvpsram_use(pce)
cvpad_use(pce)                              # USB game pads: the port is a host
# -DPCE_CLOCK_KHZ=200000: the system clock set to that at the start. Not for
# the console as it is: releases run at the SDK's 150 MHz.
if(PCE_CLOCK_KHZ)
  target_compile_definitions(pce PRIVATE PCE_CLOCK_KHZ=${PCE_CLOCK_KHZ})
endif()
pico_enable_stdio_usb(pce 0)
pico_enable_stdio_uart(pce 0)
cvcart_plugin(pce ID parrotcart.pce NAME "PC Engine" VERSION 0.1.3 BY "Parrot Cart"
              HOME /PCE OPENS .pce USES usb-host psram ICON src/pce_icon.txt FILE_ICON src/pce_file_icon.txt
              README CART-README.md)
