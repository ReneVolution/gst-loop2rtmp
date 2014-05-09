VERSION=1.0

loop2rtmp: app.c
						gcc app.c -o loop2rtmp -g -Wall `pkg-config --cflags --libs gstreamer-$(VERSION)`

legacy: VERSION=0.10
legacy:	loop2rtmp
						
clean: loop2rtmp
				rm loop2rtmp
