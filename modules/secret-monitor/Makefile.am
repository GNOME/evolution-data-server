NULL =

module_LTLIBRARIES = module-secret-monitor.la

module_secret_monitor_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-DG_LOG_DOMAIN=\"module-secret-monitor\" \
	$(E_BACKEND_CFLAGS) \
	$(E_DATA_SERVER_CFLAGS) \
	$(LIBSECRET_CFLAGS) \
	$(NULL)

module_secret_monitor_la_SOURCES = \
	module-secret-monitor.c \
	$(NULL)

module_secret_monitor_la_LIBADD = \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(E_BACKEND_LIBS) \
	$(E_DATA_SERVER_LIBS) \
	$(LIBSECRET_LIBS) \
	$(NULL)

module_secret_monitor_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) \
	$(NULL)

-include $(top_srcdir)/git.mk
