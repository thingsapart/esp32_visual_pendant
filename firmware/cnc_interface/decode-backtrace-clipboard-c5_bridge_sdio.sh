#!/bin/sh
BENV=c6_bridge_sdio
python3 ./tools/parse_stack_trace.py -e ../c6_espnow_bridge/.pio/build/${BENV}/firmware.elf --clipboard
