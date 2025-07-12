# https://github.com/IhorNehrutsa/MicroPython-quadrature-incremental-encoder/blob/main/encoder_state.py
# encoder_state.py

# Copyright (c) 2021 Ihor Nehrutsa
# Released under the MIT License (MIT) - see LICENSE file


class EncoderState:
    def __init__(self, phase_a, phase_b, x124=4, scale=1):
        self.pin_a = phase_a
        self.pin_b = phase_b
        self.x124 = x124
        self.scale = scale  # Optionally scale encoder rate to distance/angle etc.

        self._value = 0  # raw counter value

        self._state = 0  # encoder state transitions
        if x124 == 1:
            self._x = (0, 0, 1, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, 0, 0)
        elif x124 == 2:
            self._x = (0, 0, 1, 0, 0, 0, 0, -1, -1, 0, 0, 0, 0, 1, 0, 0)
        elif x124 == 4:
            self._x = (0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0)
        else:
            raise ValueError("multiplier x124 must be from [1, 2, 4]")

        try:
            print('> IRQ hard')
            self.pin_a.irq(self._callback, hard=True)
            self.pin_b.irq(self._callback, hard=True)
        except TypeError:
            print('> IRQ soft')
            self.pin_a.irq(self._callback)
            self.pin_b.irq(self._callback)

    def deinit(self):
        # use deinit(), otherwise _callback() will work after exiting the program
        try:
            self.pin_a.irq(None)
        except:
            pass
        try:
            self.pin_b.irq(None)
        except:
            pass

    def __repr__(self):
        return 'Encoder(phase_a={}, phase_a={}, x124={}, scale={})'.format(
            self.pin_a, self.pin_b, self.x124, self.scale)

    def _callback(self, pin):
        self._state = ((self._state << 2) + (self.pin_a() << 1) + self.pin_b()) & 0xF
        self._value += self._x[self._state]

    def value(self, value=None):
        _value = self._value
        if value is not None:
            self._value = value
        return _value

    def get_value(self):
        return self._value

    def position(self):
        return self._value

    def scaled(self, scaled=None):
        _scaled = self._value * self.scale
        if scaled is not None:
            self._value = round(scaled / self.scale)
        return _scaled

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

# encoder_portable.py

# Encoder Support: this version should be portable between MicroPython platforms
# Thanks to Evan Widloski for the adaptation to use the machine module

# Copyright (c) 2017-2022 Peter Hinch
# Released under the MIT License (MIT) - see LICENSE file

from machine import Pin


class Encoder:
    def __init__(self, pin_x, pin_y, scale=1):
        self.scale = scale
        self.forward = True
        self.pin_x = pin_x
        self.pin_y = pin_y
        self._x = pin_x()
        self._y = pin_y()
        self._pos = 0
        try:
            self.x_interrupt = pin_x.irq(trigger=Pin.IRQ_RISING | Pin.IRQ_FALLING, handler=self.x_callback, hard=True)
            self.y_interrupt = pin_y.irq(trigger=Pin.IRQ_RISING | Pin.IRQ_FALLING, handler=self.y_callback, hard=True)
        except TypeError:
            self.x_interrupt = pin_x.irq(trigger=Pin.IRQ_RISING | Pin.IRQ_FALLING, handler=self.x_callback)
            self.y_interrupt = pin_y.irq(trigger=Pin.IRQ_RISING | Pin.IRQ_FALLING, handler=self.y_callback)

    @micropython.native
    def x_callback(self, pin_x):
        if (x := pin_x()) != self._x:  # Reject short pulses
            self._x = x
            self.forward = x ^ self.pin_y()
            self._pos += 1 if self.forward else -1

    @micropython.native
    def y_callback(self, pin_y):
        if (y := pin_y()) != self._y:
            self._y = y
            self.forward = y ^ self.pin_x() ^ 1
            self._pos += 1 if self.forward else -1

    def position(self, value=None):
        if value is not None:
            self._pos = round(value / self.scale)  # Improvement provided by @IhorNehrutsa
        return int(self._pos * self.scale)

    def value(self, value=None):
        if value is not None:
            self._pos = value
        return self._pos


class EncoderAsync:
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

    @micropython.native
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

    @micropython.native
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

    def position(self):
        return self._cv

###############################################################################

