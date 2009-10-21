#!/usr/bin/env python
#
# Convert a 1bpp image file to hexadecimal bytes of monochrome
# bitmap data. Bits are packed MSB-first. Each scanline is padded
# to 32 bits.
#
# -- Micah Dowty <micah@vmware.com>
#

import Image
import sys

im = Image.open(sys.argv[1])

sys.stderr.write("width=%d height=%d\n" % im.size)

width, height = im.size
paddedWidth = (width + 31) & ~31

for y in range(height):
    for xByte in range(paddedWidth / 8):
        byte = 0
        for xBit in range(8):
            x = xByte * 8 + xBit
            if x < width and im.getpixel((x,y)):
                byte |= 1 << (7 - xBit)

        print "0x%02x, " % byte,
    print
