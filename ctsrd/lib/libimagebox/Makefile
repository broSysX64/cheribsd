#
# $FreeBSD$
#

LIB=		imagebox
SHLIB_MAJOR=	1

SRCS=	decode_png.c pngbox.c

INCS=	imagebox.h

# we need the png.h header but don't need to link against it
CFLAGS+=        -I${SRCTOP}/contrib/libpng
CFLAGS+=        -I${SRCTOP}/lib/libpng

WANT_CHERI=	yes

.include <bsd.lib.mk>
