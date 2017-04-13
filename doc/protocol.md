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
3. Monitor ID
4. Accent color (rgb hex: 000000 -> black)
5. Force-aspect-ratio (0 or 1)

### Play command

1. play
2. Monitor index (0 to 15)
3. Camera ID
4. Stream request URI
5. Stream type: "MPEG4", "H264", "PNG"
6. Title: ASCII text description
7. Latency (0-2000 ms)

## UDP response

Must contain 1 field

1. Error description, or empty string for ACK
