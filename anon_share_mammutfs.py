#!/usr/bin/env python
# -*- coding: utf-8 -*-
import fcntl
import os
import glob
import json
import argparse
# import subprocess
import string
import random
import sys

DEBUG = False

RANDCHAR = string.ascii_letters + string.digits

def sane_filename(fn):
    return len(set(fn).difference(set(string.ascii_letters+string.digits+".-_"))) == 0

def checkpaths_for_dir(dirname, paths):
    for p in paths:
        source = os.path.join(p, dirname)
        if not os.path.islink(source) and os.path.isdir(source):
            return True

def main():
    parser = argparse.ArgumentParser(description='Mammut anon lister')
    parser.add_argument('config', metavar='config', help='Anon lister config file')
    parser.add_argument('--reinit', action='store_true', help='Re-initialize the mapping')
    parser.add_argument('-debug', default=0, type=int, help='Debug mode (0 Normal, 1 DBG, 2 DVL')
    args = parser.parse_args()

    # single instance check
    #lock = open("/run/anonym_share-mammutfs.lock", 'w')
    #try:
    #    fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    #except IOError:
    #    # another instance is running
    #    print >> sys.stderr, "another instance is already running"
    #    sys.exit(1)

    debug = args.debug
    init_map = args.reinit

    with open(args.config) as config_file:
            cfg = json.load(config_file)

    raids   = cfg['anon_dirs']
    mapfile = cfg['mapping_file']
    map_prefix = cfg['anon_prefix']

    if debug > 0:
        print('RAIDS:')
        for r in raids:
            print("    %s" % r)
        print("Prefix: %s" % map_prefix)
        print("Mapping file: %s" % mapfile)

    dirty = False

    # mapping file: <export_name> <anon_source_raid> <anon_source_dir>
    anon_map = {}
    anon_sources = set()

    if not os.path.isfile(mapfile):
        init_map = True

    if not init_map:
        with open(mapfile) as f:
            for line in f:
                line = line.strip()
                export_name, src_raid, src_path = line.split()

                src_full_path = os.path.join(src_raid, src_path)
                # check if the user and directory is still on this raid
                if debug > 1:
                    print("check if %s still exists" % src_full_path)

                if os.path.isdir(src_full_path):
                    if debug > 1:
                        print("-> Still valid")

                    anon_map[export_name] = (src_raid, src_path)
                    anon_sources.add(src_full_path)

                else: # try to find the new raid for this dir

                    dirty = True
                    new_raid = None

                    for r in raids: # loop over all raids
                        path_on_raid = os.path.join(r, src_path)

                        if os.path.isdir(path_on_raid): # check if user dir exists
                            new_raid = r
                            break

                    if new_path is None:
                        if debug > 0:
                            print("Removed: %s (%s)" % (export_name, src_full_path))

                        reportfile = os.path.join("/srv/reports/", user, "anonym", directory)
                        if os.path.isfile(reportfile) and not os.path.islink(reportfile):
                            os.unlink(reportfile)
                    else:
                        if debug > 0:
                            print("Moved: %s (%s to %s)" % (export_name, src_raid, new_raid))

                        anon_map[export_name] = (new_raid, src_path)
                        anon_sources.add(os.path.join(new_raid, src_path))

    for r in raids:
        for user in os.listdir(r):
            if not os.path.isdir(os.path.join(r, user)):
                continue

            for entry in os.listdir(os.path.join(r, user)):

                src_path = os.path.join(r, user, entry)

                if not os.path.isdir(src_path):
                    continue

                if debug > 1:
                    print("Dir: " + src_path)

                # TODO: Check if this is still relevant
                if entry.startswith("new_") or entry.startswith("old_"):
                    continue # user is transfered between raids at the moment

                if src_path in anon_sources:
                    continue # this is already mapped

                rcode = []
                for _ in range(3):
                    rcode.extend(random.sample(RANDCHAR, 1))
                rcode = ''.join(rcode)
                export_name = "%s%s_%s" % (map_prefix, entry, rcode)
                if export_name in anon_map:
                    print >> sys.stderr, "collision!11 please ignore me if it isn't reproducable"
                    continue

                if debug > 0:
                    print("New anon dir: %s (%s)" % (export_name, src_path))

                anon_map[export_name] = (r, os.path.join(user, entry))
                anon_sources.add(src_path)

                dirty = True

    if dirty:
        if debug > 0:
            print("Rewrite anon map: %s, number of mappings: %i" % (mapfile, len(anon_map)))

        with open(mapfile + ".new", "w") as f:
            for target, (raid, src) in anon_map.items():
                f.write("%s %s %s\n" % (target, raid, src))
                if debug > 1:
                    print("%s → %s" % (target, os.path.join(raid, src)))

        if os.path.isfile(mapfile):
            os.rename(mapfile, mapfile + ".old")

        os.rename(mapfile + ".new", mapfile)

if __name__ == '__main__':
    main()

# /srv/anonym in public aufs einfügen
# anon in create home erstellen
