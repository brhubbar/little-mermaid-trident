---
title: Trident Software
---

# Software

I hope that the program is pretty self-explanatory once you read through it (perhaps a few times).  I'm going to try to give you a broad overview here, but the details, of course, are in the code itself.  [You can find the code here](https://github.com/mminkoff/trident/blob/master/trident.ino).  Since it's in a Github repository you can not only download it but you can also suggest edits via a "pull request".  [You can also post any issues (or suggestions) you have here](https://github.com/mminkoff/trident/issues).

## Color

I used HSV for color definitions rather than RGB.  You can find more than you want to know about it on [Wikipedia](https://en.wikipedia.org/wiki/HSL_and_HSV).  The bottom line is that with HSV you can simply set a hue (H) and saturation (S) and then easily adjust the brightness value (V).  Trying to do so with RGB can be very confusing, if not for some very helpful libraries, but still, HSV for the win.  Using the FastLED library, you keep two arrays - an HSV array with the colors you want and then an RGB array that FastLED communicates with the physical LEDs when you tell it to with `FastLED.show()`.  You convert the entire HSV array into the RGB array with `hsv2rgb_rainbow()`.  It's all quite civilized.

The general idea is that you light up a particular LED (or several of them) and then decay their brightness over time.  So there's a less frequent frame rate at which you might have the LEDs "chase" (appear to light up in a row) - that's where you light up the "next" LED by setting a high brightness - and a faster frame rate at which you reduce the brightness of all LEDs by a little bit each time.  It's pretty simple but it looks great.  

Here's what it looks like with the LEDs still in a strip.  Remember that each set of four LEDs is a ring so you'll see each set of four "chasing".

<iframe width="560" height="315" src="https://www.youtube.com/embed/SuLaVNUgtOI?rel=0" frameborder="0" allowfullscreen></iframe>

## "Sparkling" tines

Other than when in an "attack", the tines are meant to "sparkle."  That happens by randomly lighting up various LEDs on the tines.  The brightness decays in the same way as the shaft LEDs.

## Pressure Sensors

If we reacted to the current reading of the sensors we'd get pretty erratic behavior as the readings can shift pretty quickly.  Instead we keep an array of the most recent readings and use the ongoing average.  One side effect is that it take a second or so for the trident to light up (if you're ambitious you can adjust the software so that it will react more quickly).  Otherwise, this makes it react much more smoothly.

## adjustPower()

Every time through the loop we adjust a variety of settings based on the average pressure reading.  The speed of the chasing, the brightness of the LEDs, the likelihood of a tine LED lighting up, the speed of the brightness decay, etc.  All of this is easily adjustable.

## Mapping and Easing

We "map" an input range from the sensors to a desired output range.  The software does this mapping smoothly.  We also ease the numbers in and out so that there is, essentially, more range at the bottom and top of the sensor readings and less in the middle.  In other words, even if there's a bit of a jump between off and on it should ramp up a bit more slowly, and similarly it tapers off as we get to the top of the reading so that even if we're pretty close to the top of the sensor range we can still see noticeable changes in the output.  This doesn't sound very articulate to me at the moment but hopefully you understand.  [Here](http://easings.net) is a bit more about it, though in a somewhat different context.
