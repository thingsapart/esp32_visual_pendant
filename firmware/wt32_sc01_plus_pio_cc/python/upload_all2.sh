#!/bin/bash
if [ $# -lt 1 ]; then
  echo 1>&2 "USAGE: $0 <esp32-usb-dev filename>"
  exit 2
fi

DIRS_IN=`find . -type d -not -path '.' -not -path '*/.*' -not -path '*/SDLPointer_2' -not -path '*/*.md'`
DIRS=$(for DIR in $DIRS_IN; do echo ":${DIR}"; done)
mpremote connect $1 fs mkdir $DIRS

FILES_IN=`find . -type f -not -path '*/.*' -not -path '*/SDLPointer_2' -not -path '*/*.md' -not -path '*/*.sh'`
FILES=$(for FILE in $FILES_IN; do echo "fs cp ${FILE} :${FILE} + "; done)
mpremote connect $1 $FILES
mpremote connect $1 reset
