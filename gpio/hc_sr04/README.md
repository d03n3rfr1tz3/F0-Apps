# HC_SR04

## Description
The original version is from [SquachWare hc_sr04](https://github.com/skizzophrenic/SquachWare-CFW/tree/dev/applications/plugins/hc_sr04).
The improved version is from [jamisonderek hc_sr04](https://github.com/jamisonderek/flipper-zero-tutorials/tree/main/gpio/hc_sr04).

This version modifies the code to correctly calculate the distance based on the code from my own Arduino Library.
I also upgraded the mutex and some other things, so that it works with the newest Flipper Zero firmware version.

## Pins
- (Flipper -> HC_SR04 device)
- (5V -> VCC)
- (GND -> GND)
- (13|TX -> Trig)
- (14|RX -> Echo)

## Building

Build an app using [micro Flipper Build Tool](https://pypi.org/project/ufbt/):
```
ufbt
```
You can now run the application (actually, the build step is not needed, it will be built and then launched):
```
ufbt launch
```