#!/usr/bin/env python3

"""
Convert an autofs5 anonmapping to a mammutfs anonmapping preserving the suffixes
"""

import os
import re
import sys

def convert(infname, outfname):
    """
    Read the old anonmap generate the new one
    This method will always create a new out-mapping
    And will only read the normal anonymous files - not public files
    that are also required in mammutfs
    """
    converted = 0
    failed = 0
    with open(infname, 'r') as infile:
        with open(outfname, 'w+') as outfile:
            for line in infile:
                match = re.match(r"^a_(.*)_(\w\w\w) --bind :(.*)", line)
                success = False
                for candidate in match.group(3).split(":"):
                    candidate = candidate.strip()
                    if os.path.isdir(candidate):
                        newline = "a_{}_{}:{}\n".format(
                            match.group(1),
                            match.group(2),
                            candidate)
                        converted += 1
                        outfile.write(newline)
                        success = True
                        break
                if not success:
                    print("Could not find origin for anonmap line: ", line)
                    failed += 1

    print("Converted: %s; Not found: %s "%(converted, failed))
if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: " + sys.argv[0] + " [old anon map] [new anon map]")
        exit(1)

    convert(sys.argv[1], sys.argv[2])
