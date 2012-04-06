NULL =

module_LTLIBRARIES = module-cache-reaper.la

module_cache_reaper_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-DG_LOG_DOMAIN=\"module-cache-reaper\" \
	$(E_BACKEND_CFLAGS) \
	$(E_DATA_SERVER_CFLAGS) \
	$(NULL)

module_cache_reaper_la_SOURCES = \
	module-cache-reaper.c \
	e-cache-reaper-utils.c \
	e-cache-reaper-utils.h \
	$(NULL)

module_cache_reaper_la_LIBADD = \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(E_BACKEND_LIBS) \
	$(E_DATA_SERVER_LIBS) \
	$(NULL)

module_cache_reaper_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) \
	$(NULL)

-include $(top_srcdir)/git.mk
