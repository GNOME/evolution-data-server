lib_LTLIBRARIES = libgdata-1.2.la

libgdata_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DG_LOG_DOMAIN=\"libgdata\"			\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	$(GDATA_CFLAGS)

libgdata_1_2_la_SOURCES =			\
	gdata-feed.c                            \
	gdata-feed.h 		                \
	gdata-entry.c				\
	gdata-entry.h				\
	gdata-service-iface.c			\
	gdata-service-iface.h			

libgdata_1_2_la_LIBADD =			\
	$(GDATA_LIBS)

libgdata_1_2_la_LDFLAGS =                       \
        -version-info $(LIBGDATA_CURRENT):$(LIBGDATA_REVISION):$(LIBGDATA_AGE) $(NO_UNDEFINED)

libgdataincludedir = $(privincludedir)/google/libgdata

libgdatainclude_HEADERS =                  	\
        gdata-feed.h                       	\
	gdata-entry.h                      	\
	gdata-service-iface.h              	

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libgdata-$(API_VERSION).pc

EXTRA_DIST = $(pkgconfig_DATA:-$(API_VERSION).pc=.pc.in)

DISTCLEANFILES = $(pkgconfig_DATA)


-include $(top_srcdir)/git.mk
