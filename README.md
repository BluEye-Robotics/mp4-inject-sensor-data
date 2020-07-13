# mp4-inject-sensor-data
Proof of concept for injecting sensor data into mp4 videos using gopros [gpmf](https://github.com/gopro/gpmf-parser) standard and a modified version of gstreamers [mp4mux](https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-good/html/gst-plugins-good-plugins-mp4mux.html) element which is implemented [here](https://gitlab.freedesktop.org/erlend_ne/gst-plugins-good/tree/mp4mux-add-gpmf-track-latest).

Example output from Quik Desktop after adding Gauges: https://youtu.be/wCWc-pXQkZE

## Build instructions

First make sure to clone the repository with the gpmf-parse submodule, either by running:
```
git clone https://github.com/BluEye-Robotics/mp4-inject-sensor-data.git --recursive
```
or
```
git clone https://github.com/BluEye-Robotics/mp4-inject-sensor-data.git
cd mp4-inject-sensor-data
git submodule init
git submodule update --recursive
```

Then you make a build directory and invoke cmake the normal way:
```
mkdir build
cd build
cmake ..
make
```

The output binary will be called "mp4-inject-sensor-data", and it will create a video with a gpmf-track.
This is provided that you use the custom version of gstreamer mentioned above.
