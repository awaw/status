/*
 * Copyright (c) 2013 Armin Wolfermann <armin@wolfermann.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/resource.h>
#include <sys/vmmeter.h>
#include <sys/mount.h>
#include <sys/sensors.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

#define FONT "XFT#Bitstream Vera Sans:size=9#1"
#define BARWIDTH 1280
#define BARHEIGHT 18
#define BARPOS 1006

Display *d;
Window w;
XftFont *xftfont;
XftColor white, black;
XftDraw *xftd;
int s;
time_t tick = (time_t)0;

static int
get_int_property(const char *propname) {
	Atom property;
	Atom type;
	int format;
	unsigned long remaining;
	unsigned long nitems;
	unsigned char *ptr;
	int ret;

	property = XInternAtom(d, propname, True);
	if (property == None) {
		fprintf(stderr, "Atom %s does not exist\n", propname);
		return (-1);
	}

	if (XGetWindowProperty(d, DefaultRootWindow(d), property, 0, 1, False,
			AnyPropertyType, &type, &format, &nitems, &remaining, &ptr) != Success) {
		fprintf(stderr, "XGetWindowProperty failed\n");
		return (-1);
	}

	if (type == None || format == 0) {
		fprintf(stderr, "Window is missing property %s\n", propname);
		return (-1);
	}

	if (remaining) {
		fprintf(stderr, "%s is too big for me\n", propname);
		return (-1);
	}

	if (format != 32) {
		fprintf(stderr, "%s has bad format\n", propname);
		return (-1);
	}

	ret = *ptr;
	XFree(ptr);
	return ret;
}

static void
desktops(int start)
{
	char buf[4];
	XGlyphInfo extents;
	int x, i, curdesk, numdesk;
	int cellwidth = 14;

	curdesk = get_int_property("_NET_CURRENT_DESKTOP");
	numdesk = get_int_property("_NET_NUMBER_OF_DESKTOPS");

	for(i=0, x=start; i<numdesk; i++, x+=cellwidth) {
		sprintf(buf, "%d", i+1);
		if (i == curdesk) {
			XftDrawRect(xftd, &white, x, 0, cellwidth, BARHEIGHT);
			XftTextExtentsUtf8(d, xftfont, (const FcChar8 *)buf, strlen(buf), &extents);
			XftDrawStringUtf8(xftd, &black, xftfont, x + (cellwidth - extents.xOff) / 2, 2 + xftfont->ascent, (const FcChar8 *)buf, strlen(buf));
		} else {
			XftDrawRect(xftd, &black, x, 0, cellwidth, BARHEIGHT);
			XftTextExtentsUtf8(d, xftfont, (const FcChar8 *)buf, strlen(buf), &extents);
			XftDrawStringUtf8(xftd, &white, xftfont, x + (cellwidth - extents.xOff) / 2, 2 + xftfont->ascent, (const FcChar8 *)buf, strlen(buf));
		}
	}
}

static void
loadaverage(int start)
{
	struct loadavg load;
	size_t size;
	int mib[2];
	char buf[32];
	XGlyphInfo extents;

	mib[0] = CTL_VM;
	mib[1] = VM_LOADAVG;
	size = sizeof(load);
	if (sysctl(mib, 2, &load, &size, NULL, 0) < 0)
		return;

	sprintf(buf, "Load: %.2f %.2f %.2f",
	    (double) load.ldavg[0] / load.fscale,
	    (double) load.ldavg[1] / load.fscale,
	    (double) load.ldavg[2] / load.fscale);

	XftTextExtentsUtf8(d, xftfont, (const FcChar8 *)buf, strlen(buf), &extents);
	XftDrawRect(xftd, &black, start, 0, extents.xOff, BARHEIGHT);
	XftDrawStringUtf8(xftd, &white, xftfont, start, 1 + xftfont->ascent, (const FcChar8 *)buf, strlen(buf));
}

/* scale from 4K pages to K or M */
#define scale(x) x > 255 ? x >> 8 : x << 2 
#define unit(x) x > 255 ? "M" : "K"

