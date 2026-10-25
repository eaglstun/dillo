AM_TESTS_ENVIRONMENT = env \
	TOP_SRCDIR='$(top_srcdir)' \
	TOP_BUILDDIR='$(top_builddir)' \
	SRCDIR='$(srcdir)' \
	BUILDDIR='$(builddir)'

LOG_COMPILER = $(srcdir)/driver.sh

TESTS = \
	cmd-load.sh \
	cmd-load-multi.sh \
	cmd-load-deadlock.sh \
	cmd-load-reply.sh \
	cmd-title.sh \
	cmd-introspect.sh \
	cmd-set.sh \
	cmd-submit.sh \
	cmd-follow.sh \
	cmd-noninteractive.sh

EXTRA_DIST = \
	$(TESTS) \
	driver.sh \
	test.html \
	form.html \
	form-target.html
