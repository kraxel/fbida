Name:         fbida
License:      GPLv2+
Version:      2.15
Release:      1%{?dist}
Summary:      fbida
URL:          http://www.kraxel.org/blog/linux/%{name}/
Source:       http://www.kraxel.org/releases/%{name}/%{name}-%{version}.tar.gz

# meson
BuildRequires: gcc perl util-linux
BuildRequires: meson ninja-build

# image format libs
BuildRequires: libjpeg-devel
BuildRequires: pkgconfig(libexif)
BuildRequires: pkgconfig(libpng)
BuildRequires: pkgconfig(libtiff-4)
BuildRequires: pkgconfig(libwebp)
BuildRequires: pkgconfig(pixman-1)

# ida
BuildRequires: motif-devel
BuildRequires: libXpm-devel
BuildRequires: libXt-devel
BuildRequires: libX11-devel

# fbi + fbpdf
BuildRequires: pkgconfig(freetype2)
BuildRequires: pkgconfig(fontconfig)
BuildRequires: pkgconfig(libdrm)
BuildRequires: pkgconfig(poppler-glib)
BuildRequires: pkgconfig(cairo)
BuildRequires: pkgconfig(libudev)
BuildRequires: pkgconfig(libinput)
BuildRequires: pkgconfig(xkbcommon)

%description
fbida

%package -n fbi
Summary: Framebuffer image viewer
%description -n fbi
fbi displays images on the linux console using fbdev or drm.

%package -n fbpdf
Summary: Framebuffer pdf viewer
%description -n fbpdf
fbpdf displays pdf files on the linux console using fbdev or drm.

%package -n ida
Summary: Motif image viewer
%description -n ida
ida is a motif-based image viewer for X11.

%package -n exiftran
Summary: exiftran
%description -n exiftran
Exiftran is a command line utility to transform digital camera jpeg
images.  It can do lossless rotations like jpegtran, but unlike
jpegtran it also handles the exif metadata.

%prep
%setup -q

%build
export CFLAGS="%{optflags}"
meson setup --prefix=%{_prefix} build-rpm
meson configure build-rpm -Dmotif=enabled
meson configure build-rpm
ninja-build -C build-rpm

%install
export DESTDIR=%{buildroot}
ninja-build -C build-rpm install
# add fbgs
cp fbgs %{buildroot}%{_bindir}
cp man/fbgs.1 %{buildroot}%{_mandir}/man1

%files -n fbi
%doc COPYING README.md
%{_bindir}/fbi
%{_bindir}/fbgs
%{_mandir}/man1/fbi.1*
%{_mandir}/man1/fbgs.1*

%files -n fbpdf
%doc COPYING README.md
%{_bindir}/fbpdf

%files -n ida
%doc COPYING README.ida
%{_bindir}/ida
%{_mandir}/man1/ida.1*
/usr/share/X11/app-defaults/Ida

%files -n exiftran
%doc COPYING README.md
%{_bindir}/exiftran
%{_mandir}/man1/exiftran.1*

%changelog
* Thu Jan 22 2026 Gerd Hoffmann <kraxel@redhat.com> 2.15-1
- ci: add tito rpm test build (kraxel@redhat.com)
- spec: add util-linux to build deps (kraxel@redhat.com)
- ci: enable motif (kraxel@redhat.com)
- ci: call meson + ninja directly (kraxel@redhat.com)
- ci: install hostname with -y (kraxel@redhat.com)
- ci: install hostname (kraxel@redhat.com)
- ci: do a simple build test (kraxel@redhat.com)
- drop travis config (kraxel@redhat.com)
- drop status links from readme (kraxel@redhat.com)
- Fix API libtsm-4.0.0 (nicolas.parlant@parhuet.fr)
- fix build gcc-15/c23 (nicolas.parlant@parhuet.fr)
- spec: print meson configuratiom (kraxel@redhat.com)
- enable motif (kraxel@redhat.com)
- also drop fbcon filelist (kraxel@redhat.com)
- drop fbcon sub-rpm (kraxel@redhat.com)
- turn on rpm builds (kraxel@redhat.com)
- flip some options in auto mode (kraxel@redhat.com)
- drop libpcd build dep (kraxel@redhat.com)
- drop fbcon build deps (kraxel@redhat.com)
- meson.build: install fbgs shell script (Wolfgang.Meyer@gossenmetrawatt.com)
- meson.build: make fbpdf build optional (Wolfgang.Meyer@gossenmetrawatt.com)
- meson.build: add features options for png, gif, tiff, webp, and motif
  (Wolfgang.Meyer@gossenmetrawatt.com)
