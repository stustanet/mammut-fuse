#!/usr/bin/env python3

import libconf;
import io
import sys
import os
from pprint import pprint
import string
import random

ALLOWED_CHARS=string.ascii_uppercase + string.ascii_lowercase + string.digits + "!&()+,-.="


def list_anon_dir(path, old_entries):
    public_entries = []
    for anonuser in os.listdir(path):
        anonpath = path + "/" + anonuser
        print("Scanning User dir " + anonpath)
        for entry in os.listdir(anonpath):
            out_path = anonpath + "/" + entry
            print("Scanning anondir " + out_path)
            try:
                # Of the folder is empty we should safely ignore it;
                if not [name for name in os.listdir(path + "/" + entry)]:
                    print("Skipping empty anon folder " + path + "/" + entry)
                    continue;
            except FileNotFoundError:
                pass

            if out_path in old_entries:
                print("Selecting old value for " + out_path + ": " + old_entries[out_path]);
                public_entries.append((old_entries[out_path], out_path))
            else:
                new_entry = ""
                for e in entry:
                    if not e in ALLOWED_CHARS:
                        new_entry += "_"
                    else:
                        new_entry += e
                # Repeat until a unique identifier was found;
                while True:
                    # Generate a new random string for identification
                    suffix = ''.join(random.choice(string.ascii_uppercase
                                                   + string.ascii_lowercase
                                                   + string.digits)
                                     for _ in range(3));
                    test = "a_" + new_entry + "_" + suffix
                    if not test in public_entries:
                        break
                new_path = anonpath + "/" + entry
                public_entries.append((test, new_path))
    return public_entries

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
        try:
            path = basepath + "/public"
            for entry in os.listdir(path):
                try:
                    if [name for name in os.listdir(path + "/" + entry)]:
                        public_entries.append((entry, path + "/" + entry))
                    else:
                        print("Skipping empty user folder " + path + "/" + entry)
                except FileNotFoundError:
                    pass
        except FileNotFoundError:
            pass
        try:
            path = basepath + "/anonym";
            public_entries += list_anon_dir(path, old_entries)
        except FileNotFoundError:
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
