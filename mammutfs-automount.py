#! /usr/bin/env python3

import os

f = open("/tmp/mammut-fuse-mount.log")
f.write(os.argv)
f.close()
