NULL =

TRUST_PROMPT_SOURCES = trust-prompt-gtk.c
TRUST_PROMPT_CFLAGS = $(GNOME_PLATFORM_CFLAGS) $(GTK_CFLAGS) $(GCR_CFLAGS)
TRUST_PROMPT_LIBS = $(GNOME_PLATFORM_LIBS) $(GTK_LIBS) $(GCR_LIBS)

module_LTLIBRARIES = module-trust-prompt.la

module_trust_prompt_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-DG_LOG_DOMAIN=\"module-trust-prompt\" \
	$(E_BACKEND_CFLAGS) \
	$(E_DATA_SERVER_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(TRUST_PROMPT_CFLAGS) \
	$(NULL)

module_trust_prompt_la_SOURCES = \
	module-trust-prompt.c \
	trust-prompt.h \
	$(TRUST_PROMPT_SOURCES) \
	$(NULL)

module_trust_prompt_la_LIBADD = \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(top_builddir)/libebackend/libebackend-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(E_BACKEND_LIBS) \
	$(E_DATA_SERVER_LIBS) \
	$(CAMEL_LIBS) \
	$(TRUST_PROMPT_LIBS) \
	$(NULL)

module_trust_prompt_la_LDFLAGS = \
	-module -avoid-version $(NO_UNDEFINED) \
	$(NULL)

-include $(top_srcdir)/git.mk
