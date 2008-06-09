# common variables ...
########################################################################

# directories
DESTDIR	=
srcdir	?= .
prefix	?= /usr/local
bindir	=  $(DESTDIR)$(prefix)/bin
sbindir	=  $(DESTDIR)$(prefix)/sbin
libdir  =  $(DESTDIR)$(prefix)/$(LIB)
shrdir  =  $(DESTDIR)$(prefix)/share
mandir	=  $(shrdir)/man
locdir  =  $(shrdir)/locale
appdir  =  $(shrdir)/applications

# package + version
empty	:=
space	:= $(empty) $(empty)
ifneq ($(wildcard $(srcdir)/VERSION),)
  VERSION := $(shell cat $(srcdir)/VERSION)
else
  VERSION := 42
endif
RELTAG	:= v$(subst .,_,$(VERSION))

# programs
CC		?= gcc
CXX		?= g++
MOC             ?= $(if $(QTDIR),$(QTDIR)/bin/moc,moc)

STRIP		?= -s
INSTALL		?= install
INSTALL_BINARY  := $(INSTALL) $(STRIP)
INSTALL_SCRIPT  := $(INSTALL)
INSTALL_DATA	:= $(INSTALL) -m 644
INSTALL_DIR	:= $(INSTALL) -d

# cflags
CFLAGS		?= -g -O2
CXXFLAGS	?= $(CFLAGS)
CFLAGS		+= -Wall -Wmissing-prototypes -Wstrict-prototypes \
		   -Wpointer-arith -Wunused
CXXFLAGS	+= -Wall -Wpointer-arith -Wunused

# add /usr/local to the search path if something is in there ...
ifneq ($(wildcard /usr/local/include/*.h),)
  CFLAGS  += -I/usr/local/include
  LDFLAGS += -L/usr/local/$(LIB)
endif

# fixup include path for $(srcdir) != "."
ifneq ($(srcdir),.)
  CFLAGS  += -I. -I$(srcdir)
endif

