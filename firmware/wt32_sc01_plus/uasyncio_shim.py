
class StreamReader:
    def __init__(self, wrapped):
        self.wrapped = wrapped

    def read(self):
        return self.wrapped.read()

    def readline(self):
        return self.wrapped.readline()

async def wait_for(res, timeout = 0.5):
    print("SHIM")
    return res

def run(t):
    try:
        while True:
            t.send(None)
    except StopIteration as exc:
        t.close()

type_gen = type((lambda: (yield))())

def run_until_complete(t):
    if isinstance(t, type_gen):
        try:
            while True:
                t.send(None)
        except StopIteration as exc:
            print(exc)
            return exc.value
    else:
        return t
