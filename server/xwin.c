#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <readline/readline.h>

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

#include "xwin.h"

#define OPAQUE 0xffffffff
#define DEBUG_MSG(x) if(debug) { fprintf(stderr, x); }

char* desktop_name = NULL;

Heads* get_heads(Display *dpy) {
	XRRScreenResources *crtc;
	XRRCrtcInfo *crtc_info;
	Heads *heads;
	geom_t *geom;
	int i;
	
	crtc = XRRGetScreenResources (dpy, DefaultRootWindow(dpy));

	heads = malloc(sizeof(Heads));
	heads->dpy = dpy;
	heads->n = crtc->noutput;
	geom = malloc(heads->n * sizeof(geom_t));
	for (i = 0; i < crtc->noutput; i++) {
		crtc_info = XRRGetCrtcInfo(dpy, crtc, crtc->crtcs[i]);
		geom[i].x = crtc_info->x;
		geom[i].y = crtc_info->y;
		geom[i].width = crtc_info->width;
		geom[i].height = crtc_info->height;
		XFree(crtc_info);
		
	}
	heads->geometry = geom;
	XFree(crtc);
	return heads;
}


Window find_desktop_window(Display *disp, Window *root) {
	int status;
	unsigned int i, n;
	Window root_of, parent;
	Window *children;
	Window win = 0;
	char *name;

	status = XQueryTree(disp, *root, &root_of, &parent, &children, &n);
	if (!status) return 0; //error
	
	for (i=0; i<n; i++) {
		status = XFetchName(disp, children[i], &name);
		if (status && name && desktop_name && strcmp(name, desktop_name) == 0) {
			//desktop window found!
			win = children[i];
		}
		free(name);
		name = NULL;
		if (!win) win = find_desktop_window(disp, &children[i]);
		if (win) break;
	}
	XFree(children);
	return win;
}

Window create_window(Display *dpy, geom_t geom) {
	Window win;
	Window root, desktop;
	int screen;
	
	if (!dpy) {
		fprintf(stderr, "Error: couldn't open display!\n");
		return -1;
	}

	screen = XDefaultScreen(dpy);
	root = XRootWindow(dpy,screen);
	//root = XDefaultRootWindow(dpy);
	
	if (desktop_name) {
		desktop = find_desktop_window(dpy, &root);
		//desktop window not found
		if (desktop == 0) desktop = root;
	}
	else desktop = root;
	
	XSetWindowAttributes attr;
	//always override
	attr.override_redirect = 1;
	win = XCreateWindow(dpy, desktop, geom.x, geom.y, geom.width, geom.height, 0, CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect, &attr);
	XMapWindow(dpy, win);
	//XRaiseWindow(dpy, win);
	// if there's no desktop window, like using a plain windowmanager
	// without the whole DE stuff, the windows needs to be lowered, otherwise
	// it's painted above all windows.
	// -- tested with fluxbox, without any kind of DE --
	if (root == desktop) XLowerWindow(dpy, win);
	XSync(dpy, win);
	return win;
}

Window create_window_on_head(Display *dpy, Heads* heads, int head) {
	Window win;
	win = create_window(dpy, heads->geometry[head]);
	return win;
}

FILE* start_player(Window* win, size_t mlen, const char* player_cmd) {
	FILE *pipe;
	char* cmd;
	cmd = malloc(sizeof(char)*(mlen+1));
	snprintf(cmd, mlen, player_cmd, (int) *win);
	pipe = popen(cmd, "w");
	free(cmd);
	return pipe;
}

void stop_player(FILE *pipe) {
	fprintf(pipe, "quit\n");
	fflush(pipe);
	pclose(pipe);
}

Player* create_player(int head, Display* disp, Heads* heads, size_t mlen, const char* player_cmd) {
	Player* out;
	Region region;
	
	out = malloc(sizeof(Player));
	out->win = create_window_on_head(disp, heads, head);
	
	//noinput
	region = XCreateRegion();
	if (region) {
		XShapeCombineRegion(disp, out->win, ShapeInput, 0, 0, region, ShapeSet);
		XDestroyRegion(region);
	}
	
	out->pipe = start_player(&out->win, mlen, player_cmd);
	XSync(disp, out->win);
	return out;
}

void destroy_player(Display* disp, Player* p) {
	if ( p != NULL) {
		stop_player(p->pipe);
		XDestroyWindow(disp, p->win);
		XSync(disp, p->win);
	}
	free(p);
}

void forward(Player* p, const char* s) {
	fprintf(p->pipe, "%s\n", s);
	fflush(p->pipe);
}




