import asyncio

class MammutfsdHelp:
    """
    Help display module, used to display all available commands
    """
    def __init__(self, loop, mfsd):
        self.mfsd = mfsd
        self.loop = loop

        self.mfsd.register('help', self.display_help)

    async def display_help(self, _):
        for command, issuer in self.mfsd._commands.items():
            await self.mfsd.write(" %s\n"%command)
            for plugin in issuer:
                try:
                    await self.mfsd.write(plugin.get_help(command) + "\n")
                except AttributeError:
                    await self.mfsd.write("\t|- %s\n"%plugin.__qualname__)
                except KeyError:
                    await self.mfsd.write(
                        "\t|- Invalid Command for %s\n"%plugin.__qualname__)

    async def teardown(self):
        return

async def init(loop, mfsd):
    return MammutfsdHelp(loop, mfsd)
