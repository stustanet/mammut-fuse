import asyncio

class MammutfsdBaseCommands:
    def __init__(self, loop, mfsd):
        self.mfsd = mfsd
        self.loop = loop

        self.mfsd.register('clear', self.command)
        self.mfsd.register('reload', self.command)

        self.mfsd.register('setconfig', self.setconfig)
        self.mfsd.register('getconfig', self.getconfig)

        self.mfsd.register('clients', self.getclients)
        self.mfsd.register('select', self.focus_client)

        self.selected_client = None

    async def send(self, cmd, allow_targeting):
        """
        proxy to allow easy setting of a target
        """
        if not allow_targeting or not self.selected_client:
            await self.mfsd.sendall(cmd)
        else:
            await self.selected_client.send(cmd)

    async def command(self, cmd, allow_targeting=True):
        try:
            cmd = {
                'clear':"CLEARCACHE",
                'reload':"FORCE-RELOAD"
                } [cmd[0]]
            await self.send(cmd, allow_targeting)
        except KeyError:
            # Unknown command
            raise

    async def setconfig(self, cmd):
        pass
    async def getconfig(self, cmd):
        pass
    async def getclients(self, cmd):
        pass
    async def focus_client(self, cmd):
        pass

    async def on_fileop(self, fileop):
        if (fileop['op'] in ('MKDIR', 'RMDIR')
            and fileop['module'] == 'anonym'
            and self.is_anon_root(fileop['path'])):
            # TODO Recreate anon cache
            await self.command('reload', allow_targeting=False)

        if (fileop['op']

        print(fileop)

    async def teardown(self):
        return


    def is_anon_root(self, path):
        # TODO
        return True

    def translate_anon_root(self, path):
        # TODO
        return path

async def init(loop, mfsd):
    return MammutfsdBaseCommands(loop, mfsd)
