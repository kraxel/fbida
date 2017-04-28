# -*-python-*-
project('fbida', 'c')

# tweak warnings
add_global_arguments('-Wno-pointer-sign', language : 'c')

# init configuration
config       = configuration_data()
version      = run_command('cat', 'VERSION')
config.set_quoted('VERSION', version.stdout().strip())

# pkg-config deps
freetype_dep = dependency('freetype2')
fontconf_dep = dependency('fontconfig')
pixman_dep   = dependency('pixman-1')
poppler_dep  = dependency('poppler-glib')
cairo_dep    = dependency('cairo')
cairo_gl_dep = dependency('cairo-gl')
drm_dep      = dependency('libdrm')
gbm_dep      = dependency('gbm')
epoxy_dep    = dependency('epoxy')
exif_dep     = dependency('libexif')
png_dep      = dependency('libpng')
tiff_dep     = dependency('libtiff-4')
webp_dep     = dependency('libwebp', required : false)

# other library deps
cc           = meson.get_compiler('c')
jpeg_dep     = cc.find_library('jpeg')
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

# jpeg transformation support
jpeg_run     = run_command('scripts/jpeg-version.sh')
jpeg_ver     = jpeg_run.stdout().strip()
trans_src    = ''.join(['jpeg/', jpeg_ver,  '/transupp.c'])
trans_inc    = include_directories(''.join(['jpeg/', jpeg_ver]))

# finish & write configuration
if cairo_gl_dep.found()
    config.set('HAVE_CAIRO_GL', true)
endif
if motif_dep.found()
    config.set('HAVE_MOTIF', true)
endif
configure_file(output : 'config.h', configuration : config)
add_global_arguments(['-include', 'config.h'], language : 'c')

########################################################################

# build fbi
fbi_srcs     = [ 'fbi.c', 'fb-gui.c', 'desktop.c',
                 'parseconfig.c', 'fbiconfig.c',
                 'vt.c', 'kbd.c', 'fbtools.c', 'drmtools.c',
                 'dither.c', 'filter.c', 'op.c', 'jpegtools.c', 
                 trans_src, read_srcs ]
fbi_deps     = [ freetype_dep, fontconf_dep,
                 drm_dep, pixman_dep,
                 exif_dep, image_deps,
                 math_dep ]

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
                 'vt.c', 'kbd.c', 'fbtools.c', 'drmtools.c', 'drmtools-egl.c' ]
fbpdf_deps   = [ drm_dep, gbm_dep, epoxy_dep,
                 pixman_dep, poppler_dep, cairo_dep, cairo_gl_dep ]

executable('fbpdf',
           sources             : fbpdf_srcs,
           dependencies        : fbpdf_deps,
           install             : true)

# build kbdtest
executable('kbdtest', [ 'kbdtest.c', 'kbd.c' ])

# build ida
mkfallback   = find_program('scripts/fallback.pl')
hexify       = find_program('scripts/hexify.sh')
ida_ad       = custom_target('ida-app-defaults',
                             input   : ['Ida.ad'],
                             output  : ['Ida.ad.h'],
                             command : [ mkfallback, '@INPUT@', '@OUTPUT@'])
ida_logo     = custom_target('ida-logo',
                             input   : ['logo.jpg'],
                             output  : ['logo.h'],
                             command : [ hexify, '@INPUT@', '@OUTPUT@'])
ida_srcs     = [ 'ida.c', 'man.c', 'hex.c', 'x11.c', 'viewer.c',
                 'dither.c', 'icons.c', 'parseconfig.c', 'idaconfig.c',
                 'fileops.c', 'desktop.c', 'RegEdit.c', 'selections.c',
                 'xdnd.c', 'filebutton.c', 'filelist.c', 'browser.c',
                 'jpegtools.c', 'op.c', 'filter.c', 'lut.c', 'color.c',
                 trans_src, read_srcs, write_srcs,
                 'rd/read-xwd.c', 'rd/read-xpm.c',
                 ida_ad, ida_logo ]
ida_deps     = [ pixman_dep, exif_dep, image_deps, math_dep,
                 motif_dep, xpm_dep, xt_dep, xext_dep, x11_dep ]

executable('ida',
           sources             : ida_srcs,
           dependencies        : ida_deps,
           include_directories : trans_inc,
           install             : true)
install_man('man/ida.1')
