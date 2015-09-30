#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>

#include <X11/extensions/shape.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xrandr.h>

#define WID_LEN 7

typedef struct {
	int x;
	int y;
	unsigned int width;
	unsigned int height;
} geom_t;

typedef struct {
	Display *dpy;
	unsigned int n;
	geom_t *geometry;
} Heads;

typedef struct {
	FILE *pipe;
	Window win;
} Player;

Heads* get_heads(Display *dpy);
Window create_window(Display *dpy, geom_t geom);
Window create_window_on_head(Display *dpy, Heads* heads, int head);
FILE* start_mplayer(Window* win, size_t mlen, const char* mplayer);
void destroy_player(Display* disp, Player* p);
void forward(Player* p, const char* s);
Player* create_player(int head, Display* disp, Heads* heads, size_t mlen, const char* mplayer);


