# MonStream

MonStream is a video monitor streaming application.  It can stream multicast
or unicast video and display it full screen, or in a grid of 2x2, 2x3, etc.
It is controlled through a UDP protocol with short text messages.

## Installation

MonStream has a few build dependencies:

* gtk3
* gstreamer1
* gstreamer1-plugins-base
* libcurl

The `devel` packages for these can be installed on Fedora with:

```
dnf install gtk3-devel gstreamer1-devel gstreamer1-plugins-base-devel libcurl-devel
```

To build MonStream, run `make` in the repository root.

### Configuration

The `/var/lib/monstream` directory is used for caching data.  Create it and give
the monstream user write access.

These files are managed automatically and are not meant to be edited:

* `config` - Number of monitors to display in grid
* `sink` - Gstreamer sink element for display
* `monitor.0` .. `monitor.n` - Parameters for each monitor
* `play.0` .. `play.n` - Stream information for each monitor
* `cache` - Directory containing SDP files for known streams
