lib_LTLIBRARIES = libgdata-google-1.2.la

libgdata_google_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS)						\
	-DG_LOG_DOMAIN=\"libgdata-google\"			\
	-I$(top_srcdir)						\
	-I$(top_builddir)					\
	-I$(top_srcdir)/servers/google/libgdata			\
	$(GDATA_CFLAGS)

libgdata_google_1_2_la_SOURCES =				\
	gdata-google-service.c					\
	gdata-google-service.h				

libgdata_google_1_2_la_LIBADD =					\
	$(top_builddir)/servers/google/libgdata/libgdata-1.2.la	\
	$(GDATA_LIBS)

libgdata_google_1_2_la_LDFLAGS =	                       \
        -version-info $(LIBGDATA_GOOGLE_CURRENT):$(LIBGDATA_GOOGLE_REVISION):$(LIBGDATA_GOOGLE_AGE) $(NO_UNDEFINED)

libgdata_google_includedir = $(privincludedir)/google/libgdata-google

libgdata_google_include_HEADERS =       		        \
	gdata-google-service.h             	

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libgdata-google-$(API_VERSION).pc

EXTRA_DIST = $(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

DISTCLEANFILES = $(pkgconfig_DATA)


-include $(top_srcdir)/git.mk
