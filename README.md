# HP 21XX Paper Tape Emulator

The idea to this project come from this page: [newton.freehostia.com/hp](https://newton.freehostia.com/hp/). A simple PIC processor connected to the ubiquitous HP 12566 Microcircuit boards that provide 16 bits of general input and output with a simple handshake. The software interface is identical to the one used for the paper tape reader and paper tape punch. The interface originally used for punch and reader, 12597A is a +12V level interface which made them a bit harder to interface. The 12566A board which used just standard +5V signals was easier to work with and I had plenty of them.

So I thought I should make my own, but then I thought, why involve a PC. I could store the files I wished to download on a small SD-card and have a small display and a couple of buttons to interact with to select what file I wanted to download into the machine! And then there was this idea, why not make it work as a punch as well so that I can take the output from the HP computer and store it on a file that subsequently can be uploaded. Useful if one would try compiling some small program on it.

## Progress

Now I am able to both read files into the computer and punch from the computer. Here is a quick video showing when a Chess program is loaded into my HP 2100S.

[![Watch the video](./Running%20Chess%20on%20HP%202100S.png)](https://www.youtube.com/shorts/xUi1qgBoRDE)

Loading of the 72 k abolute binary files takes place when the red LED light up for a few seconds. It fills up the entire 32k Word memory space of the HP 2100.


![picture of the gadget](./PaperTapeEmulator.JPG)

This is how the finished board came to look like. Three buttons, Up, Down and Select, and small 128x64 OLED panel to configure and control it. A Micro SD card for file storage and USB-C connector that can be used to power the device unless powered over the 26 pin ribbin connector. The green LED indicate power and the red LED indicate activity.

The UI is rather simple. Simply navigating up and down and press the select button. The main menu have two menu items, one to select the reader file and one select the punch file. Pressing select will get you to a file selct menu where the currently selected file is shown and where you can navigate to the file you want to select. Pressing select also exits back to the main menu. On the punch menu there are also an option to create a new file with a sequence number. If an existing file is selected it will append to that file.

Since there are only three buttons the UI also have special actions when pressing a button more than 4 seconds. A long press on the select menu brings up the configuration menu. In here you can set if signals are active high or active low. For 12566A and 12566B there are two board revsions which get you to choose from active high (POS TRUE) or active low (GND TRUE) data. The active high or low for hand shake signals are configurable on both these cards. The 12566C has all parameters configurable. You should check yoour board so that it matches the config you set in this menu. There is also an option to configure 12V signals or 5V signals. 5V is used for 12566 boards while 12V is used for 12597 boards. You cannot mix a 12566 board and a 12566 board and connect them simultaneously to the paper tape emulator. There are also certain variants of the 12597 board that have -12V signals. This type of board is not supported. Unless you convert the board to +12V.

If you long press on the Down button when in the main menu the currently selected file will be rewound to the beginning so that it can be read again.

If you long press on the Up button the USB MSC mode will be stated and all emulation is stopped. This will allow you to browse the contents from a host computer if the emulator is connected over USB-C.

## BOM

For all the surface mount components the BOM is among the production files in the kicad folder. Other than that the following hardware is needed to complete the emulator:

* [Screws  M2 5mm.  8 pcs](https://www.aliexpress.com/item/32768907028.html)
* [Nylon hex standoffs for the display are 6mm M2. 4 pcs](https://www.aliexpress.com/item/1005003577479911.html)
* [Nylon washers M2 5mm x 1mm high: 8 pcs.](https://www.aliexpress.com/item/1005008318320472.html)
* [Display is a OLED 128x64 with I2C Using SH1106 chip.](https://www.aliexpress.com/item/1005007451015054.html)
* [Buttons are 6x6 mm 13 mm high tactile buttons](https://www.aliexpress.com/item/1005002487399422.html)
* [Connector is Ampehnol FCI 71922-126LF](https://www.mouser.se/ProductDetail/649-71922-126LF)
* Red 5 mm LED
* Green 5 mm LED
* 4 pin header 2.54 mm for SWD and serial debugging port. 2 pcs.

## Connectors 

The connector for the HP computer is a 48 pin 0.156 inch (3.96 mm) connector. EDAC and Sullins manufacture these.

I used a 26 pin ribbon connector in the emulator end and splitted that one in two 13 conductor halves going to one each 48 pin connector.

### Mods to supply 5V to the emulator

A good idea is to modify the 12566 and 12597 boards to supply 5V to the emulator. There are schottky diodes on the supply line internally on the emulator to avoid backpowering things so it should be safe to connect the punch and reader to different computers and also safe to have a PC connected to the USB simultaneously.

I modifed my 12566 boards to supply 5V on pin 20 since it is unused also on the 12597 boards this pin is unused.

### Reader

### Punch
