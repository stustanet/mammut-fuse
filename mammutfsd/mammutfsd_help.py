import asyncio

class MammutfsdHelp:
    def __init__(self, loop, mfsd):
        self.mfsd = mfsd
        self.loop = loop

        self.mfsd.register('help', self.display_help)

    async def display_help(self, _):
        for command, issuer in self.mfsd._commands.items():
            print(" %s"%command)
            for plugin in issuer:
                try:
                    print(plugin.get_help(command))
                except AttributeError:
                    print("\t|-", plugin.__qualname__)
                except KeyError:
                    print("\t|- Invalid Command for", plugin.__qualname__)

    async def teardown(self):
        return

async def init(loop, mfsd):
    return MammutfsdHelp(loop, mfsd)
