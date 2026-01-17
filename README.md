# HP 21XX Paper Tape Emulator

The idea to this project come from this page: [newton.freehostia.com/hp](https://newton.freehostia.com/hp/). A simple PIC processor connected to the ubiquitous HP 12566 Microcircuit boards that provide 16 bits of general input and output with a simple handshake. The software interface is identical to the one used for the paper tape reader and paper tape punch. The interface originally used for punch and reader, 12597A is a +12V level interface which made them a bit harder to interface. The 12566A board which used just standard +5V signals was easier to work with and I had plenty of them.

So I thought I should make my own, but then I thought, why involve a PC. I could store the files I wished to download on a small SD-card and have a small display and a couple of buttons to interact with to select what file I wanted to download into the machine! ANd then there was this idea, why not make it work as a punch as well so that I can take the output from the HP computer and store it on a file that subsequently can be uploaded. Useful if one would try compiling some small program on it.

