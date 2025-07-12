#!/bin/sh
if [ $# -lt 2 ]; then
  echo 1>&2 "USAGE: $0 <esp32-usb-dev filename> <file pattern>"
  exit 2
fi

for FILE in `find . -iname "*$2*" -type f -not -path '*/.*' -not -path '*/SDLPointer_2' -not -path '*/*.md'`; do
  mpremote connect $1 fs cp $FILE ":${FILE}"
done

if [ -n "$3" ]; then
  mpremote connect $1 reset
fi
