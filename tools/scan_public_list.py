#!/usr/bin/env python3
"""
Update or create an anonmap file for use with mammutfs.
Should be triggered regularely, in order to keep the mapping up to date.

TODO: - trigger running mammutfs-instances to re-read the config

Johannes Walcher 2017-2018
"""

import io
import sys
import os
import string
import random
import asyncio
import socket
import libconf

# mammutfs specific imports
from socket_connect import MammutSocket

ALLOWED_CHARS = string.ascii_uppercase + string.ascii_lowercase + string.digits\
                + "!&()+,-.=_"

STAT_TEMPLATE = {
    'known':0,
    'lost':0,
    'new':0,
    'public':0
}


def list_anon_dir(path, old_entries):
    """
    Iterate over all anonymous directories and match them with previously found pairs.
    If a pair is found, it is left unchanged, if not a new anon directory mapping is
    defined by adding a_ as prefix and a random three letter suffix.
    """
    stat = dict(STAT_TEMPLATE)


    public_entries = []
    for anonuser in os.listdir(path):
        anonpath = path + "/" + anonuser
        for entry in os.listdir(anonpath):
            out_path = anonpath + "/" + entry
            try:
                # Of the folder is empty we should safely ignore it;
                if not [name for name in os.listdir(path + "/" + entry)]:
                    continue
            except FileNotFoundError:
                pass

            if out_path in old_entries:
                stat['known'] += 1
                public_entries.append((old_entries[out_path], out_path))
                #print("known: %s"%out_path)
            else:
                stat['new'] += 1
                new_entry = ""
                for char in entry:
                    if not char in ALLOWED_CHARS:
                        new_entry += "_"
                    else:
                        new_entry += char
                # Repeat until a unique identifier was found;
                while True:
                    # Generate a new random string for identification
                    suffix = ''.join(random.choice(string.ascii_uppercase
                                                   + string.ascii_lowercase
                                                   + string.digits)
                                     for _ in range(3))
                    test = "a_" + new_entry + "_" + suffix
                    if not test in public_entries:
                        break
                new_path = anonpath + "/" + entry
                public_entries.append((test, new_path))
                #print("new: %s"%new_path)
    return public_entries, stat


def read_anonmap(anonmapfile):
    """
    Parse the anonmap and return the mapping
    """
    anonmap = {}
    try:
        with io.open(anonmapfile) as anonmapfd:
            for line in anonmapfd:
                entry = line.split(':', 1)
                anonmap[entry[1].strip()] = entry[0]
    except FileNotFoundError:
        print("Could not find anonmap file - assuming an empty one.")
    return anonmap


def write_anonmap(anonmapfile, anonmap):
    """
    Write the new anonymous mapping
    """
    with io.open(anonmapfile, "w") as outf:
        for entry in sorted(anonmap, key=lambda e: e[0]):
            line = entry[0] + ":" + entry[1] + "\n"
            outf.write(line)


def generate_anonmap(config, existing_anonmap):
    """
    Iterate over all raids and check the anonym-folders for new entries.
    """
    public_entries = []
    stats = dict(STAT_TEMPLATE)
    for basepath in config['raids']:
        try:
            path = basepath + "/public"
            for entry in os.listdir(path):
                try:
                    if [name for name in os.listdir(path + "/" + entry)]:
                        public_entries.append((entry, path + "/" + entry))
                        stats['public'] += 1
                except FileNotFoundError:
                    pass
        except FileNotFoundError:
            pass
        try:
            entries, stat = list_anon_dir(basepath + "/anonym", existing_anonmap)
            public_entries += entries
            stats = {key: oldv + stat[key] for key, oldv in stats.items()}
        except FileNotFoundError:
            pass
    stats['lost'] = len(existing_anonmap) - stats['known'] - stats['public']
    return public_entries, stats


def trigger_update(config):
    """
    Send the command to reload the anonmap to all running mammutfs instances
    """
    loop = asyncio.get_event_loop()

    try:
        count = 0
        for socketfile in os.listdir(config['socket_directory']):
            print("Reloading mammutfs @%s"%socketfile)
            count += 1
            try:
                mfssocket = MammutSocket(loop, socketfile)
                loop.run_until_complete(mfssocket.send('FORCE-RELOAD\n'))
                mfssocket.close()
            except socket.error:
                print("Error communicating")
        print("Successfully poked all (%d) running mammutfs-instances"%count)
    except FileNotFoundError:
        print("Could not read socket directory: %s"%config['socket_directory'])


def main(configfile, outfile, oldfile):
    """
    Read the config and the old anonmap and create a new one
    """

    existing_anonmap = read_anonmap(oldfile)

    with io.open(configfile) as cfgfile:
        config = libconf.load(cfgfile)

    new_anonmap, stats = generate_anonmap(config, existing_anonmap)

    write_anonmap(outfile, new_anonmap)

    print("Known: %d; New: %d; Public: %d; Lost: %d"%
          (stats['known'], stats['new'], stats['public'], stats['lost']))

    trigger_update(config)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Syntax: %s CONFIG LISTFILE OLDFILE"%sys.argv[0])
        print("Example: %s mammutfs.conf public.list public.list"%sys.argv[0])
        exit(1)

    main(sys.argv[1], sys.argv[2], sys.argv[3])
