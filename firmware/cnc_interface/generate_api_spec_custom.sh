#!/bin/sh
FILES=( ./src/ui/components/lv_*.h )
echo "PROCESSING: ${FILES[*]}"
python3 ./tools/generate_ui_json.py -o ./data/api_spec_custom.json --stub-out ./data/ui_stubs.h "${FILES[@]}"
