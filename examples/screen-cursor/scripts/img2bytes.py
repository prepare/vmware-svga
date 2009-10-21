#!/usr/bin/env python
#
# Convert an image file to hexadecimal bytes of indexed color data.
#
# -- Micah Dowty <micah@vmware.com>
#

import Image
import sys

im = Image.open(sys.argv[1])
column = 0

for byte in im.getdata():
    print "0x%02x, " % byte,
    column += 1
    if column == 8:
        print
        column = 0

