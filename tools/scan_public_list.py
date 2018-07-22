#!/usr/bin/env python3
"""
Update or create an anonmap file for use with mammutfs.
Should be triggered regularely, in order to keep the mapping up to date.

Johannes Walcher 2017-2018
"""

import io
import sys
import os
import string
import random
import socket
import json
import libconf

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

            known = False
            for okey, oval in old_entries.items():
                if out_path == oval:
                    stat['known'] += 1
                    public_entries.append((okey, out_path))
                    known = True
                    break

            if not known:
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
                public_entries.append((test, anonpath + "/" + entry))
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
        for key, value in sorted(anonmap.items()):
            line = value + ":" + key + "\n"
            outf.write(line)


def generate_anonmap(config, existing_anonmap):
    """
    Iterate over all raids and check the anonym-folders for new entries.
    """
    # We use a list here, because it is much easier to combine two lists than
    # two dicts. We convert it to dict later

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

    # here it is converted into a dict, because from now on it is better to work
    # with a dict
    new_anonmap = {key: value for key, value in public_entries}

    stats['lost'] = len(existing_anonmap) - stats['known'] - stats['public']
    changed = len(existing_anonmap) != len(new_anonmap)
    if not changed:
        for key, value in new_anonmap.items():
            if not key in existing_anonmap or existing_anonmap[key] != value:
                changed = True
                break
    return new_anonmap, stats, changed


def trigger_update(config):
    """
    Send mammutfsd that it should update the anonmapping
    """
    retval = send_to_mammutfs(config, b"reload")
    try:
        status = json.loads(retval.decode('utf-8'))
    except json.JSONDecodeError:
        print("Received invalid response: ", retval)
        return -1

    if status['state'] != 'success':
        print("Have an error: ", retval)
        return 1
    return 0


def send_to_mammutfs(config, cmdstr):
    """
    Send a command to the running mammutfsd daemon
    """
    # Check mammutfsd config
    if config['mammutfsd']['interaction'] != "net":
        print("Mammutfsd was not configured for net interaction, cannot communicate!")
        sys.exit(-1)

    command = bytearray(cmdstr)
    if command[-1] != "\n":
        command += b"\n"

    port = int(config['mammutfsd']['port'])
    host = '127.0.0.1'
    mammutfsd = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    mammutfsd.connect((host, port))
    mammutfsd.send(command)
    retval = mammutfsd.recv(1024)
    mammutfsd.close()
    return retval



def main(configfile, outfile, oldfile):
    """
    Read the config and the old anonmap and create a new one
    """

    existing_anonmap = read_anonmap(oldfile)

    with io.open(configfile) as cfgfile:
        config = libconf.load(cfgfile)

    new_anonmap, stats, changed = generate_anonmap(config, existing_anonmap)

    write_anonmap(outfile, new_anonmap)

    if changed:
#        print ("old: ", existing_anonmap)
#        print ("")
#        print ("new ", new_anonmap)


        print("Known: %d; New: %d; Public: %d; Lost: %d"%
              (stats['known'], stats['new'], stats['public'], stats['lost']))
        trigger_update(config)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Syntax: %s CONFIG LISTFILE OLDFILE"%sys.argv[0])
        print("Example: %s mammutfs.conf public.list public.list"%sys.argv[0])
        exit(1)

    main(sys.argv[1], sys.argv[2], sys.argv[3])
