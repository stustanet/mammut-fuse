#!/usr/bin/env python3

import libconf
import pathlib
import subprocess
import sys
import io
import pwd
import os

if len(sys.argv) > 1:
    configfile = sys.argv[1]
else:
    configfile = "/etc/mammutfs/backup-list.conf"
#mammutfs = "/usr/local/sbin/mammut-fuse/build/mammutfs"
mammutfs = "./build/mammutfs"
setfacl = "setfacl"

with io.open(configfile) as cfgfile:
    config = libconf.load(cfgfile)
backupuser = config["backupuser"]
mountpoint = config["mountpoint"]

# 1. force the ACL on all backup folders
# "getfacl" will not duplicate permissions!
#for raid in config["raids"]:
#    path = pathlib.Path(raid) / "backup"
#    if path.exists() and path.is_dir():
##        args = [setfacl, "-R", "-m", "u:%s:rx"%backupuser, str(path)]
#        print("Running", args)
#        subprocess.run(args)

# 2. Drop user priviliges to the backup user
pwnam = pwd.getpwnam(backupuser)

# If the running user is root, we can possibly force to fix the mountpoint
if os.getuid() == 0:
    os.chown(mountpoint, pwnam.pw_uid, pwnam.pw_gid)
    os.chmod(mountpoint, stat.S_IRUSR | stat.S_IWUSR | stat.S_IXUSR)
    # Drop into the users priviliges
    # Set the group priviliges
    os.setgroups(os.getgrouplist(pwnam.pw_name, pwnam.pw_gid))
    os.setgid(pwnam.pw_gid)
    os.setuid(pwnam.pw_uid)
else:
    print("We are not root, so we cannot drop access rights")
    #sys.exit(-1)

# 3. Start mammutfs at backup dir
args = [mammutfs, configfile]
print("Starting " + str(args))
os.execv(mammutfs, args)
