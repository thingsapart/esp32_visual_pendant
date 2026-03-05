#!/bin/sh
BENV=jc8012p4a1
python3 ./tools/parse_stack_trace.py -e ../.pio/build/:${BENV}/firmware.elf --clipboard