- fbida: Include missing <sys/types.h> (raj.khem@gmail.com)
- gitlab ci test build (kraxel@redhat.com)
- Drop gbm / epoxy / egl dependencies (caramelli.devel@gmail.com)
- Revert "add gitlab ci rpmbuild" (kraxel@redhat.com)
- add gitlab ci rpmbuild (kraxel@redhat.com)
- update travis badge for gitlab (kraxel@redhat.com)
- travis: use bionic (kraxel@redhat.com)
- gcc10 build fix (kraxel@redhat.com)
- add readme files to %%doc (kraxel@redhat.com)
- some rwadme tweaks (kraxel@redhat.com)
- try fix link (kraxel@redhat.com)
- add readme.mb, rename old readme (kraxel@redhat.com)
- travis: add mesa devel pkgs (kraxel@redhat.com)
- meson: fix systemd version (kraxel@redhat.com)
- travis: use newer meson from pip (kraxel@redhat.com)
- travis: drop leftover lines (kraxel@redhat.com)
- add .travis.yml (kraxel@redhat.com)
- sync connector names (kraxel@redhat.com)
- add byteorder.h (kraxel@redhat.com)
- portability fixes, some modernization (kraxel@redhat.com)
- resume display tweak (kraxel@redhat.com)
- drop dup error message (kraxel@redhat.com)
- add device_open() (kraxel@redhat.com)
- logind tweaks (kraxel@redhat.com)
- use xkbcommon (kraxel@redhat.com)
- xkb: add xkb_init() to kbd.c (kraxel@redhat.com)
- xkb: move xkb_configure to kbd.c (kraxel@redhat.com)
- xkb: move variables to kbd.c (kraxel@redhat.com)
- xkb: prefix variables (kraxel@redhat.com)
- drop some dead code (kraxel@redhat.com)
- debug logging (kraxel@redhat.com)
- even more logind console switching (kraxel@redhat.com)
- more logind console switching (kraxel@redhat.com)
- [wip still] more logind console switching (kraxel@redhat.com)
- drop dead code (kraxel@redhat.com)
- [wip] logind console switching (kraxel@redhat.com)
- move config files to ~/.config/ (kraxel@redhat.com)
- run login shell (kraxel@redhat.com)
- exec tweaks (kraxel@redhat.com)
- some logind vt switch bits (kraxel@redhat.com)
- list management fixes (kraxel@redhat.com)
- add dbus checks (kraxel@redhat.com)
- logind improvements (kraxel@redhat.com)
- move logind code to separate file (kraxel@redhat.com)
- fbcon: sd_bus_message_read fix #3 (kraxel@redhat.com)
- fbcon: sd_bus_message_read fix #2 (kraxel@redhat.com)
- fbcon: sd_bus_message_read fix (kraxel@redhat.com)
- even more logind logging (kraxel@redhat.com)
- more ligind logging (kraxel@redhat.com)
- fbcon: logind debug messages (kraxel@redhat.com)
- implement logind_close (kraxel@redhat.com)
- logind support (kraxel@redhat.com)
- fbcon: add config file (kraxel@redhat.com)
- clear sb on resize (kraxel@redhat.com)
- scrollback (kraxel@redhat.com)
- fbcon: cleanups, resize (kraxel@redhat.com)
- color tweak, inverse support (kraxel@redhat.com)
- some optimizations (kraxel@redhat.com)
- fixes (kraxel@redhat.com)
- update specfile (kraxel@redhat.com)
- use libtsm kbd support (kraxel@redhat.com)
- fbcon: switch to libtsm (kraxel@redhat.com)
- drop dead code (kraxel@redhat.com)
- blit using pixman, drop dirty line tracking (kraxel@redhat.com)
- fix cairo shadow clear (kraxel@redhat.com)
- tweak error logging (kraxel@redhat.com)
- drm format wireup (kraxel@redhat.com)
- fmt wireup, framebuffer (kraxel@redhat.com)
- format fixups (kraxel@redhat.com)
- add gfxfmt, fixes (kraxel@redhat.com)
- drop drmtools-egl.c (kraxel@redhat.com)
- use pixman for image blit+blend (kraxel@redhat.com)
- libinput check fix (kraxel@redhat.com)
- libinput tweaks (kraxel@redhat.com)
- dirty fix, parse font name (kraxel@redhat.com)
- fix pause (kraxel@redhat.com)
- use cairo for text rendering (kraxel@redhat.com)
- fix darkify (kraxel@redhat.com)
- drop more line editing support (kraxel@redhat.com)
- drop line editing support (kraxel@redhat.com)
- use cairo for darkify (kraxel@redhat.com)
- fix cairo, use cairo for lines (kraxel@redhat.com)
- init cairo context for shadow (kraxel@redhat.com)
- fix blend (kraxel@redhat.com)
- drop legacy build system (kraxel@redhat.com)
- drop dither support, switch shadow fb to DRM_FORMAT_XRGB8888 (aka
  CAIRO_FORMAT_RGB24) (kraxel@redhat.com)
