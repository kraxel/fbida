Name:         fbida
License:      GPLv2+
Version:      2.14
Release:      1%{?dist}
Summary:      fbida
URL:          http://www.kraxel.org/blog/linux/%{name}/
Source:       http://www.kraxel.org/releases/%{name}/%{name}-%{version}.tar.gz

# meson
BuildRequires: gcc perl
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
meson --prefix=%{_prefix} build-rpm
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

%files -n fbcon
%doc COPYING
%{_bindir}/fbcon
/usr/share/wayland-sessions/fbcon.desktop

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

