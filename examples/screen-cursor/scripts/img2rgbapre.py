#!/usr/bin/env python
#
# Convert an image file to hexadecimal words of pre-multiplied RGBA data.
#
# -- Micah Dowty <micah@vmware.com>
#

import Image
import sys

im = Image.open(sys.argv[1])

sys.stderr.write("width=%d height=%d\n" % im.size)

words = []

def flush():
    print " ".join(words)
    del words[:]

for r, g, b, a in im.getdata():
    r = r * a // 255
    g = g * a // 255
    b = b * a // 255
    words.append("0x%02x%02x%02x%02x," % (a,r,g,b))
    if len(words) >= 6:
        flush()

flush()
