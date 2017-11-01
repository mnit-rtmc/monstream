# MonStream Protocol

## UDP command

Fields are separated by ASCII unit separator (31).
Commands are separated by ASCII record separator (30).

### Config command

1. config
2. Monitor count (1-16)

### Monitor command

1. monitor
2. Monitor index (0 to 15)
3. Monitor ID (blank for overlay mode)
4. Accent color (rgb hex: 000000 -> black)
5. Force-aspect-ratio (0 or 1)
6. Font size (pt)

### Play command

1. play
2. Monitor index (0 to 15)
3. Camera ID
4. Stream request URI
5. Encoding: "MPEG2", "MPEG4", "H264", "PNG"
6. Title: ASCII text description
7. Latency (0-2000 ms)

## Asynchronous UDP status

Sent once per second for each monitor.  Destination port is taken from last
received command.

1. status
2. Monitor index (0 to 15)
3. Camera ID
4. Stream status error (blank for OK)