#   STM32F405 specific (pboard equivalents) MicroPython code for quadrature output rotary encoders
#
#   Code below modified from original authors - Mike Teachman / Peter Hinch
#
#   Mike Teachman:
#   https://github.com/MikeTeachman/micropython-rotary
#
#   Peter Hinch:
#   https://github.com/peterhinch/micropython-async
#


import uasyncio as asyncio
from machine import Pin


_DIR_CW = const(0x10)  # Clockwise step
_DIR_CCW = const(0x20)  # Counter-clockwise step

# Rotary Encoder States
_R_START = const(0x0)
_R_CW_1 = const(0x1)
_R_CW_2 = const(0x2)
_R_CW_3 = const(0x3)
_R_CCW_1 = const(0x4)
_R_CCW_2 = const(0x5)
_R_CCW_3 = const(0x6)
_R_ILLEGAL = const(0x7)

_transition_table = [

    # |------------- NEXT STATE -------------|            |CURRENT STATE|
    # CLK/DT    CLK/DT     CLK/DT    CLK/DT
    #   00        01         10        11
    [_R_START, _R_CCW_1, _R_CW_1,  _R_START],             # _R_START
    [_R_CW_2,  _R_START, _R_CW_1,  _R_START | _DIR_CCW],  # _R_CW_1
    [_R_CW_2,  _R_CW_3,  _R_CW_1,  _R_START],             # _R_CW_2
    [_R_CW_2,  _R_CW_3,  _R_START, _R_START | _DIR_CW],   # _R_CW_3
    [_R_CCW_2, _R_CCW_1, _R_START, _R_START | _DIR_CW],   # _R_CCW_1
    [_R_CCW_2, _R_CCW_1, _R_CCW_3, _R_START],             # _R_CCW_2
    [_R_CCW_2, _R_START, _R_CCW_3, _R_START | _DIR_CCW],  # _R_CCW_3
    [_R_START, _R_START, _R_START, _R_START]]             # _R_ILLEGAL

# there may be a missing state here but I don't have a hafl step encoder to test
# with. Issue is missing step on direction change
_transition_table_half_step = [
    [_R_CW_3,            _R_CW_2,  _R_CW_1,  _R_START],
    [_R_CW_3 | _DIR_CCW, _R_START, _R_CW_1,  _R_START],
    [_R_CW_3 | _DIR_CW,  _R_CW_2,  _R_START, _R_START],
    [_R_CW_3,            _R_CCW_2, _R_CCW_1, _R_START],
    [_R_CW_3,            _R_CW_2,  _R_CCW_1, _R_START | _DIR_CW],
    [_R_CW_3,            _R_CCW_2, _R_CW_3,  _R_START | _DIR_CCW]]

_STATE_MASK = const(0x07)
_DIR_MASK = const(0x30)