- use cloexec (kraxel@redhat.com)
- fbdev support, console switching (kraxel@redhat.com)
- add fbcon session (kraxel@redhat.com)
- tweak console switching (kraxel@redhat.com)
- drop -vt option (kraxel@redhat.com)
- read keymap from /etc/vconsole.conf (kraxel@redhat.com)
- move ansi keys (kraxel@redhat.com)
- color tweaks (kraxel@redhat.com)
- terminal reply (kraxel@redhat.com)
- udev device enumeration (kraxel@redhat.com)
- fill winsize (kraxel@redhat.com)
- more keys (kraxel@redhat.com)
- update spec (kraxel@redhat.com)
- add static (kraxel@redhat.com)
- cursor key support (kraxel@redhat.com)
- cache contexts, init font early, autosize terminal (kraxel@redhat.com)
- add color and cursor rendering (kraxel@redhat.com)
- add simple, experimental terminal emulator (kraxel@redhat.com)
- specfile: add libinput (kraxel@redhat.com)
- Revert "libinput: grab tweaks" (kraxel@redhat.com)
- libinput: handle console switches (kraxel@redhat.com)
- libinput: grab tweaks (kraxel@redhat.com)
- libinput: mouse button support (kraxel@redhat.com)
- kbd: experimental libinput support (kraxel@redhat.com)
- abstract away some kbd details (kraxel@redhat.com)
- drop forgotten cairo-gl bits (kraxel@redhat.com)
- drop sane support (kraxel@redhat.com)
- drop curl support (kraxel@redhat.com)
- drop lirc support (kraxel@redhat.com)
- reload filelist when modified (kraxel@redhat.com)
- add perl to build deps (kraxel@redhat.com)
- drop opengl (via cairo-gl) support (kraxel@redhat.com)
- drop cairo-gl dep from meson.build (kraxel@redhat.com)
- add gcc to specfile (kraxel@redhat.com)
- drop cairo-gl in specfile (kraxel@redhat.com)
- mode switching for drm fixup (kraxel@redhat.com)
- mode switching for drm (kraxel@redhat.com)

* Wed Aug 30 2017 Gerd Hoffmann <kraxel@redhat.com> 2.14-1
- meson. fix app defaults install (kraxel@redhat.com)
- spec: install fbgs (kraxel@redhat.com)
- switch specfile to meson (kraxel@redhat.com)
- fix ps writer gcc7 warning (kraxel@redhat.com)
- meson: conditionally build ida (kraxel@redhat.com)
- meson: install app defaults file (kraxel@redhat.com)
- jpeg-version: add cflags to cpp cmd line (kraxel@redhat.com)
- meson: initial install support (kraxel@redhat.com)
- move & rename manpages (kraxel@redhat.com)
- deprecate curl, sane and lirc support (kraxel@redhat.com)
- meson: tag as python for emacs (kraxel@redhat.com)
- meson: add ida (kraxel@redhat.com)
- meson: minor reordering (kraxel@redhat.com)
- move blob hexifxing to script (kraxel@redhat.com)
- pass filenames as args to fallback.pl (kraxel@redhat.com)
- move fallback.pl to scripts (kraxel@redhat.com)
- meson: add thumbnail.cgi (kraxel@redhat.com)
- meson: add exiftran (kraxel@redhat.com)
- meson: fix cairo deps (kraxel@redhat.com)
- meson: add cairo-gl detection (kraxel@redhat.com)
- meson: add jpeg version detection (kraxel@redhat.com)
- move libjpeg version detect to script (kraxel@redhat.com)
- meson: add pcd + gif (kraxel@redhat.com)
- meson: add webp (kraxel@redhat.com)
- meson: add kbdtest (kraxel@redhat.com)
- meson: add fbpdf (kraxel@redhat.com)
- meson: start build file (kraxel@redhat.com)
- drop strsignal detection (kraxel@redhat.com)
- invert partial fix (kraxel@redhat.com)
- fix flip & rotate (kraxel@redhat.com)
- fix exiftran build (kraxel@redhat.com)
- update buildreq in specfile (kraxel@redhat.com)
- use pixman images for storage (kraxel@redhat.com)
- add egl pkg-config (kraxel@redhat.com)
- make cairo-gl a compile time option (kraxel@redhat.com)
- zap HAVE_LIBTIFF, tiff is a hard dependency now (kraxel@redhat.com)
- zap HAVE_LIBPNG, png is a hard dependency now (kraxel@redhat.com)
- zap HAVE_NEW_EXIF (kraxel@redhat.com)
- zap HAVE_ENDIAN_H (kraxel@redhat.com)
- use eglGetPlatformDisplayEXT if available (kraxel@redhat.com)
- add 'make apt-get' (kraxel@redhat.com)
- fix debian/ubuntu build issue (kraxel@redhat.com)
- sync maintainer makefiles (kraxel@redhat.com)
- tito: add VERSION.template (kraxel@redhat.com)

* Wed Feb 22 2017 Gerd Hoffmann <kraxel@redhat.com> 2.13-1
- new package built with tito

