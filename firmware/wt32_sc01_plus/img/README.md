# Images used on-device.

## Notes
### Button Images

From Google Fonts, resized to 32x32.

* `mogrify  -scale 32x32 *.png`
* `mogrify  -fill "srgb(70%,70%,70%)" -colorize 100 *.png`

### Compound Images

* ` convert center_bore.png ctr_boss.png -geometry 18x18+7+7 -composite ctr1_boss.png` ...
