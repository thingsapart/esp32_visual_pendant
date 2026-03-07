#!/bin/sh
BENV=jc8012p4a1-sdio-debug
python3 ./tools/parse_stack_trace.py -e ./.pio/build/display-${BENV}/firmware.elf --clipboard
