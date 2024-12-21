import sys
if sys.platform == 'esp8266' or sys.platform == 'esp32':
    from rotary_irq_esp import RotaryIRQ
elif sys.platform == 'pyboard':
    from rotary_irq_pyb import RotaryIRQ
elif sys.platform == 'rp2':
    from rotary_irq_rp2 import RotaryIRQ
else:
    print('Warning:  The Rotary module has not been tested on this platform')

import uasyncio as asyncio
from machine import Pin

class EventLoop():
    def __init__(self):
        pass

    def run(self, px, py, callback):
        # _px = Pin(px, Pin.IN, Pin.PULL_UP)
        # _py = Pin(py, Pin.IN, Pin.PULL_UP)
        self.r1 = RotaryIRQ(
            py,
            px,
            min_val=0,
            max_val=100,
            incr=1,
            reverse=False,
            range_mode=RotaryIRQ.RANGE_UNBOUNDED,
            pull_up=False,
            half_step=False,
            invert=False)

        self.myevent = asyncio.Event()
        self.callback = callback
        self.r1.add_listener(self.callback)

        print("ROTARY SETUP", self.r1.value())

        try:
            asyncio.create_task(self.action())

            print("action task created")
        except KeyboardInterrupt:
            print("Interrupted")
        finally:
            asyncio.Loop.run_forever()

    def callback(self):
        print('ENCODER:  rotary 1 = {}'. format(self.r1.value()))
        self.myevent.set()

    async def action(self):
        while True:
            await self.myevent.wait()
            print('ENCODER:  rotary 1 = {}'. format(self.r1.value()))
            callback(self.r1.value())

            # do something with the encoder result ...
            self.myevent.clear()
