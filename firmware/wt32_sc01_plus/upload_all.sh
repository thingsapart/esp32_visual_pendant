#!/bin/sh
if [ $# -lt 1 ]; then
  echo 1>&2 "USAGE: $0 <esp32-usb-dev filename>"
  exit 2
fi

for FILE in `find . -type f -not -path '*/.*'`; do
  mpremote connect $1 fs cp $FILE ":${FILE}"
done
mpremote connect $1 reset
