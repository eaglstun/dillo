SUBDIRS = lout dw dlib dpip src doc dpid dpi test mac

dist_bin_SCRIPTS = dillo-install-hyphenation

EXTRA_DIST = README.md Doxyfile dillorc install-dpi-local d_size.h

sysconf_DATA = dillorc

desktopdir = $(datadir)/applications
desktop_DATA = dillo.desktop

appicon48dir = $(datadir)/icons/hicolor/48x48/apps/
appicon48_DATA = icons/48x48/dillo.png

appicon128dir = $(datadir)/icons/hicolor/128x128/apps/
appicon128_DATA = icons/128x128/dillo.png

EXTRA_DIST += $(desktop_DATA) $(appicon48_DATA) $(appicon128_DATA)

# DejaVu, bundled so dillo renders with its preferred fonts on systems that
# do not ship them. Registered at startup; see registerBundledFonts().
FONT_FILES = \
	fonts/DejaVuSans.ttf \
	fonts/DejaVuSans-Bold.ttf \
	fonts/DejaVuSans-Oblique.ttf \
	fonts/DejaVuSans-BoldOblique.ttf \
	fonts/DejaVuSerif.ttf \
	fonts/DejaVuSerif-Bold.ttf \
	fonts/DejaVuSerif-Italic.ttf \
	fonts/DejaVuSerif-BoldItalic.ttf \
	fonts/DejaVuSansMono.ttf \
	fonts/DejaVuSansMono-Bold.ttf \
	fonts/DejaVuSansMono-Oblique.ttf \
	fonts/DejaVuSansMono-BoldOblique.ttf \
	fonts/LICENSE

EXTRA_DIST += $(FONT_FILES)

if BUNDLE_FONTS
dillofontsdir = $(datadir)/dillo/fonts
dillofonts_DATA = $(FONT_FILES)
endif
