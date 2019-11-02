import asyncio

class MammutfsdHelp:
    """
    Help display module, used to display all available commands
    """
    def __init__(self, loop, mfsd):
        self.mfsd = mfsd
        self.loop = loop

        self.mfsd.register('help', self.display_help)

    async def display_help(self, client, writer, _):
        for command, issuer in self.mfsd._commands.items():
            writer.write((" %s\n"%command).encode('utf-8'))
            for plugin in issuer:
                try:
                    writer.write((plugin.get_help(command) + "\n").encode('utf-8'))
                except AttributeError:
                    # Maybe we can even generate the method call?
                    writer.write(("\t|- %s\n"%plugin.__qualname__).encode('utf-8'))
                except KeyError:
                    writer.write(
                        ("\t|- Invalid Command for %s\n"%plugin.__qualname__)
                        .encode('utf-8'))
            await writer.drain()

    async def teardown(self):
        return

async def init(loop, mfsd):
    return MammutfsdHelp(loop, mfsd)
