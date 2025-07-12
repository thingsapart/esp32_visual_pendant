#!/bin/bash
FW=$1
/Users/loranttoth/.platformio//packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-addr2line -e ${FW} ${@:2}
