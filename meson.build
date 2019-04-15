# -*-python-*-
project('fbida', 'c', default_options : [ 'c_std=gnu99' ])

# tweak warnings
add_global_arguments('-Wno-pointer-sign', language : 'c')
add_global_arguments('-D_POSIX_SOURCE=1', language : 'c')
add_global_arguments('-D_GNU_SOURCE=1', language : 'c')

# init configuration
config       = configuration_data()
version      = run_command('cat', 'VERSION')
config.set_quoted('VERSION', version.stdout().strip())
x11resrun    = run_command('scripts/x11resdir.sh', get_option('prefix'))
x11resdir    = x11resrun.stdout().strip()

# pkg-config deps
pixman_dep   = dependency('pixman-1')
poppler_dep  = dependency('poppler-glib')
cairo_dep    = dependency('cairo')
drm_dep      = dependency('libdrm')
gbm_dep      = dependency('gbm')
epoxy_dep    = dependency('epoxy')
exif_dep     = dependency('libexif')
png_dep      = dependency('libpng')
tiff_dep     = dependency('libtiff-4')
webp_dep     = dependency('libwebp', required : false)
udev_dep     = dependency('libudev')
input_dep    = dependency('libinput')
xkb_dep      = dependency('xkbcommon')
glib_dep     = dependency('glib-2.0')
tsm_dep      = dependency('libtsm', required : false)
systemd_dep  = dependency('libsystemd', required : false, version : '>=221')

# other library deps
cc           = meson.get_compiler('c')
jpeg_dep     = cc.find_library('jpeg')
util_dep     = cc.find_library('util')
math_dep     = cc.find_library('m', required : false)
pcd_dep      = cc.find_library('pcd', required : false)
gif_dep      = cc.find_library('gif', required : false)

# motif + x11 libs
motif_dep    = cc.find_library('Xm', required : false)
xpm_dep      = cc.find_library('Xpm', required : false)
xt_dep       = cc.find_library('Xt', required : false)
xext_dep     = cc.find_library('Xext', required : false)
x11_dep      = cc.find_library('X11', required : false)

# image formats
read_srcs    = [ 'readers.c', 'rd/read-ppm.c', 'rd/read-bmp.c',
                 'rd/read-jpeg.c', 'rd/read-png.c', 'rd/read-tiff.c' ]
write_srcs   = [ 'writers.c', 'wr/write-ppm.c', 'wr/write-ps.c',
                 'wr/write-jpeg.c', 'wr/write-png.c', 'wr/write-tiff.c' ]
image_deps   = [ jpeg_dep, png_dep, tiff_dep,
                 pcd_dep, gif_dep, webp_dep ]

if pcd_dep.found()
    read_srcs += 'rd/read-pcd.c'
    config.set('HAVE_LIBPCD', true)
endif
if gif_dep.found()
    read_srcs += 'rd/read-gif.c'
    config.set('HAVE_LIBGIF', true)
endif
if webp_dep.found()
    read_srcs += 'rd/read-webp.c'
    config.set('HAVE_LIBWEBP', true)
endif
if systemd_dep.found()
    config.set('HAVE_SYSTEMD', true)
endif

# jpeg transformation support
jpeg_run     = run_command('scripts/jpeg-version.sh')
jpeg_ver     = jpeg_run.stdout().strip()
trans_src    = ''.join(['jpeg/', jpeg_ver,  '/transupp.c'])
trans_inc    = include_directories(''.join(['jpeg/', jpeg_ver]))

# finish & write configuration
if motif_dep.found()
    config.set('HAVE_MOTIF', true)
endif
if target_machine.system() == 'linux'
    config.set('SYSTEM_LINUX', true)
endif
configure_file(output : 'config.h', configuration : config)
add_global_arguments(['-include', 'config.h'], language : 'c')

########################################################################

# build fbi
fbi_srcs     = [ 'fbi.c', 'fb-gui.c', 'desktop.c',
                 'parseconfig.c', 'fbiconfig.c',
                 'vt.c', 'kbd.c', 'fbtools.c', 'drmtools.c', 'gfx.c',
                 'filter.c', 'op.c', 'jpegtools.c',
                 trans_src, read_srcs ]
fbi_deps     = [ drm_dep, pixman_dep, cairo_dep,
                 exif_dep, image_deps,
                 math_dep, udev_dep, input_dep, xkb_dep ]

