loop2rtmp: app.c
						gcc app.c -o loop2rtmp -g -Wall `pkg-config --cflags --libs gstreamer-0.10`

clean: loop2rtmp
				rm loop2rtmp
