#!/usr/bin/env python3

import os
import pwd
import stat
import sys
from threading import Event
import systemd.journal

#CONFIG:
mammutfs = "/srv/mammutfs/mammut-fuse/build/mammutfs"
config_user = "/etc/mammutfs/user.conf"
#home = "/tmp/{}".format(pwnam.pw_name)
#END CONFIG

def log(msg, PRIORITY=systemd.journal.LOG_INFO):
    systemd.journal.send(msg, PRIORITY=PRIORITY, SYSLOG_IDENTIFIER="createhome")

def createhome(pwnam):
    base = '/srv/mammut/storage'
    private = 0o700 #stat.S_IRUSR | stat.S_IWUSR |stat.S_IXUSR
    public = 0o755  #private | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH
    to_create = [
            (f"/srv/home/{pwnam.pw_name}", private),
            (f"{base}/backup/{pwnam.pw_name}", private),
            (f"{base}/var/public/{pwnam.pw_name}", public),
            (f"{base}/var/private/{pwnam.pw_name}", private),
            (f"{base}/var/anonym/{pwnam.pw_name}", public),
            (f"{base}/var/authkeys/{pwnam.pw_name}", private),
    ]

    for f, perm in to_create:
        try:
            if not os.path.exists(f):
                log("creating folder: " + f)
                os.mkdir(f)
            os.chown(f, pwnam.pw_uid, pwnam.pw_gid)
            os.chmod(f, perm)
        except IOError as exc:
            log("IO Error: " + str(exc), PRIORITY=systemd.journal.LOG_ERR)

def stop():
    # systemd keeps care of cleaning up all mounted fsses
    sys.exit(0)

def start(uid):
    pwnam = pwd.getpwuid(uid)
    createhome(pwnam)
    home = f"/srv/home/{pwnam.pw_name}"
    # Start mammutfs with arguments
    args = [mammutfs, config_user,
            "--mountpoint", home,
            "--username", pwnam.pw_name]

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
