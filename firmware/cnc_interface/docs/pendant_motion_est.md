# Pendant Jog — Accumulating Mode

## The problem with one-click-one-move

The naive jog approach issues a G-Code move for every encoder tick. At low
scroll speeds this is fine, but at higher speeds the CNC controller's input
queue fills up with many tiny moves. The machine has to start, accelerate,
decelerate, and stop for each one — wasting time and producing jerky motion.

## The accumulating queue

Instead of sending a move immediately, each encoder tick is posted to a small
FreeRTOS queue. A dedicated task drains that queue and **sums all pending
ticks into a single move**. Even without the motion-estimation sleep (see
below), this alone reduces the number of G-Code commands sent at high scroll
speeds because multiple ticks will naturally pile up in the queue during the
brief time between task wakeups.

## Motion-estimation sleep (`JOG_ACCUM_MOTION_ESTIMATION=1`)

After sending the combined move, the task estimates roughly how long the
machine will take to execute it and **sleeps for most of that duration**. While
it sleeps, more encoder ticks accumulate in the queue. When the task wakes up
(slightly before the move finishes), it drains the queue again and sends the
next batch.

The duration estimate uses a simple trapezoidal velocity model: the machine
accelerates to its jog feed rate, travels, then decelerates. If the move is
short enough that it never reaches full speed, a triangular profile is used
instead. Both cases only need the feed rate and the axis acceleration — no
connection to the controller required.

The result is **self-regulating**: scroll slowly and each batch is one or two
ticks; scroll quickly and the sleep fills with many ticks, so the next move is
proportionally larger. The controller receives fewer, longer moves regardless
of how fast the encoder is turned.

## Simple mode (`JOG_ACCUM_MOTION_ESTIMATION=0`)

The sleep is skipped entirely. The task just yields and immediately waits for
the next batch. The queue still batches any ticks that arrive while the G-Code
is being dispatched through the send task, but there is no deliberate pacing.
Useful as a baseline or when the machine's real acceleration values are
unknown.

## Configuration

All tuning values are `#define`s in `tasks/jog_accumulator_task.h` and can
be overridden per-board in `platformio.ini` via `build_flags`:

| Define | Default | Notes |
|---|---|---|
| `JOG_ACCUM_MOTION_ESTIMATION` | `1` | `0` = simple mode |
| `JOG_ACCUM_FEED_XY_MM_MIN` | `3000` | Jog feed for X and Y |
| `JOG_ACCUM_FEED_Z_MM_MIN` | `1000` | Jog feed for Z |
| `JOG_ACCUM_ACCEL_X_MM_S2` | `500` | X acceleration |
| `JOG_ACCUM_ACCEL_Y_MM_S2` | `500` | Y acceleration |
| `JOG_ACCUM_ACCEL_Z_MM_S2` | `200` | Z acceleration (typically lower) |
| `JOG_ACCUM_LEAD_AHEAD_MS` | `30` | Wake up this many ms early |
| `JOG_ACCUM_MIN_SLEEP_MS` | `10` | Floor on computed sleep |
| `JOG_ACCUM_MAX_SLEEP_MS` | `500` | Ceiling on computed sleep |
| `JOG_ACCUM_QUEUE_LEN` | `64` | Click queue depth |

The feed and acceleration values do **not** need to match the controller
exactly — they just need to be in the right ballpark. Over-estimating the
move duration is safe (the task wakes up late and starts the next move
slightly after the previous one finishes). Under-estimating causes the task
to send the next move before the previous one is done, which the controller
will queue internally just as before.
