import asyncio
import os
import string
import random
import tempfile

ALLOWED_CHARS = string.ascii_uppercase + string.ascii_lowercase + string.digits\
                + "!&()+,-.=_"
class AnonMap:
    """
    Management of the anonymous mapping of mapput
    """

    def __init__(self, mapfile, log):
        self.mapfile = mapfile
        self.log = log
        self.mapping = self.read_anonmap(self.mapfile)

    def reload(self):
        """
        Reload the mapping from file (useful for example if another application
        has changed it
        """
        self.mapping = self.read_anonmap(self.mapfile)

    def _makekey(self, module, user, path):
        """
        Generate a the key for the anonmap from the given elements
        """
        if module == 'anonym':
            return (user, path.strip('/').split('/')[0])
        elif module == 'public':
            return (module, user)
        return None


    async def remove_entry(self, module, user, path):
        """
        Remove this entry from the annonmap
        """
        key = self._makekey(module, user, path)
        try:
            del self.mapping[key]
        except KeyError:
            pass

        self.write_anonmap()

    async def add_entry(self, module, user, path, raid):
        """
        Add this entry with a new random suffix
        """
        if module == 'anonym':
            key, entry = self._generate_anon_entry(user, path, raid)
        elif module == 'public':
            key = (module, user)
            entry = {
                'fullpath': os.path.join(raid, 'public', user),
                'mappedpath': user,
            }

        self.mapping[key] = entry
        self.write_anonmap()

    def _generate_anon_entry(self, user, path, raid):
        """
        Create a new anonmap entry
        """
        # TODO does this really have to be recreated every time?
        known_entries = [value['mappedpath'].split('/')[-1]
                         for key, value in self.mapping.items()
                         if key[0] != 'public']

        path = path.strip('/').split('/')[0]

        new_entry = ""
        for char in path:
            if not char in ALLOWED_CHARS:
                new_entry += "_"
            else:
                new_entry += char
            # Repeat until a unique identifier was found;
        while True:
            # Generate a new random string for identification
            suffix = ''.join(random.choice(string.ascii_uppercase
                                           + string.ascii_lowercase
                                           + string.digits)
                             for _ in range(3))
            test = "a_" + new_entry + "_" + suffix
            if not test in known_entries:
                break
        key = (user, path)
        entry = {
            'fullpath': os.path.join(raid, 'anonym', user, path),
            'mappedpath': test,
        }
        return key, entry

    def read_entry(self, line):
        """
        Read the entry from the anon mapping file and create the correct mapping

        The entry has the key: (username, anonfolder) or ('public', username)
        depending on wether this is a public or a anon mapping.
        """
        line = line.strip().split(':', 1)

        path = line[1].strip('/').split('/')
        if path[-2] == 'public':
            # this seams to be a "public" line
            key = ('public', path[-1]) # ('public','001234')
        elif path[-3] == 'anonymous' or path[-3] == 'anonym':
            # this seams to be a "anonym" line
            key = (path[-2], path[-1]) # ('001234', 'bla')
        else:
            print("\n\nWARNING: THERE IS SOME FUCKUP IN THE ANONMAP! Skipping entry\n\n")
            return None, None

        entry = {
            'fullpath': line[1],
            'mappedpath': line[0],
        }
        return key, entry

    async def get_public_path(self, module, user, path, publicroot):
        """
        Get the public path of a private path
        """
        key = self._makekey(module, user, path)
        if not key:
            return key

        try:
            subpath = path.strip('/').split('/')
            if len(subpath) > 1:
                subpath = os.path.join(*subpath[1:])
            else:
                subpath = subpath[0]
            return os.path.join(publicroot,
                                self.mapping[key]['mappedpath'],
                                subpath)
        except KeyError:
            return None


    def create_storable_entry(self, entry):
        """
        Create an anonmapping entry, that we can actually store in the anonmap again
        """
        return "{}:{}\n".format(entry['mappedpath'], entry['fullpath'])

    def read_anonmap(self, anonmapfile):
        """
        Parse the anonmap and return the mapping
        """
        anonmap = {}
        try:
            with open(anonmapfile) as anonmapfd:
                for line in anonmapfd:
                    key, entry = self.read_entry(line)
                    if key:
                        anonmap[key] = entry
        except FileNotFoundError:
            print("Could not find anonmap file - assuming an empty one.")
        return anonmap

    def write_anonmap(self):
        """
        Store the loaded anonmap back to the filesystem

        1. Create a temporary file with the new anonmap
        2. Flush to disk
        3. Atomically replace the old anonmap
        """
        cnt = 0

        suffix = ''.join(random.choice(string.ascii_uppercase
                                       + string.ascii_lowercase
                                       + string.digits)
                         for _ in range(6))

        tmpname = self.mapfile + '.mammutfsd.' + suffix

        with open(tmpname, "w+") as tmpfile:
            try:
                for entry in self.mapping.values():
                    cnt += 1
                    line = self.create_storable_entry(entry)
                    tmpfile.write(line)
            finally:
                tmpfile.flush()
        os.rename(tmpname, self.mapfile)
        print("updated anonmap {} with {} entries".format(self.mapfile, cnt))

