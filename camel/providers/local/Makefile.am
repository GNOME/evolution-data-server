NULL =

camel_provider_LTLIBRARIES = libcamellocal.la
camel_provider_DATA = libcamellocal.urls

if OS_WIN32
SYSDEP_PROVIDER_SOURCES =
else
SYSDEP_PROVIDER_SOURCES = \
	camel-spool-folder.c \
	camel-spool-settings.c \
	camel-spool-store.c \
	camel-spool-summary.c \
	$(NULL)
endif

libcamellocal_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir)/camel \
	-I$(top_srcdir) \
	$(CAMEL_CFLAGS) \
	-DG_LOG_DOMAIN=\"camel-local-provider\" \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libcamellocal_la_SOURCES = \
	camel-local-folder.c \
	camel-local-store.c \
	camel-local-summary.c \
	camel-local-private.c \
	camel-local-provider.c \
	camel-mbox-folder.c \
	camel-mbox-store.c \
	camel-mbox-summary.c \
	camel-mh-folder.c \
	camel-mh-settings.c \
	camel-mh-store.c \
	camel-mh-summary.c \
	camel-maildir-folder.c \
	camel-maildir-store.c \
	camel-maildir-summary.c \
	$(SYSDEP_PROVIDER_SOURCES) \
	$(NULL)

noinst_HEADERS = \
	camel-local-folder.h \
	camel-local-store.h \
	camel-local-summary.h \
	camel-mh-folder.h \
	camel-mh-settings.h \
	camel-mh-store.h \
	camel-mh-summary.h \
	camel-mbox-folder.h \
	camel-mbox-store.h \
	camel-mbox-summary.h \
	camel-maildir-folder.h \
	camel-maildir-store.h \
	camel-maildir-summary.h \
	camel-spool-folder.h \
	camel-spool-settings.h \
	camel-spool-store.h \
	camel-spool-summary.h \
	camel-local-private.h \
	$(NULL)

libcamellocal_la_LDFLAGS = -avoid-version -module $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

libcamellocal_la_LIBADD = \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(CAMEL_LIBS) \
	$(NULL)

EXTRA_DIST = libcamellocal.urls

-include $(top_srcdir)/git.mk
