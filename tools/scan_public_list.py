#!/usr/bin/env python3

import libconf;
import io
import sys
import os
from pprint import pprint
import string
import random


def main(configfile, outfile, oldfile):
    old_entries = {}
    try:
        with io.open(oldfile) as f:
            for line in f:
                l = line.split(':', 1)
                old_entries[l[1]] = l[0]
    except FileNotFoundError:
        pass

    with io.open(configfile) as f:
        config = libconf.load(f)

    public_entries = []
    for basepath in config['raids']:
        path = basepath + "/public"
        for entry in os.listdir(path):
            public_entries.append((entry, path + "/" + entry))

        for user in os.listdir(path):
            path = basepath + "/anonymous/" + user + "/"
            try:
                for entry in os.listdir(path):
                    out_path = path + "/" + entry
                    if out_path in old_entries:
                        print("Selecting old value for " + out_path + ": " + old_entries[out_path]);
                        public_entries.append((old_entries[out_path], out_path))
                    else:
                        # Repeat until a unique identifier was found;
                        while True:
                            # Generate a new random string for identification
                            suffix = ''.join(random.choice(string.ascii_uppercase
                                                           + string.ascii_lowercase
                                                           + string.digits)
                                             for _ in range(3));
                            new_entry = entry + "_" + suffix
                            new_path = path + entry
                            if not new_entry in public_entries:
                                public_entries.append((new_entry, new_path))
                                break
            except FileNotFoundError:
                print("could not find file " + path)
                pass

    public_entries = sorted(public_entries, key=lambda e:e[0])

    # The public_entries list is done
    pprint(public_entries)
    with io.open(outfile, "w") as f:
        for e in public_entries:
            line = e[0] + ":" + e[1] + "\n"
            f.write(line);

   

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Syntax: SCAN CONFIG LISTFILE OLDFILE")
        exit(1)

    config = sys.argv[1];
    outfile = sys.argv[2];
    oldfile = sys.argv[3];

    main(config, outfile, oldfile)
