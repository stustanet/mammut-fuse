#!/usr/bin/env python3

import sys
import os
import tempfile
from datetime import datetime

if len(sys.argv) < 3:
    print("usage: update_anonmap_raidmove.py $FULLVOL $FREEVOL $USER")
    sys.exit(-1)

anonmapfile = "/etc/mammutfs/anon.map"
outfile = "/etc/mammutfs/new.anon.map"

fullvol = sys.argv[1]
freevol = sys.argv[2]
user = sys.argv[3]

with open(outfile, "w+") as newfd:
    with open(anonmapfile) as anonmap:
        for line in anonmap:
            mapping = line.split(":", 1)

            path = mapping[1].strip().split('/')
            if ((path[-2] == 'public' and path[-1] == user) or
                    (path[-3] == 'anonym' and path[-2] == user)):
                mapping[1] = mapping[1].replace(fullvol, freevol)
                line = ":".join(mapping)
            newfd.write(line)

os.rename(anonmapfile,
          "/etc/mammutfs/anon.map_backups/anon.map."
          + datetime.now().replace(microsecond=0).isoformat())
os.rename(outfile, anonmapfile)