def _wrap(value, incr, lower_bound, upper_bound):
    range = upper_bound - lower_bound + 1
    value = value + incr

    if value < lower_bound:
        value += range * ((lower_bound - value) // range + 1)

    return lower_bound + (value - lower_bound) % range


def _bound(value, incr, lower_bound, upper_bound):
    return min(upper_bound, max(lower_bound, value + incr))

class EncoderAsyncM():

    RANGE_UNBOUNDED = const(1)
    RANGE_WRAP = const(2)
    RANGE_BOUNDED = const(3)

    def __init__(self, pin_a, pin_b, vmin=None, vmax=None, reverse=False, range_mode=None, pull_up=False, half_step=False, callback=lambda a : None, args=()):
        self.pin_a = pin_a
        self.pin_b = pin_b
        self._vmin = vmin
        self._vmax = vmax
        self._reverse = -1 if reverse else 1
        self._range_mode = range_mode
        self._value = 0 # always start at 0
        self._state = _R_START
        self._half_step = half_step
        self._callback = callback
        self._args = args

        if ((vmin is not None) and vmin > vmax) or ((vmax is not None) and vmax < vmin):
            raise ValueError('Incompatible args: must have vmin < vmax or vmax > vmin')
        self._tsf = asyncio.ThreadSafeFlag()
        if pull_up == True:
            self._pin_a = Pin(pin_a, Pin.IN, Pin.PULL_UP)
            self._pin_b = Pin(pin_b, Pin.IN, Pin.PULL_UP)
        else:
            self._pin_a = Pin(pin_a, Pin.IN)
            self._pin_b = Pin(pin_b, Pin.IN)
        trig = Pin.IRQ_RISING | Pin.IRQ_FALLING

        #self._a_irq = ExtInt(pin_a, ExtInt.IRQ_RISING_FALLING, Pin.PULL_NONE, self._a_cb)
        #self._b_irq = ExtInt(pin_b, ExtInt.IRQ_RISING_FALLING, Pin.PULL_NONE, self._b_cb)
        try:
            print('> IRQ hard')
            self._a_irq = self._pin_a.irq(self._a_cb, hard=True, trigger=Pin.IRQ_RISING)
            self._b_irq = self._pin_b.irq(self._b_cb, hard=True, trigger=Pin.IRQ_RISING)
        except TypeError:
            print('> IRQ soft')
            self._a_irq = self._pin_a.irq(self._a_cb, trigger=Pin.IRQ_RISING)
            self._b_irq = self._pin_b.irq(self._b_cb, trigger=Pin.IRQ_RISING)

        asyncio.create_task(self._run())


    def set(self, value=None, vmin=None, vmax=None, reverse=None, range_mode=None):
        #   disable interrupts
        self._a_irq.disable()
        self._b_irq.disable()

        if value is not None:
            self._value = value
        if vmin is not None:
            self._vmin = vmin
        if vmax is not None:
            self._vmax = vmax
        if reverse is not None:
            self._reverse = -1 if reverse else 1
        if range_mode is not None:
            self._range_mode = range_mode
        self._state = _R_START

        # enable interrupts
        self._a_irq.enable()
        self._b_irq.enable()


    #    Hardware IRQ's
    def _a_cb(self, pin):
        self._tsf.set()

    def _b_cb(self, pin):
        self._tsf.set()

    def value(self):
        return self._value


    async def _run(self):
        while True:
            await self._tsf.wait()
            old_value = self._value
            a_b_pins = (self._pin_a.value() << 1) | self._pin_b.value()
            # Determine next state
            if self._half_step:
                self._state = _transition_table_half_step[self._state & _STATE_MASK][a_b_pins]
            else:
                self._state = _transition_table[self._state & _STATE_MASK][a_b_pins]

            direction = self._state & _DIR_MASK
            incr = 0
            if direction == _DIR_CW:
                incr = 1
            elif direction == _DIR_CCW:
                incr = -1

            incr *= self._reverse

            if self._range_mode == self.RANGE_WRAP:
                self._value = _wrap(self._value, incr, self._vmin, self._vmax)
            elif self._range_mode == self.RANGE_BOUNDED:
                self._value = _bound(self._value, incr, self._vmin, self._vmax)
            else:
                self._value = self._value + incr

            # print(self._value)
            try:
                if old_value != self._value:
                    self._callback(self._value, *self._args)    # User CB in uasyncio context
            except (KeyboardInterrupt, Exception) as e:
                print('Exception {} {}\n'.format(type(e).__name__, e))

###############################################################################


class EventLoop:
    def __init__(self):
        self.position = 0

    def cb(self, pos, delta):
        #print('ENCODER >>', pos, delta)
        self.callback(pos)

    async def main(self):
        while True:
            await uasyncio.sleep(1)

            #self.callback(pos)
            # pos = self.encoder.position()
            # if pos != self.position:
            #     self.callback(pos)
            #    self.position = pos
            # await uasyncio.sleep_ms(1000//10)

    def run(self, ppx, ppy, callback):
        print('Starting event loop')
        from machine import Pin
        px = Pin(ppx, Pin.IN, Pin.PULL_UP)
        py = Pin(ppy, Pin.IN, Pin.PULL_UP)

        # AsyncM.
        # enc = EncoderAsyncM(ppx, ppy, callback=callback)
        # Async.
        #enc = EncoderAsync(px, py, v=0, div=1, delay=5, callback=callback)
        #enc = EncoderAsync(px, py, v=0, callback=self.cb, div=4, delay=1)
        # IRQ 1
        #enc = Encoder(px, py, scale=0.25)
        # IRQ 2 state
        enc = EncoderState(px, py, x124=1)

        self.callback = callback
        self.encoder = enc

        ##################################################################################################
        # Start event loop
        ##################################################################################################
        try:
            create_task(self.main())
        except KeyboardInterrupt:
            print("Interrupted")
        finally:
            Loop.run_forever()

if __name__ == '__main__':
    def update_v(v):
        print("V: ", v)
    evt = EventLoop()
    evt.run(10, 11, update_v)