class MammutfsdBaseCommands:
    """
    Module representing the core mammutfs functionality.
    This includes especially the monitoring of file changes and redistribution
    of the anonmap.
    """

    def __init__(self, loop, mfsd):
        self.mfsd = mfsd
        self.loop = loop

        self.mfsd.register('clear', self.command)
        self.mfsd.register('reload', self.command)

        self.mfsd.register('cmd', self.mfscmd)

        self.mfsd.register('setconfig', self.setconfig)
        self.mfsd.register('getconfig', self.getconfig)

        self.mfsd.register('clients', self.getclients)
        self.mfsd.register('select', self.focus_client)

        self.selected_client = None

        self.anon_map = AnonMap(self.mfsd.config['anon_mapping_file'], mfsd.log)

    async def send(self, cmd, allow_targeting):
        """
        proxy to allow easy setting of a target
        """
        if not allow_targeting or not self.selected_client:
            await self.mfsd.sendall(cmd)
        else:
            await self.selected_client.write(cmd)

    async def command(self, client, writer, cmd, allow_targeting=True):
        """
        Translate a command to send it to a mammutfs
        """
        try:
            cmd = {
                'clear':"CLEARCACHE",
                'reload':"FORCE-RELOAD",
            }[cmd[0]]
            await self.send(cmd, allow_targeting)
        except KeyError:
            # Unknown command
            writer.write(b'{"state":"error", "msg":"unknown command"}')
            raise

    async def mfscmd(self, client, writer, cmd):
        """
        Send the raw command
        """
        cmd = ' '.join(cmd[1:])
        await self.send(cmd, True)

    async def setconfig(self, client, writer, cmd):
        """
        Set a config value at one or all connected mammutfs systems
        """
        if len(cmd) < 3:
            writer.write("Error: Usage {} config value\n".format(cmd[0]).encode('utf-8'))
            await writer.drain()
        self.send("SETCONFIG " + cmd[1] + "=" + cmd[2], allow_targeting=True)

    async def getconfig(self, client, writer, cmd):
        """
        get a config value for one or all connected mammutfs systems
        """
        if len(cmd) < 2:
            writer.write("Error: Usage {} config\n".format(cmd[0]).encode('utf-8'))
            await writer.drain()
        await self.send("CONFIG " + cmd[1], allow_targeting=True)

    async def getclients(self, client, writer, cmd):
        """
        get a list of connected clients together with some raw information
        """
        for client in self.mfsd._clients:
            writer.write("Client: {} mounted at {}\n".format(
                client.details['user'],
                client.details['mountpoint']).encode('utf-8'))
        await writer.drain()

    async def focus_client(self, client, writer, cmd):
        """
        Select a single client, to target all following commands only to this
        client.
        The selection argument may be either name or mountpoint
        """
        if len(cmd) <= 1 or not cmd[1]:
            self.selected_client = None
            writer.write("Unselected client\n".encode('utf-8'))
            await writer.drain()
            return

        parameter = cmd[1]
        for client in self.mfsd._clients:
            if (client.details['user'] == parameter
                or client.details['mountpoint'] == parameter):
                self.selected_client = client
                writer.write("Selected Client: {}\n"
                             .format(client.details).encode('utf-8'))
                await writer.drain()
                break

    async def on_fileop(self, client, fileop):
        """
        Will be called whenever a client performs a fileoperation.
        The fileop looks like:

        { op:OP, module:MODULE, path:"bla/bli/blubb' }

        with OP one of MKDIR,RMDIR,WRITE,TRUNCATE,RELEASE,...
        with MODULE one of public,anonym

        More might follow, when mammutfs gets more and more modules
        """
        if (fileop['op'] in ('MKDIR', 'RMDIR')
            and fileop['module'] in ('anonym')
            and self.is_anon_root(fileop['path'])):

            if fileop['op'] == 'MKDIR':
                await self.anon_map.add_entry(fileop['module'],
                                              await client.user(fileop['module']),
                                              fileop['path'],
                                              await client.anonym_raid())
            elif fileop['op'] == 'RMDIR':
                await self.anon_map.remove_entry(fileop['module'],
                                                 await client.user(fileop['module']),
                                                 fileop['path'])

            await self.command(['reload'], allow_targeting=False)

    async def on_namechange(self, source, dest, **kwargs):
        """ The user config of a user has changed, so reload it """
        # TODO: check which username has changed to which value
        self.anon_map.reload()


    async def teardown(self):
        """
        None required
        """
        return


    def is_anon_root(self, path):
        """
        The anon module sends pathes relative to the /anonymous/ directory.
        This means, that if there is no / in the path, it has to be a anonymous
        share folder.

        Strip the path from / at front and back before trying this
        """
        return not '/' in path.strip('/')

async def init(loop, mfsd):
    return MammutfsdBaseCommands(loop, mfsd)
