#!/usr/bin/env python3

import os
import random
import subprocess


BASE = '/tmp/mammut-fuse'

RAIDS = ['raid0', 'raid1', 'raid2']
USERS = ['karl', 'heinz', 'otto', os.environ['USER']]
MODULES = ['anonym', 'backup', 'private', 'public', 'authkeys']


def makefile(filepath):
    with open(filepath, 'w+'):
        pass

def create_test_mapping():
    subprocess.call(['rm', '-rf', os.path.join(BASE, 'raids')])
    user_on_raid_map = { user: random.choice(RAIDS) for user in USERS }

    os.makedirs(os.path.join(BASE, 'mnt'), mode=0o755, exist_ok=True)
    for user in USERS:
        raid = user_on_raid_map[user]
        for module in MODULES:
            path = os.path.join(BASE, 'raids', raid, module, user)
            os.makedirs(path, mode=0o755, exist_ok=True)

            if module == 'anonym':
                os.makedirs(os.path.join(path, "anon"), mode=0o755, exist_ok=True)
                makefile(os.path.join(path, "anon", "testfile"))
                os.makedirs(os.path.join(path, "anon" + user), mode=0o755, exist_ok=True)
                makefile(os.path.join(path, "anon" + user, "testfile"))

            elif module in ['public', 'private', 'backup']:
                os.makedirs(os.path.join(path, module + 'testdir'),
                            mode=0o755, exist_ok=True)
                makefile(os.path.join(path, module + 'testfile'))

            elif module in ['authkeys']:
                makefile(os.path.join(path, 'authorized_keys'))

if __name__ == "__main__":
    create_test_mapping()
