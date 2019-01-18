# config
srcdir	= .
VPATH	= $(srcdir)
-include Make.config
include $(srcdir)/mk/Variables.mk

resdir	=  $(DESTDIR)$(RESDIR)

# fixup flags
CFLAGS	+= -DVERSION='"$(VERSION)"' -I$(srcdir)
CFLAGS	+= -Wno-pointer-sign

# hard build deps
PKG_CONFIG = pkg-config
PKGS_IDA := libexif libpng libtiff-4 pixman-1
PKGS_FBI := freetype2 fontconfig libdrm libexif libpng libtiff-4 pixman-1
PKGS_FBPDF := libdrm poppler-glib gbm egl epoxy pixman-1
HAVE_DEPS := $(shell $(PKG_CONFIG) $(PKGS_FBI) $(PKGS_FBPDF) && echo yes)

# map pkg-config names to debian packages using apt-file
APT_REGEX = /($(shell echo $(PKGS_FBI) $(PKGS_FBPDF) | sed -e 's/ /|/g')).pc
APT_DEBS  = $(shell apt-file search --package-only --regex "$(APT_REGEX)")

ifeq ($(HAVE_LINUX_FB_H),yes)
ifneq ($(HAVE_DEPS),yes)
.PHONY: deps
deps:
	@echo "Build dependencies missing for fbi and/or fbpdf."
	@echo "  fbi   needs:  $(PKGS_FBI)"
	@echo "  fbpdf needs:  $(PKGS_FBPDF)"
	@echo "Please install.  Try 'make yum', 'make dnf' or 'make apt-get' (needs sudo)."
	@false

yum dnf:
	sudo $@ install $(patsubst %,"pkgconfig(%)",$(PKGS_FBI) $(PKGS_FBPDF))

apt-get:
	sudo apt-get install $(APT_DEBS)

endif
endif

# default target
all: build

# what to build
TARGETS := exiftran thumbnail.cgi
ifeq ($(HAVE_LINUX_FB_H),yes)
  TARGETS += fbi fbpdf kbdtest
endif
ifeq ($(HAVE_MOTIF),yes)
  TARGETS += ida
endif


#################################################################
# poor man's autoconf ;-)

include $(srcdir)/mk/Autoconf.mk

ac_jpeg_ver = $(shell \
	$(call ac_init,for libjpeg version);\
	$(call ac_s_cmd, $(srcdir)/scripts/jpeg-version.sh);\
	$(call ac_fini))

define make-config
LIB		:= $(LIB)
RESDIR		:= $(call ac_resdir)
HAVE_LINUX_FB_H	:= $(call ac_header,linux/fb.h)
HAVE_LIBPCD	:= $(call ac_lib,pcd_open,pcd)
HAVE_LIBGIF	:= $(call ac_lib,DGifOpenFileName,gif)
HAVE_LIBWEBP	:= $(call ac_pkg_config,libwebp)
HAVE_MOTIF	:= $(call ac_lib,XmStringGenerate,Xm,-L/usr/X11R6/$(LIB) -lXpm -lXt -lXext -lX11)
JPEG_VER        := $(call ac_jpeg_ver)
endef

# transposing
CFLAGS  += -Ijpeg/$(JPEG_VER)

########################################################################
# conditional stuff

ifeq ($(HAVE_LIBWEBP),yes)
  PKGS_IDA += libwebp
  PKGS_FBI += libwebp
endif

libraries       = PCD GIF
ida_libs	= PCD GIF WEBP
fbi_libs	= PCD GIF WEBP

PCD_LDLIBS	:= -lpcd
GIF_LDLIBS	:= -lgif

WEBP_OBJS	:= rd/read-webp.o
PCD_OBJS	:= rd/read-pcd.o
GIF_OBJS	:= rd/read-gif.o

# common objs
OBJS_READER	:= readers.o rd/read-ppm.o rd/read-bmp.o rd/read-jpeg.o \
                   rd/read-png.o rd/read-tiff.o
OBJS_WRITER	:= writers.o wr/write-ppm.o wr/write-ps.o wr/write-jpeg.o \
                   wr/write-png.o wr/write-tiff.o

# update various flags depending on HAVE_*
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
exiftran      : CFLAGS += $(shell $(PKG_CONFIG) --cflags pixman-1)
exiftran      : LDLIBS += $(shell $(PKG_CONFIG) --libs   pixman-1)
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
ida : CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS_IDA))
ida : LDLIBS += $(shell $(PKG_CONFIG) --libs   $(PKGS_IDA))
ida : LDLIBS += -ljpeg -lm

# RegEdit.c is good old K&R ...
RegEdit.o : CFLAGS += -Wno-missing-prototypes -Wno-strict-prototypes -Wno-maybe-uninitialized

ida: $(OBJS_IDA) $(OBJS_READER) $(OBJS_WRITER)

Ida.ad.h: Ida.ad $(srcdir)/scripts/fallback.pl
	perl $(srcdir)/scripts/fallback.pl $< $@

logo.h: logo.jpg
	scripts/hexify.sh $< $@

ida.o: Ida.ad.h logo.h


########################################################################
# rules for fbi

# object files
OBJS_FBI := \
	fbi.o vt.o kbd.o fbtools.o drmtools.o fb-gui.o desktop.o \
	parseconfig.o fbiconfig.o \
	jpegtools.o jpeg/$(JPEG_VER)/transupp.o \
	dither.o filter.o op.o
OBJS_FBI += $(filter-out wr/%,$(call ac_lib_mkvar,$(fbi_libs),OBJS))

# font + drm + jpeg/exif libs
fbi : CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS_FBI))
fbi : LDLIBS += $(shell $(PKG_CONFIG) --libs   $(PKGS_FBI))
fbi : LDLIBS += -ljpeg -lm

fbi: $(OBJS_FBI) $(OBJS_READER)


########################################################################
# rules for fbpdf

# object files
OBJS_FBPDF := \
	fbpdf.o vt.o kbd.o fbtools.o drmtools.o drmtools-egl.o \
	fbiconfig.o parseconfig.o

# font + drm + jpeg/exif libs
fbpdf : CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS_FBPDF))
fbpdf : LDLIBS += $(shell $(PKG_CONFIG) --libs   $(PKGS_FBPDF))

fbpdf: $(OBJS_FBPDF)


########################################################################
# rules for kbdtest

kbdtest : kbdtest.o kbd.o

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
	$(INSTALL_DATA) $(srcdir)/man/exiftran.1 $(mandir)/man1
ifeq ($(HAVE_LINUX_FB_H),yes)
	$(INSTALL_BINARY) fbi $(bindir)
	$(INSTALL_SCRIPT) fbgs $(bindir)
	$(INSTALL_SCRIPT) fbpdf $(bindir)
	$(INSTALL_DATA) $(srcdir)/man/fbi.1 $(mandir)/man1
	$(INSTALL_DATA) $(srcdir)/man/fbgs.1 $(mandir)/man1
endif
ifeq ($(HAVE_MOTIF),yes)
	$(INSTALL_BINARY) ida $(bindir)
	$(INSTALL_DATA) $(srcdir)/man/ida.1 $(mandir)/man1
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
