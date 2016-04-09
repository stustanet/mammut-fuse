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

    raids           = cfg['anon_dirs']
    mapfile         = cfg['mapping_file']
    map_prefix      = cfg['anon_prefix']
    reports_dir     = cfg['reports_dir']
    reports_name    = cfg['reports_name']

    if debug > 0:
        print('RAIDS:')
        for r in raids:
            print("    %s" % r)
        print("Prefix: %s" % map_prefix)
        print("Mapping file: %s" % mapfile)

    dirty = False

    # mapping file: <export_name> <userid> <anon_source_dir>
    anon_map = {}
    anon_sources = set()

    if not os.path.isfile(mapfile):
        init_map = True

    if not init_map:
        with open(mapfile) as f:
            for line in f:
                line = line.strip()
                export_name, userid, orig = line.split()

                # check if the user and directory is still existing
                if debug > 1:
                    print("check if %s of %s still exists" % (orig, userid))

                still_valid = False
                for r in raids: # loop over all raids
                    path_on_raid = os.path.join(r, userid, orig)

                    if os.path.isdir(path_on_raid): # check if user dir exists
                        if debug > 1:
                            print("OK: path=%s" % path_on_raid)

                        still_valid = True
                        break

                if not still_valid:
                    if debug > 0:
                        print("Removed: %s" % export_name)

                    dirty = True

                else:
                    anon_map[export_name] = (userid, orig)
                    anon_sources.add(os.path.join(userid, orig))

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

                if os.path.join(user, entry) in anon_sources:
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

                anon_map[export_name] = (user, entry)
                anon_sources.add(os.path.join(user, entry))

                dirty = True

    if dirty:
        if debug > 0:
            print("Rewrite anon map: %s, number of mappings: %i" % (mapfile, len(anon_map)))

        with open(mapfile + ".new", "w") as f:
            for target, (user, src) in anon_map.items():
                f.write("%s %s %s\n" % (target, user, src))
                if debug > 1:
                    print("%s → %s" % (target, os.path.join(user, src)))

        if os.path.isfile(mapfile):
            os.rename(mapfile, mapfile + ".old")

        os.rename(mapfile + ".new", mapfile)

        usermappings = {}
        for target, (user, src) in anon_map.items():
            if not user in usermappings:
                usermappings[user] = set()

            usermappings[user].add((target, src))

        for r in raids:
            for user in os.listdir(r):
                if not os.path.isdir(os.path.join(r, user)):
                    continue

                if not user in usermappings:
                    continue

                reportfolder = os.path.join(reports_dir, user)
                if not os.path.lexists(reportfolder):
                    os.makedirs(reportfolder, 0o755)

                with open(os.path.join(reportfolder, reports_name), "w") as f:
                    for export, src in usermappings[user]:
                        f.write("%s is %s\n" % (src, export))


if __name__ == '__main__':
    main()

# /srv/anonym in public aufs einfügen
# anon in create home erstellen
