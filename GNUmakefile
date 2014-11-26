# config
srcdir	= .
VPATH	= $(srcdir)
-include Make.config
include $(srcdir)/mk/Variables.mk

resdir	=  $(DESTDIR)$(RESDIR)

# fixup flags
CFLAGS	+= -DVERSION='"$(VERSION)"' -I$(srcdir)
CFLAGS	+= -Wno-pointer-sign

# default target
all: build

# what to build
TARGETS := exiftran thumbnail.cgi
ifeq ($(HAVE_LINUX_FB_H),yes)
  TARGETS += fbi
endif
ifeq ($(HAVE_MOTIF),yes)
  TARGETS += ida
endif


#################################################################
# poor man's autoconf ;-)

include $(srcdir)/mk/Autoconf.mk

ac_jpeg_ver = $(shell \
	$(call ac_init,for libjpeg version);\
	$(call ac_s_cmd,echo JPEG_LIB_VERSION \
		| cpp -include jpeglib.h | tail -n 1);\
	$(call ac_fini))

define make-config
LIB		:= $(LIB)
RESDIR		:= $(call ac_resdir)
HAVE_ENDIAN_H	:= $(call ac_header,endian.h)
HAVE_LINUX_FB_H	:= $(call ac_header,linux/fb.h)
HAVE_NEW_EXIF	:= $(call ac_header,libexif/exif-log.h)
HAVE_GLIBC	:= $(call ac_func,fopencookie)
HAVE_STRSIGNAL	:= $(call ac_func,strsignal)
HAVE_LIBPCD	:= $(call ac_lib,pcd_open,pcd)
HAVE_LIBGIF	:= $(call ac_lib,DGifOpenFileName,gif)
HAVE_LIBPNG	:= $(call ac_lib,png_read_info,png,-lz)
HAVE_LIBTIFF	:= $(call ac_lib,TIFFOpen,tiff)
HAVE_LIBWEBP	:= $(call ac_lib,WebPDecodeRGBA,webp)
#HAVE_LIBMAGICK	:= $(call ac_binary,Magick-config)
HAVE_LIBSANE	:= $(call ac_lib,sane_init,sane)
HAVE_LIBCURL	:= $(call ac_lib,curl_easy_init,curl)
HAVE_LIBLIRC	:= $(call ac_lib,lirc_init,lirc_client)
HAVE_MOTIF	:= $(call ac_lib,XmStringGenerate,Xm,-L/usr/X11R6/$(LIB) -lXpm -lXt -lXext -lX11)
JPEG_VER        := $(call ac_jpeg_ver)
endef

# transposing
CFLAGS  += -Ijpeg/$(JPEG_VER)

# transparent http/ftp access using curl depends on fopencookie (glibc)
ifneq ($(HAVE_GLIBC),yes)
  HAVE_LIBCURL	:= no
endif

# catch fopen calls for transparent ftp/http access
ifeq ($(HAVE_LIBCURL),yes)
  ida fbi : CFLAGS   += -D_GNU_SOURCE
  ida fbi : LDFLAGS  += -Wl,--wrap=fopen
endif

PKG_CONFIG = pkg-config

########################################################################
# conditional stuff

includes        = ENDIAN_H STRSIGNAL NEW_EXIF
libraries       = PCD GIF PNG TIFF WEBP CURL SANE LIRC
ida_libs	= PCD GIF PNG TIFF WEBP CURL SANE
fbi_libs	= PCD GIF PNG TIFF WEBP CURL LIRC

#MAGICK_CFLAGS	= $(shell Magick-config --cflags)
#MAGICK_LDFLAGS	= $(shell Magick-config --ldflags)
#MAGICK_LDLIBS	= $(shell Magick-config --libs)
#MAGICK_OBJS	:= rd/magick.o

PNG_LDLIBS	:= -lpng -lz
TIFF_LDLIBS	:= -ltiff
WEBP_LDLIBS	:= -lwebp
PCD_LDLIBS	:= -lpcd
GIF_LDLIBS	:= -lgif
SANE_LDLIBS	:= -lsane
CURL_LDLIBS	:= -lcurl
LIRC_LDLIBS     := -llirc_client

PNG_OBJS	:= rd/read-png.o  wr/write-png.o
TIFF_OBJS	:= rd/read-tiff.o wr/write-tiff.o
WEBP_OBJS	:= rd/read-webp.o
PCD_OBJS	:= rd/read-pcd.o
GIF_OBJS	:= rd/read-gif.o
SANE_OBJS	:= sane.o
CURL_OBJS	:= curl.o
LIRC_OBJS       := lirc.o

# common objs
OBJS_READER	:= readers.o rd/read-ppm.o rd/read-bmp.o rd/read-jpeg.o
OBJS_WRITER	:= writers.o wr/write-ppm.o wr/write-ps.o wr/write-jpeg.o

# update various flags depending on HAVE_*
CFLAGS		+= $(call ac_inc_cflags,$(includes))
CFLAGS		+= $(call ac_lib_cflags,$(libraries))
CFLAGS		+= $(call ac_lib_mkvar,$(libraries),CFLAGS)
LDFLAGS		+= $(call ac_lib_mkvar,$(libraries),LDFLAGS)

