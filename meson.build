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
configure_file(output : 'config.h', configuration : config)
add_global_arguments(['-include', 'config.h'], language : 'c')

########################################################################

# build fbi
fbi_deps     = [ freetype_dep, fontconf_dep,
                 drm_dep, pixman_dep,
                 exif_dep, image_deps,
                 math_dep ]
fbi_srcs     = [ 'fbi.c', 'fb-gui.c', 'desktop.c',
                 'parseconfig.c', 'fbiconfig.c',
                 'vt.c', 'kbd.c', 'fbtools.c', 'drmtools.c',
                 'dither.c', 'filter.c', 'op.c', 'jpegtools.c', 
                 trans_src, read_srcs ]

executable('fbi',
           sources             : fbi_srcs,
           dependencies        : fbi_deps,
           include_directories : trans_inc)

# build exiftran
exiftr_srcs  = [ 'exiftran.c', 'genthumbnail.c', 'jpegtools.c',
                 'filter.c', 'op.c', 'readers.c', 'rd/read-jpeg.c',
                 trans_src ]
exiftr_deps  = [ jpeg_dep, exif_dep, math_dep, pixman_dep ]

executable('exiftran',
           sources             : exiftr_srcs,
           dependencies        : exiftr_deps,
           include_directories : trans_inc)

# build fbpdf
fbpdf_deps   = [ drm_dep, gbm_dep, epoxy_dep,
                 pixman_dep, poppler_dep, cairo_dep, cairo_gl_dep ]
fbpdf_srcs   = [ 'fbpdf.c', 'parseconfig.c', 'fbiconfig.c',
                 'vt.c', 'kbd.c', 'fbtools.c', 'drmtools.c', 'drmtools-egl.c' ]

executable('fbpdf',
           sources             : fbpdf_srcs,
           dependencies        : fbpdf_deps)

# build kbdtest
executable('kbdtest', [ 'kbdtest.c', 'kbd.c' ])