static void
memory(int start)
{
	struct vmtotal vmt;
	struct bcachestats bcs;
	size_t size;
	int mib[3];
	char buf[64];
	XGlyphInfo extents;

	mib[0] = CTL_VM;
	mib[1] = VM_METER;
	size = sizeof(vmt);
	if (sysctl(mib, 2, &vmt, &size, NULL, 0) < 0)
		return;

	mib[0] = CTL_VFS;
	mib[1] = VFS_GENERIC;
	mib[2] = VFS_BCACHESTAT;
	size = sizeof(bcs);
	if (sysctl(mib, 3, &bcs, &size, NULL, 0) < 0)
		return;

	sprintf(buf, "Mem: %d%s/%d%s Free: %d%s Cache: %lld%s",
	    scale(vmt.t_arm), unit(vmt.t_arm), scale(vmt.t_rm), unit(vmt.t_rm),
	    scale(vmt.t_free), unit(vmt.t_free),
	    scale(bcs.numbufpages), unit(bcs.numbufpages));

	XftTextExtentsUtf8(d, xftfont, (const FcChar8 *)buf, strlen(buf), &extents);
	XftDrawRect(xftd, &black, start, 0, extents.xOff, BARHEIGHT);
	XftDrawStringUtf8(xftd, &white, xftfont, start, 1 + xftfont->ascent, (const FcChar8 *)buf, strlen(buf));
}

static void
procs(int start)
{
	unsigned int nprocs;
	size_t size;
	int mib[2];
	char buf[32];
	XGlyphInfo extents;

	mib[0] = CTL_KERN;
	mib[1] = KERN_NPROCS;
	size = sizeof(nprocs);
	if (sysctl(mib, 2, &nprocs, &size, NULL, 0) < 0)
		return;

	sprintf(buf, "Procs: %d", nprocs);

	XftTextExtentsUtf8(d, xftfont, (const FcChar8 *)buf, strlen(buf), &extents);
	XftDrawRect(xftd, &black, start, 0, extents.xOff, BARHEIGHT);
	XftDrawStringUtf8(xftd, &white, xftfont, start, 1 + xftfont->ascent, (const FcChar8 *)buf, strlen(buf));
}

static void
showfile(int start)
{
	FILE *f;
	char *p;
	size_t len, pos = 0;
	char buf[256];
	XGlyphInfo extents;

	f = fopen("/tmp/status.txt", "r");
	p = fgetln(f, &len);
	while(pos < 256 && len-- && *p != '\n')
		buf[pos++] = *p++;
	buf[pos] = '\0';
	fclose(f);

	XftTextExtentsUtf8(d, xftfont, (const FcChar8 *)buf, strlen(buf), &extents);
	XftDrawRect(xftd, &black, start, 0, extents.xOff, BARHEIGHT);
	XftDrawStringUtf8(xftd, &white, xftfont, start, 1 + xftfont->ascent, (const FcChar8 *)buf, strlen(buf));
}

static void
datetime(void)
{
	char buf[40];
	XGlyphInfo extents;

	tick = time(NULL);
	if (tick % 2)
		strftime(buf, sizeof(buf), "%A %d.%m.%Y %H:%M", localtime(&tick));
	else
		strftime(buf, sizeof(buf), "%A %d.%m.%Y %H.%M", localtime(&tick));

	XftTextExtentsUtf8(d, xftfont, (const FcChar8 *)buf, strlen(buf), &extents);
	/*XFillRectangle(d, w, DefaultGC(d, s), BARWIDTH - 5 - extents.xOff, 0, BARWIDTH, BARHEIGHT);*/
	XftDrawRect(xftd, &black, BARWIDTH - 5 - extents.xOff, 0, extents.xOff, BARHEIGHT);
	XftDrawStringUtf8(xftd, &white, xftfont, BARWIDTH - 5 - extents.xOff, 1 + xftfont->ascent, (const FcChar8 *)buf, strlen(buf));
}

static void
redraw(void)
{
	desktops(5);
	memory(90);
	loadaverage(400);
	procs(550);
	showfile(700);
	datetime();
}
 
