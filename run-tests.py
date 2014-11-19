#!/usr/bin/env python

import os
import shutil
import subprocess

basepath = "raids"
mntpath = "mnt"
raids = 3
users = {
        0: ["001001", "001002", "001003"],
        1: ["002001", "002002", "002003"],
        2: ["003001", "003002", "003003"]}

directories = [
    "anonymous",
    "backup",
    "public",
    "private"]

files = ["foo", "bar", "baz", "trolling"]
payload = "trkihjuhjzudedegftkhzdedefrdexfvrgrdej" # created by a fair roll with my head over the keyboard



if os.access(basepath, os.F_OK):
    if raw_input("raid exists already. Delete it? (y/n)") == 'y':
        shutil.rmtree(basepath)
        shutil.rmtree(mntpath)
    else:
        exit(1)

raidstring = ""

os.mkdir(basepath)
os.mkdir(mntpath)

# create directory structure
for raid in range(0,raids):
    path_raid = basepath + "/raid" + str(raid)   #raids/raid1
    os.mkdir(path_raid)
    for directory in directories:
        path_dir = path_raid + "/" + directory   #raids/raid1/public
        os.mkdir(path_dir)
        for user in users[raid]:
            path_user = path_dir + "/" + user    #raids/raid1/public/002001
            os.mkdir(path_user)
            if directory == "anonymous":
                path_dir_anon = path_user + "/anon_test"
                os.mkdir(path_dir_anon)

            for f in files:
                temp_file = os.open(path_user + "/" + f + "_" + user, os.O_CREAT | os.O_WRONLY)
                os.write(temp_file, payload)


# mount our fs
testuser = users[1][1]
raidstring = ""

for raid in range(0, raids):
    raidstring += raidstring + " " + os.getcwd() + "/raids/raid" + str(raid) # fix this to a format string # fix this to a format string
print "./mammutfs {} {} -- {}".format(mntpath, testuser, raidstring)
subprocess.call("./mammutfs {} {} -- {}".format(mntpath, testuser, raidstring))
