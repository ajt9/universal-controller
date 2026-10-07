# Universal Controller

Github repo designed to be built and run on a Raspberry Pi Zero W running Raspbian Lite.

This project's goal is to have the Pi connect to a controller and a console, and translate the controller inputs to the console on the fly. In short, this should allow any supported controller to work on any supported console.
**Supported Controllers**: Xbox One, Dualshock5, Nintendo Switch Pro. 
**Supported Consoles**: Nintendo Switch, Playstation 5, Xbox One.
Note that not all support is implemented as of 10/5/26.

## Input Reader

Currently able to read inputs from any controllers supported by the EVDEV Linux Kernel API.
Writes outputs to stdout. WILL be changed to write to IPC.

## Website

A webUI frontend for changing the configuration file that will eventually be used by the translator process that, as the name implies, translates inputs from the user controller to inputs that the target console can interpret.

I like potatos
