# Minimal macOS application bundle.
#
# Apple Help resolves a help book through the *application* bundle's
# Info.plist (CFBundleHelpBookFolder / CFBundleHelpBookName), so the help book
# cannot be shipped on its own -- hence this wrapper around the plain binary.

HELPBOOK = Dillo.help
APPDIR = Dillo.app
ICON = Dillo.icns
ICON_PNG = Dillo-512.png
ICON_SOURCE = $(top_srcdir)/icons/128x128/dillo.png
LPROJ = $(APPDIR)/Contents/Resources/$(HELPBOOK)/Contents/Resources/en.lproj

EXTRA_DIST = \
	Dillo.help/Contents/Info.plist \
	Dillo.help/Contents/Resources/en.lproj/index.html \
	Dillo.help/Contents/Resources/en.lproj/shortcuts.html \
	Dillo.help/Contents/Resources/en.lproj/browsing.html

if MACOS_APP
all-local: $(APPDIR)/Contents/Info.plist

# Ship the control-socket client inside the bundle, next to dillo, so
# Shortcuts' "Run Shell Script" (and any script) can reach it at a fixed path:
# /Applications/Dillo.app/Contents/MacOS/dilloc. See AUTOMATION.md.
if ENABLE_CONTROL_SOCKET
BUNDLE_DILLOC = $(top_builddir)/src/dilloc
endif

# The search index is a build product: hiutil ships with the command line
# tools, and a stale index is worse than none.
$(ICON): $(ICON_SOURCE)
	sips -z 512 512 $(ICON_SOURCE) --out $(ICON_PNG) >/dev/null
	sips -s format icns $(ICON_PNG) --out $@ >/dev/null
	rm -f $(ICON_PNG)

$(APPDIR)/Contents/Info.plist: Info.plist $(top_builddir)/src/dillo $(BUNDLE_DILLOC) $(ICON) $(EXTRA_DIST)
	rm -rf $(APPDIR)
	$(MKDIR_P) $(APPDIR)/Contents/MacOS $(APPDIR)/Contents/Resources
	cp -R $(srcdir)/$(HELPBOOK) $(APPDIR)/Contents/Resources/
	chmod -R u+w $(APPDIR)/Contents/Resources/$(HELPBOOK)
	hiutil -I corespotlight -C -a -m 3 -s en -f $(LPROJ)/search.cshelpindex $(LPROJ)
	cp $(top_builddir)/src/dillo $(APPDIR)/Contents/MacOS/dillo
	if test -n "$(BUNDLE_DILLOC)"; then \
	   cp $(BUNDLE_DILLOC) $(APPDIR)/Contents/MacOS/dilloc; \
	fi
	cp $(ICON) $(APPDIR)/Contents/Resources/$(ICON)
	printf 'APPL????' > $(APPDIR)/Contents/PkgInfo
	cp Info.plist $@

clean-local:
	rm -rf $(APPDIR) $(ICON) $(ICON_PNG)
endif
