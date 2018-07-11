import asyncio

class RedisFiller:
    def __init__(self, loop, mfsd):
        self.mfsd = mfsd
        self.loop = loop

        mfsd.register("stats", self.stats)

    async def init(self):
        pass


    async def stats(self, args):
        print("ARGS: ", args)
        print("Sorry no stats for you")


async def init(loop, mfsd):
    redis = RedisFiller(loop, mfsd)
    await redis.init()
    return redis