# link which conditional libs
ida : LDLIBS += $(call ac_lib_mkvar,$(ida_libs),LDLIBS)
fbi : LDLIBS += $(call ac_lib_mkvar,$(fbi_libs),LDLIBS)


########################################################################
# rules for the small tools

# jpeg/exif libs
exiftran      : LDLIBS += -ljpeg -lexif -lm
thumbnail.cgi : LDLIBS += -lexif -lm

exiftran: exiftran.o genthumbnail.o jpegtools.o \
	jpeg/$(JPEG_VER)/transupp.o \
	filter.o op.o readers.o rd/read-jpeg.o
thumbnail.cgi: thumbnail.cgi.o


########################################################################
# rules for ida

# object files
OBJS_IDA := \
	ida.o man.o hex.o x11.o viewer.o dither.o icons.o \
	parseconfig.o idaconfig.o fileops.o desktop.o \
	RegEdit.o selections.o xdnd.o jpeg/$(JPEG_VER)/transupp.o \
	filebutton.o filelist.o browser.o jpegtools.o \
	op.o filter.o lut.o color.o \
	rd/read-xwd.o rd/read-xpm.o 

OBJS_IDA += $(call ac_lib_mkvar,$(ida_libs),OBJS)

# for X11 + Motif
ida : CFLAGS	+= -I/usr/X11R6/include
ida : LDFLAGS	+= -L/usr/X11R6/$(LIB)
ida : LDLIBS	+= -lXm -lXpm -lXt -lXext -lX11

# jpeg/exif libs
ida : LDLIBS	+= -ljpeg -lexif -lm

# RegEdit.c is good old K&R ...
RegEdit.o : CFLAGS += -Wno-missing-prototypes -Wno-strict-prototypes -Wno-maybe-uninitialized

ida: $(OBJS_IDA) $(OBJS_READER) $(OBJS_WRITER)

Ida.ad.h: Ida.ad $(srcdir)/fallback.pl
	perl $(srcdir)/fallback.pl < $< > $@

logo.h: logo.jpg
	hexdump -v -e '1/1 "0x%02x,"' < $< > $@
	echo >> $@ # make gcc 3.x happy

ida.o: Ida.ad.h logo.h


########################################################################
# rules for fbi

# object files
OBJS_FBI := \
	fbi.o fbtools.o fb-gui.o desktop.o \
	parseconfig.o fbiconfig.o \
	jpegtools.o jpeg/$(JPEG_VER)/transupp.o \
	dither.o filter.o op.o

OBJS_FBI += $(filter-out wr/%,$(call ac_lib_mkvar,$(fbi_libs),OBJS))

# jpeg/exif libs
fbi : CFLAGS += $(shell $(PKG_CONFIG) --cflags freetype2 fontconfig)
fbi : LDLIBS += $(shell $(PKG_CONFIG) --libs   freetype2 fontconfig)
fbi : LDLIBS += -ljpeg -lexif -lm

fbi: $(OBJS_FBI) $(OBJS_READER)


########################################################################
# general rules

.PHONY: check-libjpeg build install clean distclean realclean
build: check-libjpeg $(TARGETS)

check-libjpeg:
	@test -d jpeg/$(JPEG_VER) || \
		( echo "Need files from libjpeg $(JPEG_VER) in jpeg/"; false)

install: build
	$(INSTALL_DIR) $(bindir)
	$(INSTALL_DIR) $(mandir)/man1
	$(INSTALL_BINARY) exiftran $(bindir)
	$(INSTALL_DATA) $(srcdir)/exiftran.man $(mandir)/man1/exiftran.1
ifeq ($(HAVE_LINUX_FB_H),yes)
	$(INSTALL_BINARY) fbi $(bindir)
	$(INSTALL_SCRIPT) fbgs $(bindir)
	$(INSTALL_DATA) $(srcdir)/fbi.man $(mandir)/man1/fbi.1
	$(INSTALL_DATA) $(srcdir)/fbgs.man $(mandir)/man1/fbgs.1
endif
ifeq ($(HAVE_MOTIF),yes)
	$(INSTALL_BINARY) ida $(bindir)
	$(INSTALL_DATA) $(srcdir)/ida.man $(mandir)/man1/ida.1
	$(INSTALL_DIR) $(resdir)/app-defaults
	$(INSTALL_DATA) $(srcdir)/Ida.ad $(resdir)/app-defaults/Ida
endif

clean:
	-rm -f *.o jpeg/$(JPEG_VER)/*.o rd/*.o wr/*.o $(depfiles) core core.*

realclean distclean: clean
	-rm -f Make.config
	-rm -f $(TARGETS) *~ rd/*~ wr/*~ xpm/*~ Ida.ad.h logo.h


include $(srcdir)/mk/Compile.mk
-include $(depfiles)


########################################################################
# maintainer stuff

include $(srcdir)/mk/Maintainer.mk

#sync::
#	cp $(srcdir)/../xawtv/common/parseconfig.[ch] $(srcdir)
#	cp $(srcdir)/../xawtv/console/fbtools.[ch] $(srcdir)
