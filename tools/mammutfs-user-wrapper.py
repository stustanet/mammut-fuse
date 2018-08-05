#!/usr/bin/env python3

import os
import pwd
import stat
import subprocess
import sys


if len(sys.argv) < 3 or sys.argv[1] not in ['start', 'stop']:
    print("Usage: %s [start|stop] UID"% sys.argv[0])
    sys.exit(-1)

uid = int(sys.argv[2])
pwnam = pwd.getpwuid(uid)

#CONFIG:
mammutfs = "/usr/local/sbin/mammut-fuse/build/mammutfs"
config_user = "/etc/mammutfs/user.conf"
#home = "/srv/home/{}".format(pwnam.pw_name)
home = "/tmp/{}".format(pwnam.pw_name)
#END CONFIG

if 'stop' == sys.argv[1]:
    # fusermount -u home
    ret = subprocess.run(['fusermount', '-u', home])
    sys.exit(ret.returncode)

elif 'start' == sys.argv[1]:
    os.makedirs("/run/mammutfs_sockets", exist_ok=True)
    os.chown(home, pwnam.pw_uid, pwnam.pw_gid)
    os.chmod(home, stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)

    # Drop into the users priviliges
    # Set the group priviliges
    os.setgroups(os.getgrouplist(pwnam.pw_name, pwnam.pw_gid))
    os.setgid(pwnam.pw_gid)
    os.setuid(pwnam.pw_uid)

    #os.environ['HOME'] = home

    # Start mammutfs with arguments
    args = [mammutfs, config_user,
    #        "--deamonize", "0", # If the subprozess forks it will destroy the management
                                # threads. so we have to make him not forking, regardless
                                # whats in the config - we have already forked.
            "--mountpoint", home,
            "--username", pwnam.pw_name]

    ## Force jotweh to run in foreground. DO NOT DO THIS UNDER AUTOMOUNT CONDITIONS!
    #if pwnam.pw_name == "007394":
    #    args += [ "--deamonize", "0" ]

    print("Starting " + str(args))
    os.execv(mammutfs, args)
