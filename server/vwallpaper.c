#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <readline/readline.h>
#include <readline/history.h>

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

#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "xwin.h"

#define BUFFER 5120 //more than enough
#define SOCKPATH "/tmp/vwallpaper-%s"
#define PROMPT "VWallpaper $ "

#define MPV "mpv --input-file=/dev/stdin --no-input-terminal --idle --really-quiet --fs --panscan=1 --wid=0x%x"
#define MPVNOA " --no-audio" //leading \s is important!
//{ "command": ["set_property", "loop", "inf"] }
#define MPVLOOPON "{ \"command\": [\"set_property\", \"loop\", \"inf\"] }"
#define MPVLOOPOFF "{ \"command\": [\"set_property\", \"loop\", 0] }"

Display *disp;
Heads *heads;
Player **player;
int *audio;
int *loop;

extern char* desktop_name;;

/* TODO:
 * Communicate with mpv over a socket or fifo.
 * We need an socket or fifo for every head, and user, of course.
 */

void stop_mpv(int head) {
	destroy_player(disp, player[head]);
	player[head] = NULL;
}

void start_mpv(int head) {
	char* p;
	size_t mlen;
	//max. buffer length
	mlen = strlen(MPV)+strlen(MPVNOA)+WID_LEN+1;
	p = malloc(sizeof(char)*mlen);
	//buffer length are safe here
	strcpy(p, MPV);
	if (!audio[head]) strcat(p, MPVNOA);

	player[head] = create_player(head, disp, heads, mlen, p);
	free(p);
	
	if (loop[head]) forward(player[head], MPVLOOPON);
	else forward(player[head], MPVLOOPOFF);
}

// [head:] command
// command: raw mpv command, either something like 'loadfile /path/to/video' or JSON-IPC string
// pros: two way communication with mpv, enabling restart without dropping the current playlist
void do_stuff(const char* cmd) {
	
	char *command;
	int onhead, i, conv;
	
	conv = sscanf(cmd, "%d: %m[^\n]s", &onhead, &command);
	
	if (conv < 2) {
		onhead = -1;
		command = malloc(sizeof(char)*strlen(cmd));
		strcpy(command, cmd);
	}
	if (onhead >= (int) heads->n || (conv == 2 && onhead == -1)) {
		//wrong head number
		//printf("wrong head number %d\n", onhead);
		return;
	}
	/* iter through all heads */
	if (onhead < 0) {
		i = 0;
		onhead = heads->n;
	}
	/* only the given head */
	else i = onhead++;

	for (;i<onhead;i++){
		if (strcmp(command, "quit") == 0) {
			if (player[i]) stop_mpv(i);
			continue;
		}
		if (strcmp(command, "audio on") == 0) {
			audio[i] = 1;
			stop_mpv(i);
			//start_mpv(i);
			continue;
		}
		if (strcmp(command, "audio off") == 0) {
			audio[i] = 0;
			stop_mpv(i);
			//start_mpv(i);
			continue;
		}
		if (strcmp(command, "loop on") == 0) {
			loop[i] = 1;
			forward(player[i], MPVLOOPON);
			continue;
		}
		if (strcmp(command, "loop off") == 0) {
			loop[i] = 0;
			forward(player[i], MPVLOOPOFF);
			continue;
		}
		if ( player[i] == NULL) {
			start_mpv(i);
		}
		forward(player[i], command);
	}
	free(command);
}

int rlinterface(void) {
	char* line;
	while ( (line = readline(PROMPT)) != NULL) {
		if (strcmp(line, "exit") == 0 || strcmp(line, "shutdown") == 0) break;
		//readline history
		add_history(line);
		//process command
		do_stuff(line);
		free(line);
	}
	return 0;
}

void teardown_sinterface(int lsock, char* sockpath) {
	close(lsock);
	unlink(sockpath);
}

int sinterface(const char* addr) {
	struct sockaddr_un address;
	socklen_t addrlen;
	int lsock, asock;
	char *buf = malloc(sizeof(char)*BUFFER);
	char *sopath, *user;
	ssize_t size;
	size_t psize;
	
	umask(0177);

	sopath = (char*) addr;
	//using default socket
	if (!sopath) {
		user = getenv("USER");
		psize = strlen(SOCKPATH)+strlen(user);
		sopath = malloc(sizeof(char)*(psize+1));
		snprintf(sopath, psize, SOCKPATH, user);
	}
	printf("using socket: %s\n", sopath);
	
	addrlen = sizeof(struct sockaddr_un);
	lsock = socket(AF_LOCAL, SOCK_STREAM, 0);
	address.sun_family = AF_LOCAL;
	strncpy(address.sun_path, sopath, 107);
	unlink(sopath);
	if (bind(lsock, (struct sockaddr*) &address, sizeof(address)) != 0){
		printf("error binding socket\n");
		return 1;
	}
	listen(lsock, 5);
	while(1) {
		asock = accept(lsock, (struct sockaddr*) &address, &addrlen);
		// every message to vwallpaper fits (it simply does) in the buffer
		// a message larger than the buffer, is defined as an invalid message,
		// so there no need to merge messages
		do {
			if (asock > 0) {
				size = recv(asock, buf, BUFFER-1, 0);
				if (size <= 0) break;
				//remove trailing \n
				else if (buf[size-1]=='\n') size--;
				buf[size] = '\0';
				//printf("%s\n", buf);
				if (strcmp(buf, "shutdown") == 0) {
					close(asock);
					teardown_sinterface(lsock, sopath);
					return 1;
				}
				else do_stuff(buf);
			}
			else break;
		} while (buf[0] != '\0');
		close(asock);
	}
	return 1;
}

void printhelp(void) {
	printf("vwallpaper [-w win] [-i winid] [-d [/path/to/socket]]\n");
	printf("\t-w <win>: use name as desktop window\n");
	printf("\t-d: use socket interface\n");
}

//vwallpaper [-d] [/path/to/socket]
int main(int argc, char** argv) {
	int i, ret;
	int soi = 0;
	char* sockpath = NULL;
	
	disp = XOpenDisplay(NULL);
	heads = get_heads(disp);
	
	player = malloc(sizeof(Player*)*heads->n);
	audio = malloc(sizeof(int)*heads->n);
	loop = malloc(sizeof(int)*heads->n);
	for (i=0; i<heads->n; i++) {
		player[i] = NULL;
		audio[i] = 0;
		loop[i] = 1;
	}
	
	// checking for some arguments
	for (i=1; i<argc; i++) {
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			printhelp();
			return 1;
		}
		if (strcmp(argv[i], "-w") == 0) if (i++ < argc) desktop_name = argv[i];
		if (strcmp(argv[i], "-d") == 0) {
			soi = 1;
			if (i++ < argc) sockpath = argv[i];
		}
	}
	
	//main loop
	if (soi) ret = sinterface(sockpath); //socket
	else ret = rlinterface(); //terminal, fifo or whatsoever

	//teardown
	for (i=0; i<heads->n; i++) destroy_player(disp, player[i]);
	free(player);
	free(heads);
	XCloseDisplay(disp);

	return ret;
}
