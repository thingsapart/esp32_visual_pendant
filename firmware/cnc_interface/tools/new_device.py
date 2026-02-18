#!/usr/bin/env python3
import sys
import os

TEMPLATE = """
[env:{device_name}]
extends                 = {base_env}
build_flags             = ${{env:{base_env}.build_flags}}
                            -D APP_{app_type_upper}
{extra_flags}
"""

def main():
    if len(sys.argv) < 3:
        print("Usage: tools/new_device.py <device_name> <app_type> [base_env]")
        print("  app_type: pendant | hub | dro")
        print("  base_env: env:display32s3 (default), etc.")
        sys.exit(1)

    device_name = sys.argv[1]
    app_type = sys.argv[2]
    base_env = sys.argv[3] if len(sys.argv) > 3 else "env:display32s3"
    
    app_type_upper = app_type.upper()
    
    extra_flags = ""
    if app_type == "pendant":
        extra_flags = """                            -D LVGL_UI_RUNTIME
                            -D ESP32_LVGL_ESP_DISP"""

    print(TEMPLATE.format(
        device_name=device_name,
        base_env=base_env.replace("env:", ""),
        app_type_upper=app_type_upper,
        extra_flags=extra_flags
    ))
    
    print(f"; Copy the above block into platformio.ini")

if __name__ == "__main__":
    main()
