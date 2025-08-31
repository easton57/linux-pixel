## Interesting Notes
- tcpm.o would not build with the typec.h from the private/google-modules/soc/gs/include/trace/hooks directory
- Kernel build recommended disabling CONFIG_DEBUG_INFO_BTF (install dwarves that includes pahole and error goes away)
- /include/dt-bindings/power/s2mpg1x-power.h was wanting kconfig info creating build problems as dtsi, from my understanding, shouldn't need that information yet since it builds before kconfig build and inclusion. A separate s2mpg12-power.h has been created for s2mpg12 and s2mpg14 power. s2mpg1x-power defaults to the lower METER_CHANNEL_MAX value of 8 vs 12 in s2mpg12-power.h

## Things to remember you changed
- 16k include statements are disabled in the device trees for zuma

### 08-19-25 FIRST SUCCESSFUL BUILD AT THIS POINT ON MY LAPTOP WOOTWOOT

### 08-31-25 Successful PMOS Build

## For your consideration

### DTBO SHIZ
- DTBO android info for pmos https://wiki.postmarketos.org/wiki/Android_DTB/DTBO_Format
- DTBO format: https://source.android.com/docs/core/architecture/dto/partitions
- DTB images: https://source.android.com/docs/core/architecture/bootloader/dtb-images

### Vendor Boot Analysis
https://wiki.postmarketos.org/wiki/Porting_to_a_new_device/Device_specific_package#