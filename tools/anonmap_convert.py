import sys
import re

class converter:
    def __init__(self):
        pass

    def convert(self, infname, outfname):
        with open(infname, 'r') as infile:
            with open(outfname, 'w+') as outfile:
                for line in infile:
                    match = re.match(r"^a_(.*)_(\w\w\w) .*:(.*)", line)
                    newline = "a_{}_{}:{}\n".format(match.group(1), 
                            match.group(2), match.group(3))
                    outfile.write(newline)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: " + sys.argv[0] + " [old anon map] [new anon map]")
        exit(1)

    cv = converter()
    cv.convert(sys.argv[1], sys.argv[2])

