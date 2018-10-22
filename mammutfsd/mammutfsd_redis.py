import asyncio

class RedisFiller:
    """
    Redis interaction module, will fill all changed public files into the redis
    queue for indexing and hashing
    """
    def __init__(self, loop, mfsd):
        self.mfsd = mfsd
        self.loop = loop

        mfsd.register("redis-stats", self.stats)

    async def stats(self, client, writer, args):
        writer.write(b"Sorry no stats for you\n")


async def init(loop, mfsd):
    redis = RedisFiller(loop, mfsd)
    return redis
