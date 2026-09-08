# Minimal macOS application bundle.
#
# Apple Help resolves a help book through the *application* bundle's
# Info.plist (CFBundleHelpBookFolder / CFBundleHelpBookName), so the help book
# cannot be shipped on its own -- hence this wrapper around the plain binary.

HELPBOOK = Dillo.help
APPDIR = Dillo.app
LPROJ = $(APPDIR)/Contents/Resources/$(HELPBOOK)/Contents/Resources/en.lproj

EXTRA_DIST = \
	Dillo.help/Contents/Info.plist \
	Dillo.help/Contents/Resources/en.lproj/index.html \
	Dillo.help/Contents/Resources/en.lproj/shortcuts.html \
	Dillo.help/Contents/Resources/en.lproj/browsing.html

if MACOS_APP
all-local: $(APPDIR)/Contents/Info.plist

# The search index is a build product: hiutil ships with the command line
# tools, and a stale index is worse than none.
$(APPDIR)/Contents/Info.plist: Info.plist $(top_builddir)/src/dillo $(EXTRA_DIST)
	rm -rf $(APPDIR)
	$(MKDIR_P) $(APPDIR)/Contents/MacOS $(APPDIR)/Contents/Resources
	cp -R $(srcdir)/$(HELPBOOK) $(APPDIR)/Contents/Resources/
	chmod -R u+w $(APPDIR)/Contents/Resources/$(HELPBOOK)
	hiutil -I corespotlight -C -a -m 3 -s en -f $(LPROJ)/search.cshelpindex $(LPROJ)
	cp $(top_builddir)/src/dillo $(APPDIR)/Contents/MacOS/dillo
	printf 'APPL????' > $(APPDIR)/Contents/PkgInfo
	cp Info.plist $@

clean-local:
	rm -rf $(APPDIR)
endif
