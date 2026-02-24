/*----------------------------------------------*/
/* TJpgDec configuration for cam_receiver        */
/* Private copy — do NOT share with LVGL build   */
/*----------------------------------------------*/

#define JD_SZBUF        512
/* Specifies size of stream input buffer */

#define JD_FORMAT       1
/* Output pixel format: 1 = RGB565 (16-bit/pix, native) */

#define JD_USE_SCALE    0
/* Descaling not needed */

#define JD_TBLCLIP      1
/* Saturation arithmetic table — slightly faster on 32-bit MCU */

#define JD_FASTDECODE   1
/* 32-bit barrel shifter optimisation — good for 32-bit MCUs (ESP32-S3).
   Work pool requirement: ~3.5 KB (use at least 4096 bytes). */
