# MonStream Protocol

Messages are received on UDP port 7001, by default.  Each message must end with
an ASCII record separator (U+001E).  Within messages, fields are separated by
the ASCII unit separator (U+001F).

## Received Messages

### Config

To begin configuration, a `config` message is sent with a value of `0`.  Then,
a series of `monitor` messages is sent, to configure each monitor.  Afterwards,
another `config` message is sent with the total monitor count.  At this point,
configuration is completed.

1. `config`
2. Monitor count, or `0` to enter config mode

### Monitor

1. `monitor`
2. Monitor index (`0` or higher; max `15` for grid)
3. Monitor ID (blank for overlay mode)
4. Accent color (rgb hex: 000000 -> black)
5. Force-aspect-ratio (`0` or `1`)
6. Font size (pt)
7. Four-letter crop code (`ABAB` -> upper-left quad).
   Horizontal index, horizontal span, vertical index, vertical span
8. Horizontal gap (`1` -> 0.01%)
9. Vertical gap (`20` -> 0.2%)
10. Extra monitor label (full-screen)

### Sink

1. `sink`
2. Sink name (`XVIMAGE` or `VAAPI`)

### Play

1. `play`
2. Monitor index
3. Camera ID
4. Stream request URI
5. Encoding: `MPEG2`, `MPEG4`, `H264`, `PNG`
6. Title: ASCII text description
7. Latency (0-2000 ms)

### Display

Sent in response to query message.

1. `display`
2. Monitor ID
3. Camera ID
4. Sequence # (ending with " if paused)

### Heartbeat

Send every 30 seconds.

1. `heartbeat`

## Messages from VDU to IRIS

### Status

Sent once per second for each monitor.  Destination port is taken from last
received command.

1. `status`
2. Monitor index
3. Camera ID
4. Stream status error (blank for OK)
5. Mode: "full" or ""

### Query

Sent every 300 ms when USB joystick connected.

1. `query`
2. Monitor ID

### Switch

1. `switch`
2. Monitor ID
3. Camera ID

### Next camera

1. `next`
2. Monitor ID

### Previous camera

1. `previous`
2. Monitor ID

### Sequence

1. `sequence`
2. Monitor ID
3. pause / Sequence #

### Pan/tilt/zoom

For held values, must be sent every second or will timeout.

1. `ptz`
2. Monitor ID
3. Camera ID
4. Pan value (-1.0 to 1.0)
5. Tilt value (-1 to 1)
6. Zoom value (-1 to 1)

### Lens

1. `lens`
2. Monitor ID
3. Camera ID
4. `iris_open` / `iris_close` / `iris_stop` / `focus_near` / `focus_far` / `focus_stop` / `wiper`

### Preset

1. `preset`
2. Monitor ID
3. Camera ID
4. `recall` / `store`
5. Preset #

### Menu

1. `menu`
2. Monitor ID
3. Camera ID
4. `open` / `enter` / `cancel`