int
main(int argc, char *argv[])
{
	XEvent e;
	Atom type;
	XClassHint *h;
	XSetWindowAttributes wa;
	unsigned int desktop;
	struct pollfd pfd[1];
	int nfds;
	char *fontstr = FONT;
	int running = 1;

	d = XOpenDisplay(NULL);
	if (d == NULL) {
		fprintf(stderr, "Cannot open display\n");
		exit(1);
	}

	s = DefaultScreen(d);

	wa.override_redirect = 1;
	wa.background_pixmap = ParentRelative;
	wa.event_mask = ExposureMask | ButtonReleaseMask | ButtonPressMask;

	w = XCreateWindow(d, RootWindow(d, s), 0, BARPOS, BARWIDTH, BARHEIGHT, 0,
	    DefaultDepth(d, s), CopyFromParent, DefaultVisual(d, s),
	    CWOverrideRedirect | CWBackPixmap | CWEventMask, &wa);

	h = XAllocClassHint();
	h->res_name  = "status";
	h->res_class = "status";
	XSetClassHint(d, w, h);
	XFree(h);

	XStoreName(d, w, "status");

	type = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DOCK", False);
	XChangeProperty(d, w, XInternAtom(d, "_NET_WM_WINDOW_TYPE", False), XInternAtom(d, "ATOM", False), 32, PropModeReplace, (unsigned char *)&type, 1);

	type = XInternAtom(d, "_NET_WM_STATE_ABOVE", False);
	XChangeProperty(d, w, XInternAtom(d, "_NET_WM_STATE", False), XInternAtom(d, "ATOM", False), 32, PropModeReplace, (unsigned char *)&type, 1);

	type = XInternAtom(d, "_NET_WM_STATE_STICKY", False);
	XChangeProperty(d, w, XInternAtom(d, "_NET_WM_STATE", False), XInternAtom(d, "ATOM", False), 32, PropModeAppend, (unsigned char *)&type, 1); 

	desktop = 0xffffffff;
	XChangeProperty(d, w, XInternAtom(d, "_NET_WM_DESKTOP", False), XInternAtom(d, "CARDINAL", False), 32, PropModeReplace, (unsigned char *)&desktop, 1);

	xftd = XftDrawCreate(d, w, DefaultVisual(d, s), DefaultColormap(d, s));

	XftColorAllocName(d, DefaultVisual(d, s), DefaultColormap(d, s),  "white",  &white);
	XftColorAllocName(d, DefaultVisual(d, s), DefaultColormap(d, s),  "black",  &black);

	xftfont = XftFontOpenXlfd(d, s, fontstr);
	if (!xftfont)
		xftfont = XftFontOpenName(d, s, fontstr);
	if (!xftfont)
		exit(1);

	XSelectInput(d, w, ExposureMask | ButtonPressMask);
	XSelectInput(d, RootWindow(d, s), PropertyChangeMask);

	XMapWindow(d, w);
	XFlush(d);

	pfd[0].fd = ConnectionNumber(d);
	pfd[0].events = POLLIN;

	while (running) {
		nfds = poll(pfd, 1, 1000);
		if (nfds == -1 || (pfd[0].revents & (POLLERR|POLLHUP|POLLNVAL)))
			break;
		if (nfds == 0) {
			redraw();
			XFlush(d);
			continue;
		}

		while (XPending(d)) {
			XNextEvent(d, &e);
			if (e.type == PropertyNotify && e.xproperty.window == RootWindow(d, s) && e.xproperty.atom == XInternAtom(d, "_NET_CURRENT_DESKTOP", True)) {
				redraw();
			}
			if (e.type == Expose) {
				XftDrawRect(xftd, &black, 0, 0, BARWIDTH, BARHEIGHT);
				redraw();
			}
			if (e.type == ButtonPress) {
				/*running = 0;
				break;*/
				redraw();
			}
		}
	}

	XftColorFree(d, DefaultVisual(d, s), DefaultColormap(d, s), &white);
	XftColorFree(d, DefaultVisual(d, s), DefaultColormap(d, s), &black);
	XftFontClose(d, xftfont);
	XftDrawDestroy(xftd);
	XDestroyWindow(d, w);
	XCloseDisplay(d);
	return 0;
}
