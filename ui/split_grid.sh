#!/bin/bash
filename=$(basename -- "$1")
extension="${filename##*.}"
filename="${filename%.*}"

convert "$1" -crop 3x3@ +repage "${filename}_%d.${extension}"
