#!/usr/bin/env python
#
# Convert an image file to hexadecimal bytes of colormap data.
#
# -- Micah Dowty <micah@vmware.com>
#

import Image
import sys

im = Image.open(sys.argv[1])
column = 0

for byte in im.getpalette():
    print "0x%02x, " % byte,
    column += 1
    if column == 8:
        print
        column = 0

