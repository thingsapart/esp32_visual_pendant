
class StreamReader:
    def __init__(self, wrapped):
        self.wrapped = wrapped

    def read(self):
        return self.wrapped.read()

    def readline(self):
        return self.wrapped.readline()

async def wait_for(res, timeout = 0.5):
    return res
