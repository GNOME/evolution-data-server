NULL =

phone_numbers_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir)/addressbook \
	-I$(top_srcdir)/addressbook/backends/google \
	-DG_LOG_DOMAIN=\"evolution-tests\" \
	$(NULL)

phone_numbers_CFLAGS = \
	$(AM_CFLAGS) \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(GDATA_CFLAGS) \
	$(CAMEL_CFLAGS) \
	$(NULL)

LDADD = \
	$(AM_LDADD) \
	$(top_builddir)/addressbook/backends/google/libebook-google-utils.la \
	$(EVOLUTION_ADDRESSBOOK_LIBS) \
	$(GDATA_LIBS) \
	$(NULL)

noinst_PROGRAMS = \
	phone-numbers \
	$(NULL)
TESTS = $(noinst_PROGRAMS)

phone_numbers_SOURCES = phone-numbers.c

-include $(top_srcdir)/git.mk
