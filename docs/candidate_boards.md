### Caveats:
* 480x272 means -50px vertical, probably difficult as 320px height UI is already cramped.
* ESP32-S3 usually comes with PSRAM => preferred for fonts and image loading, not sure current app would fit into pure RAM.
* (R) = resistive, (C) = capacitive.
  
### Candidate boards:

#### CYD (Cheap Yellow Display):

* (R) and (C)
* ESP32-4827S043(r/c)* (ESP32-S3) - 4.3 in but 480x272
* ESP32-8048S050 (ESP32-S3) - 5.0 in, 800x480 (needs UI reorg/scaling)
* ESP32-8048S070 (ESP32-S3) - 7.0 in, 800x480 (needs UI reorg)

#### Elecrow:

* DIS05035H (ESP32) - 3.5 in, 
* DIS06043H (ESP32-S3) - 4.3 in but 480x272 (R)
* DIS07050H (ESP32-S3) - 5.0" 800x480 (R)
* DIS08070H (ESP32-S3) - 7.0" 800x480 (R)
* ESP32 Terminal 15811 and 16099 (ESP32-S3) - 3.5" 480x320 (C)

#### Makerfabs:

* E32S3RGB43 (ESP32-S3) - 4.3" 800x480 (C)
* ESP32S3SPI35 (ESP32-S3) - 3.5" SPI 800x480 (C)
