# trident

This repo hosts the code and documentation for an LED trident prop intended for
use in productions of Disney's The Little Mermaid.

## Hardware

It was originally built for use with the now deprecated [Adafruit Pro Trinket
5V](https://www.adafruit.com/products/2000), and has since been updated for use
on an [Arduino Micro](https://store.arduino.cc/products/arduino-micro). In
truth, any microcontroller supported by Arduino will work just fine, but you may
need to [adjust some pinouts to optimize performance of the
LEDs](https://github.com/FastLED/FastLED/wiki/Wiring-leds).

The LEDs of choise are [DotStar LEDs](https://www.adafruit.com/categories/885),
otherwise known as [APA102
LEDs](https://github.com/FastLED/FastLED/wiki/Chipset-reference). These are
super fast, affordable, and reliable.

There are several [buttons](https://www.adafruit.com/categories/235) used for
mode control and pressure sensors made with [Velostat conductive
material](https://www.adafruit.com/products/1361) used for intensity control.

You can find out much more about it at
[http://mminkoff.github.io/trident](http://mminkoff.github.io/trident).

## Software

The only dependency not included with Arduino by default is
[FastLED](https://github.com/FastLED/FastLED), which can be installed using the
Arduino Package Manager.

## Editing

It's recommended that you use VS Code for editing. VS Code provides a number of
extensions for completing and inspecting code, and Microsoft is working on an
extension to fully integrate the Arduino build experience. To use it:

- Install the [Arduino Extension](https://github.com/microsoft/vscode-arduino)
- [Configure Intellisense](https://stackoverflow.com/a/54510703)
- Install the Draw.io unofficial extension

## Power Consumption

Full power

- Triton Mode: 1.0 A
- Ursula Mode: 1.1 A
- Plus Attack: 1.4 A (this is absolute peak power draw with 150 RGB LEDs and 60
  White LEDs attached)
- Magic Mode: 0.6 A

Resting: 0.3 A

Battery is 500 mAh @ 3.7 V --> 370 mAh @ 5 V, so 1 hour on with no lights, 22
minutes in Triton mode at full power
