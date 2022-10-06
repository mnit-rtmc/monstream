# MonStream

MonStream is a video monitor streaming application.  It can stream multicast
or unicast video and display it full screen, or in a grid of 2x2, 2x3, etc.
It is controlled through a UDP [protocol] with short text messages.

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


[protocol]: doc/protocol.md
