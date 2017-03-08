Name:         fbida
License:      GPLv2+
Version:      2.13
Release:      1%{?dist}
Summary:      fbida
Group:        Applications/Multimedia
URL:          http://www.kraxel.org/blog/linux/%{name}/
Source:       http://www.kraxel.org/releases/%{name}/%{name}-%{version}.tar.gz

# image format libs
BuildRequires: libjpeg-devel
BuildRequires: libpcd-devel
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
BuildRequires: pkgconfig(gbm)
BuildRequires: pkgconfig(egl)
BuildRequires: pkgconfig(epoxy)
BuildRequires: pkgconfig(cairo-gl)

%description
fbida

%package -n fbi
Summary: Framebuffer image viewer
%description -n fbi
fbi displays images on the linux console using fbdev or drm.

%package -n fbpdf
Summary: Framebuffer pdf viewer
Group: Applications/Productivity
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
make prefix=/usr

%install
make prefix=/usr DESTDIR=%{buildroot} STRIP="" install

%files -n fbi
%doc COPYING
%{_bindir}/fbi
%{_bindir}/fbgs
%{_mandir}/man1/fbi.1*
%{_mandir}/man1/fbgs.1*

%files -n fbpdf
%doc COPYING
%{_bindir}/fbpdf

%files -n ida
%doc COPYING
%{_bindir}/ida
%{_mandir}/man1/ida.1*
/usr/share/X11/app-defaults/Ida

%files -n exiftran
%doc COPYING
%{_bindir}/exiftran
%{_mandir}/man1/exiftran.1*

%changelog
* Wed Feb 22 2017 Gerd Hoffmann <kraxel@redhat.com> 2.13-1
- new package built with tito

