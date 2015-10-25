#!/usr/bin/env python
# -*- coding: utf-8 -*-
import fcntl
import os
import glob
# import subprocess
import string
import random
import sys

DEBUG = False

RANDCHAR = string.letters + string.digits

RAIDBASE = "/srv/raids/"

RAIDRELATIVE = "var/anonym"

RAIDS = ('intern0', 'intern1', 'extern0', 'extern1', 'extern2')

SEARCHPATHS = [os.path.join(RAIDBASE, r, RAIDRELATIVE) for r in RAIDS]

def sane_filename(fn):
    return len(set(fn).difference(set(string.ascii_letters+string.digits+".-_"))) == 0

def checkpaths_for_dir(dirname, paths):
    for p in paths:
        source = os.path.join(p, dirname)
        if not os.path.islink(source) and os.path.isdir(source):
            return True

def main():
    # single instance check
    lock = open("/run/anonym_share-mammutfs.lock", 'w')
    try:
        fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except IOError:
        # another instance is running
        print >> sys.stderr, "another instance is already running"
        sys.exit(1)

    # check if all raids are mounted
    # for raid in [os.path.join(RAIDBASE, RAID) for RAID in RAIDS]:
    #     if not os.path.ismount(raid):
    #         print >> sys.stderr, "the raid %s looks like it is not mounted. will not check anonymous paths as they might change" % (raid)
    #         sys.exit(1)

    dirty = False

    # a_%name_%random -> %target
    anon_map = {}
    anon_sources = set()

    with open('/srv/anonym.mapping') as f:
        for line in f:
            line = line.strip() # thegame_WtF intern0 004445 thegame    # mapping raid user anonym_dir
            target, raid, user, directory = line.split()

            correct_raid = None # intern1
            # check if the user and directory is still on this raid
            anon_directory = os.path.join(RAIDBASE, raid, RAIDRELATIVE, user, directory)
            print "check if %s still maps to %s" % (target, anon_directory)
            if os.path.isdir(anon_directory):
                print "path still exists"
                correct_raid = raid
                anon_map[target] = (correct_raid, user, directory)
                anon_sources.add(os.path.join(correct_raid, user, directory))
            else: # try to find the correct raid for the user
                dirty = True
                for RAID in RAIDS: # loop over all raids
                    if os.path.isdir(os.path.join(RAIDBASE, RAID, RAIDRELATIVE, user)): # check if user dir exists
                        print "user dir is on raid %s" % RAID
                        correct_raid = RAID
                        if os.path.isdir(os.path.join(RAIDBASE, RAID, RAIDRELATIVE, user, directory)): # anonymous directory still exists
                            print "anonymous directory is still here"
                            correct_raid = RAID
                        else:
                            correct_raid = None
                if correct_raid is None:
                    print "directory %s can be deleted" % target
                    reportfile = os.path.join("/srv/reports/", user, "anonym", directory)
                    if os.path.isfile(reportfile) and not os.path.islink(reportfile):
                        os.unlink(reportfile)
                else:
                    print "directory %s now maps to the new raid %s" % (target, correct_raid)
                    anon_map[target] = (correct_raid, user, directory)
                    anon_sources.add(os.path.join(correct_raid, user, directory))

    for source in glob.iglob("/srv/raids/*/var/anonym/*/*"):
        base, name = os.path.split(source)
        anonymbase, user = os.path.split(base)
        raid = anonymbase.split("/")[3] # TODO: this is ugly

        if user.startswith("new_") or user.startswith("old_"):
            continue # user is transfered between raids at the moment
        if os.path.join(raid, user, name) in anon_sources:
            continue # this is already mapped

        # TODO: some sanity checks on the directory name... only used in mammutfs.
        # the mapped directory "thegame_WtF" should be a safe directory as FTP, Apache,Samba etc. are exposed to this

        rcode = []
        for _ in xrange(3):
            rcode.extend(random.sample(RANDCHAR, 1))
        rcode = ''.join(rcode)
        new_name = "%s_%s" % (name, rcode)
        if new_name in anon_map:
            print >> sys.stderr, "collision!11 please ignore me if it isn't reproducable"
            continue

        print "add mapping %s for %s" % (new_name, name)
        # TODO: what if /srv/reports/001234/anonym
        userreportfolder = os.path.join("/srv/reports/", user)
        if not os.path.lexists(userreportfolder):
            os.mkdir(userreportfolder, 0o775)
        reportfolder = os.path.join("/srv/reports/", user, 'anonym')
        if not os.path.lexists(reportfolder):
            os.mkdir(reportfolder, 0o755)
        if not os.path.islink(reportfolder) and os.path.isdir(reportfolder):
            reportfile = os.path.join(reportfolder, name)
            with open(reportfile, 'w') as reportfilehandle:
                reportfilehandle.write(new_name+"\n")
        dirty = True

        anon_map[new_name] = (raid, user, name)
        anon_sources.add(os.path.join(raid, user, name))

    if dirty:
        with open("/srv/anonym.mapping.new", "w") as f:
            for target, (raid, user, directory) in anon_map.iteritems():
                # assert sane_filename(target) and len(target) < 256
                # sources = ' '.join([":"+os.path.join(RAIDBASE,r,RAIDRELATIVE,source) for r in RAIDS])
                f.write("%s %s/%s/%s " % (target, raid, user, directory))

        os.rename("/srv/anonym.mapping", "/srv/anonym.mapping.bak")
        os.rename("/srv/anonym.mapping.new", "/srv/anonym.mapping")

if __name__ == '__main__':
    main()

# /srv/anonym in public aufs einfügen
# anon in create home erstellen
