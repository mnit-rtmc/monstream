# MonStream

MonStream is a streaming application for live video display on dedicated
monitors.  It can stream video from unicast or multicast sources in MPEG2,
MPEG4, H264 and motion JPEG formats.  Streams can be displayed in a grid of 2x2,
2x3, etc.

It is controlled by [IRIS] through a UDP [protocol] with short text messages.

## Dependencies

MonStream requires a few dependencies to be installed:

* gtk3
* gstreamer1
* gstreamer1-plugins-base
* libcurl

__Fedora__:

```bash
sudo dnf install gtk3-devel gstreamer1-devel gstreamer1-plugins-base-devel libcurl-devel
```

To enable [OpenH264]:

```bash
sudo dnf config-manager --set-enabled fedora-cisco-openh264
sudo dnf install gstreamer1-plugin-openh264
```

__Ubuntu__:

```bash
sudo apt install git gcc make libcurl4-openssl-dev libgtk-3-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-bad gstreamer1.0-libav
```

## Building

To build, clone the repository and run `make` in the repository root:

```bash
git clone https://github.com/mnit-rtmc/monstream.git
pushd monstream
make
popd
```

## Installation

Install the `monstream` program and enable caching:

```bash
sudo bash
install monstream/monstream /usr/local/bin/
adduser monstream
mkdir /var/lib/monstream
chown monstream /var/lib/monstream
exit
```

The `/var/lib/monstream` directory is used for configuration and caching data.
The files are managed automatically and are not meant to be edited.

## Running

The program should be run as the `monstream` user.

```bash
monstream --help
Usage: monstream [option]
  --version       Display version and exit
  --no-gui        Run headless (still connect to streams)
  --stats         Display statistics on stream errors
  --port [p]      Listen on given UDP port (default 7001)
  --sink VAAPI    Configure VA-API video acceleration
  --sink XVIMAGE  Configure xvimage sink (no acceleration)
```

## Control

For dedicated workstations, a joystick and keyboard can be used for pan / tilt /
zoom control and switching.  The switching functions can all be performed on
a dedicated USB numeric keypad.

Key                           | Function
------------------------------|----------------------------------------
<kbd>0</kbd> ... <kbd>9</kbd> | Add digit to __entry__ (up to 5 digits)
<kbd>Backspace</kbd>          | Remove final digit of __entry__
<kbd>.</kbd>                  | Change selected monitor to __entry__
<kbd>Enter</kbd>              | Switch camera to __entry__
<kbd>-</kbd>                  | Switch to "upstream" camera
<kbd>+</kbd>                  | Switch to "downstream" camera
<kbd>`*`</kbd>                | Start / pause sequence __entry__
<kbd>/</kbd>                  | Recall preset __entry__
<kbd>Tab</kbd>                | Hide on-screen control bar

A joystick can be used to control the currently selected camera.  Pressing
_left_ and _right_ sends pan commands, while _up_ and _down_ causes the camera
to tilt.  Some models have a third axis for zoom, which can be controlled by
twisting the joystick.


[IRIS]: https://github.com/mnit-rtmc/iris
[OpenH264]: https://docs.fedoraproject.org/en-US/quick-docs/openh264/#_installation_from_fedora_cisco_openh264_repository
[protocol]: doc/protocol.md
