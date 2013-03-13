#PROG=xcb
#SRCS=xcb.c
PROG=status
SRCS=status.c
MAN=

CFLAGS+=-g -I/usr/X11R6/include -I/usr/X11R6/include/freetype2 -Wall -Werror -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wshadow -Wpointer-arith -Wcast-qual -Wsign-compare
LDFLAGS+=-L/usr/X11R6/lib -lX11 -lXft

.include <bsd.prog.mk>
