
.PHONY: clean

all: vwallpaper

vwallpaper: xwin.c vwallpaper.c
	gcc -Wall -lX11 -lXext -lXrender -lXrandr -lreadline xwin.c vwallpaper.c -o vwallpaper

clean:
	rm -f vwallpaper
