#!/usr/bin/env python

import os
import shutil

basepath = "raids"
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
    else:
        exit(1)

os.mkdir(basepath)
# create directory structure
for raid in range(0,raids):
    path_raid = basepath + "/raid" + str(raid)
    os.mkdir(path_raid)
    for user in users[raid]:
        path_user = path_raid + "/" + user
        os.mkdir(path_user)
        for directory in directories:
	    path_dir = path_user + "/" + directory
	    os.mkdir(path_dir)
	    if directory == "anonymous":
		path_dir = path_dir + "/anon_test"
		os.mkdir(path_dir)

            for f in files:
                temp_file = os.open(path_dir + "/" + f + "_" + user, os.O_CREAT | os.O_WRONLY)
                os.write(temp_file, payload)


