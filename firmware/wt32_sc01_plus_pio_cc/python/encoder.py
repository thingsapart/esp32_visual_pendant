from machine import Pin, Encoder
from uasyncio import sleep, create_task, Loop, CancelledError
import uasyncio

class EventLoop:
    def __init__(self):
        self.position = 0

    def cb(self, pos, delta):
        #print('ENCODER >>', pos, delta)
        self.callback(pos)

    async def main(self):
        while True:
            pos = self.encoder.value()
            if pos != self.position:
                self.callback(pos)
                self.position = pos
            await uasyncio.sleep_ms(1000//10)

    def run(self, ppx, ppy, callback):
        print('Starting event loop')
        from machine import Pin
        px = Pin(ppx, Pin.IN, Pin.PULL_UP)
        py = Pin(ppy, Pin.IN, Pin.PULL_UP)

        enc = Encoder(0, px, py, filter_ns=500, phases=4)

        self.callback = callback
        self.encoder = enc

        #######################################################################
        # Start event loop
        ######################################################################
        print('Encoder evt loop...')
        try:
            create_task(self.main())
        except KeyboardInterrupt:
            print("Interrupted")

if __name__ == '__main__':
    def update_v(v):
        print("V: ", v)
    evt = EventLoop()
    evt.run(10, 11, update_v)

