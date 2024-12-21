# MIT License
#
# Copyright (c) 2016 Peter Hinch
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# https://github.com/peterhinch/micropython-async/

from machine import Pin
from uasyncio import sleep, create_task, Loop, CancelledError
import uasyncio

class Encoder:
    def __init__(
        self,
        pin_x,
        pin_y,
        v=0,
        div=1,
        vmin=None,
        vmax=None,
        mod=None,
        callback=lambda a, b: None,
        args=(),
        delay=100,
    ):
        self._pin_x = pin_x
        self._pin_y = pin_y
        self._x = pin_x()
        self._y = pin_y()
        self._v = v * div  # Initialise hardware value
        self._cv = v  # Current (divided) value
        self.delay = delay  # Pause (ms) for motion to stop/limit callback frequency
        self._trig = uasyncio.Event()

        if ((vmin is not None) and v < vmin) or ((vmax is not None) and v > vmax):
            raise ValueError("Incompatible args: must have vmin <= v <= vmax")
        self._tsf = uasyncio.ThreadSafeFlag()
        trig = Pin.IRQ_RISING | Pin.IRQ_FALLING
        try:
            xirq = pin_x.irq(trigger=trig, handler=self._x_cb, hard=True)
            yirq = pin_y.irq(trigger=trig, handler=self._y_cb, hard=True)
        except TypeError:  # hard arg is unsupported on some hosts
            xirq = pin_x.irq(trigger=trig, handler=self._x_cb)
            yirq = pin_y.irq(trigger=trig, handler=self._y_cb)
        uasyncio.create_task(self._run(vmin, vmax, div, mod, callback, args))

    # Hardware IRQ's. Duration 36μs on Pyboard 1 ~50μs on ESP32.
    # IRQ latency: 2nd edge may have occured by the time ISR runs, in
    # which case there is no movement.
    def _x_cb(self, pin_x):
        # print("IRQ X")
        if (x := pin_x()) != self._x:
            # print(x, self._x, self._v)
            self._x = x
            self._v += 1 if x ^ self._pin_y() else -1
            self._tsf.set()

    def _y_cb(self, pin_y):
        # print("IRQ Y")
        if (y := pin_y()) != self._y:
            # print(y, self._y, self._v)
            self._y = y
            self._v -= 1 if y ^ self._pin_x() else -1
            self._tsf.set()

    async def _run(self, vmin, vmax, div, mod, cb, args):
        pv = self._v  # Prior hardware value
        pcv = self._cv  # Prior divided value passed to callback
        lcv = pcv  # Current value after limits applied
        plcv = pcv  # Previous value after limits applied
        delay = self.delay
        while True:
            self._tsf.clear()
            await self._tsf.wait()  # Wait for an edge. A stopped encoder waits here.
            await uasyncio.sleep_ms(delay)  # Optional rate limit for callback/trig.
            hv = self._v  # Sample hardware (atomic read).
            if hv == pv:  # A change happened but was negated before
                continue  # this got scheduled. Nothing to do.
            pv = hv
            cv = round(hv / div)  # cv is divided value.
            if not (dv := cv - pcv):  # dv is change in divided value.
                continue  # No change
            lcv += dv  # lcv: divided value with limits/mod applied
            lcv = lcv if vmax is None else min(vmax, lcv)
            lcv = lcv if vmin is None else max(vmin, lcv)
            lcv = lcv if mod is None else lcv % mod
            self._cv = lcv  # update ._cv for .value() before CB.
            if lcv != plcv:
                cb(lcv, lcv - plcv, *args)  # Run user CB in uasyncio context
                self._trig.set()  # Enable async iterator
            pcv = cv
            plcv = lcv

    def __aiter__(self):
        return self

    async def __anext__(self):
        await self._trig.wait()
        self._trig.clear()
        return self._cv

    def value(self):
        return self._cv

class EventLoop:
    def cb(self, pos, delta):
        print('ENCODER >>', pos, delta)
        self.callback(pos)

    async def main(self):
        while True:
            await uasyncio.sleep(1)

    def run(self, px, py, callback):
        print('Starting event loop')
        from machine import Pin
        px = Pin(px, Pin.IN, Pin.PULL_UP)
        py = Pin(py, Pin.IN, Pin.PULL_UP)
        enc = Encoder(px, py, v=0, callback=self.cb, div=4, delay=1)
        self.callback = callback

        ##################################################################################################
        # Start event loop
        ##################################################################################################
        try:
            create_task(self.main())
        except KeyboardInterrupt:
            print("Interrupted")
        finally:
            Loop.run_forever()