executable('fbi',
           sources             : fbi_srcs,
           dependencies        : fbi_deps,
           include_directories : trans_inc,
           install             : true)
install_man('man/fbi.1')

# build exiftran
exiftr_srcs  = [ 'exiftran.c', 'genthumbnail.c', 'jpegtools.c',
                 'filter.c', 'op.c', 'readers.c', 'rd/read-jpeg.c',
                 trans_src ]
exiftr_deps  = [ jpeg_dep, exif_dep, math_dep, pixman_dep ]

executable('exiftran',
           sources             : exiftr_srcs,
           dependencies        : exiftr_deps,
           include_directories : trans_inc,
           install             : true)
install_man('man/exiftran.1')

# build thumbnail.cgi
executable('thumbnail.cgi',
           sources             : 'thumbnail.cgi.c',
           dependencies        : exif_dep)

# build fbpdf
fbpdf_srcs   = [ 'fbpdf.c', 'parseconfig.c', 'fbiconfig.c',
                 'vt.c', 'kbd.c', 'fbtools.c', 'drmtools.c', 'gfx.c' ]
fbpdf_deps   = [ drm_dep, gbm_dep, epoxy_dep,
                 pixman_dep, poppler_dep, cairo_dep,
                 udev_dep, input_dep, xkb_dep ]

executable('fbpdf',
           sources             : fbpdf_srcs,
           dependencies        : fbpdf_deps,
           install             : true)

# build fbcon
fbcon_srcs   = [ 'fbcon.c', 'drmtools.c', 'fbtools.c', 'gfx.c', 'vt.c', 'kbd.c', 'logind.c' ]
fbcon_deps   = [ drm_dep, cairo_dep, util_dep, udev_dep, input_dep, xkb_dep, glib_dep,
                 tsm_dep, systemd_dep ]

if tsm_dep.found()
    executable('fbcon',
               sources             : fbcon_srcs,
               dependencies        : fbcon_deps,
               install             : true)
    install_data('fbcon.desktop',
                 install_dir : 'share/wayland-sessions')
endif

# build kbdtest
executable('kbdtest',
           sources             : [ 'kbdtest.c', 'kbd.c' ],
           dependencies        : [ udev_dep, input_dep, xkb_dep, systemd_dep ] )

# build vttest
executable('vttest',
           sources             : [ 'vttest.c', 'kbd.c', 'logind.c' ],
           dependencies        : [ udev_dep, input_dep, xkb_dep, systemd_dep ] )

# build ida
mkfallback   = find_program('scripts/fallback.pl')
hexify       = find_program('scripts/hexify.sh')
copy         = find_program('cp')
ida_ad       = custom_target('ida-app-defaults-fallback',
                             input   : ['Ida.ad'],
                             output  : ['Ida.ad.h'],
                             command : [ mkfallback, '@INPUT@', '@OUTPUT@'])
ida_logo     = custom_target('ida-logo',
                             input   : ['logo.jpg'],
                             output  : ['logo.h'],
                             command : [ hexify, '@INPUT@', '@OUTPUT@'])
ida_srcs     = [ 'ida.c', 'man.c', 'hex.c', 'x11.c', 'viewer.c',
                 'icons.c', 'parseconfig.c', 'idaconfig.c',
                 'fileops.c', 'desktop.c', 'RegEdit.c', 'selections.c',
                 'xdnd.c', 'filebutton.c', 'filelist.c', 'browser.c',
                 'jpegtools.c', 'op.c', 'filter.c', 'lut.c', 'color.c',
                 trans_src, read_srcs, write_srcs,
                 'rd/read-xwd.c', 'rd/read-xpm.c',
                 ida_ad, ida_logo ]
ida_deps     = [ pixman_dep, exif_dep, image_deps, math_dep,
                 motif_dep, xpm_dep, xt_dep, xext_dep, x11_dep ]

if motif_dep.found()
    executable('ida',
               sources             : ida_srcs,
               dependencies        : ida_deps,
               include_directories : trans_inc,
               install             : true)
    install_man('man/ida.1')
    custom_target('ida-app-defaults',
                  input   : ['Ida.ad'],
                  output  : ['Ida'],
                  command : [ copy, '@INPUT@', '@OUTPUT@'],
                  install : true, install_dir : x11resdir)
endif
