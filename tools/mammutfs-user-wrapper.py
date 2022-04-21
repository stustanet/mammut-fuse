#!/usr/bin/env python3

import os
import pwd
import stat
import sys
from threading import Event
import systemd.journal
import time

try:
    with open("/etc/mammutfs/testuser") as testuserconfig:
        uid = int(sys.argv[2])
        pwnam = pwd.getpwuid(uid)
        if pwnam.pw_name in testuserconfig.read():
            tester = True
        else:
            tester = False
except:
    tester = False

#CONFIG:
if tester:
    mammutfs = "/srv/mammutfs/mammut-integration/build/mammutfs"
    epochedit = os.path.getatime(mammutfs)
    print("using mammutfs as a tester with a custom executable at", mammutfs, "with age", time.time() - epochedit, "s")
else:
    mammutfs = "/srv/mammutfs/mammut-fuse/build/mammutfs"

config_user = "/etc/mammutfs/user.conf"
#home = "/tmp/{}".format(pwnam.pw_name)
#END CONFIG

def log(msg, PRIORITY=systemd.journal.LOG_INFO):
    systemd.journal.send(msg, PRIORITY=PRIORITY, SYSLOG_IDENTIFIER="createhome")

def createhome(pwnam, homename):
    base = '/srv/mammut/storage'
    private = 0o700 #stat.S_IRUSR | stat.S_IWUSR |stat.S_IXUSR
    public = 0o755  #private | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH
    to_create = [
            (f"/srv/home/{homename}", private),
            (f"{base}/backup/{homename}", private),
            (f"{base}/var/public/{homename}", public),
            (f"{base}/var/private/{homename}", private),
            (f"{base}/var/anonym/{homename}", public),
            (f"{base}/var/authkeys/{homename}", private),
    ]
    authfile = f"{base}/var/authkeys/{homename}/authorized_keys"

    for f, perm in to_create:
        try:
            if not os.path.exists(f):
                log("creating folder: " + f)
                os.mkdir(f)
            os.chown(f, pwnam.pw_uid, pwnam.pw_gid)
            os.chmod(f, perm)
        except IOError as exc:
            log("IO Error: " + str(exc), PRIORITY=systemd.journal.LOG_ERR)

    if not os.path.exists(authfile):
        with open(authfile, 'w+') as fo:
            fo.write("# This is the authorized_keys file for authenticating for sftp via a keyfile\n")
            fo.write("# Please consult the wiki at https://wiki.stusta.de/Datenserver for details\n\n")

        os.chown(authfile, pwnam.pw_uid, pwnam.pw_gid)
        os.chmod(authfile, 0o600)


def stop():
    # systemd keeps care of cleaning up all mounted fsses
    sys.exit(0)

def start(uid):
    pwnam = pwd.getpwuid(uid)

    # get the homename dir, assumption: homename is the last component of the home path
    # We know that this is kind of hacky, however we still chose to use this for the
    # sake of simplicity
    # (c) JW, Jobi
    homename = pwnam.pw_dir.split("/")[-1]
    createhome(pwnam, homename)
    home = pwnam.pw_dir
    # Start mammutfs with arguments
    args = [mammutfs, config_user,
            "--mountpoint", home,
            "--username", pwnam.pw_name,
            "--homename", homename]

    ## Force jotweh to run in foreground. DO NOT DO THIS UNDER AUTOMOUNT CONDITIONS!
    #if pwnam.pw_name == "007394":
    #    args += [ "--deamonize", "0" ]

    # Drop into the users priviliges
    os.setgroups(os.getgrouplist(pwnam.pw_name, pwnam.pw_gid))
    os.setgid(pwnam.pw_gid)
    os.setuid(pwnam.pw_uid)

    print("Starting " + str(args))
    os.execv(mammutfs, args)

def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ['start', 'stop']:
        print("Usage: %s [start|stop] UID"% sys.argv[0])
        sys.exit(-1)

    if 'stop' == sys.argv[1]:
        stop()
    elif 'start' == sys.argv[1]:
        uid = int(sys.argv[2])
        # Local users have ids < 90k - so we will not mammutfs for them!
        if uid < 90000:
            # but this is started as systemd.service:TYPE=forking
            # so we fork into a process that does nothing except wait for its death
            if os.fork() == 0:
                # this creates a new void event, that is never triggered until the
                # process dies -> daemon runs, systemd is happy
                Event().wait()
                sys.exit(0)
            else:
                # The main process exists so systemd can continue
                sys.exit(0)
        start(uid)

if __name__ == "__main__":
    main()
