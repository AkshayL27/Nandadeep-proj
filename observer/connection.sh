gst-launch-1.0 -v libcamerasrc ! \
  video/x-raw,width=1920,height=1080,framerate=30/1 ! \
  videoconvert ! \
  v4l2h264enc bitrate=10000000 ! video/x-h264,profile=high ! \
  h264parse ! rtph264pay config-interval=1 pt=96 mtu=1400 ! \
  udpsink host=224.1.1.1 port=5000 auto-multicast=true
