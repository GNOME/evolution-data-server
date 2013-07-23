BUILT_SOURCES = cursor-example-resources.h cursor-example-resources.c

noinst_PROGRAMS = cursor-example
cursor_example_SOURCES =	\
	$(BUILT_SOURCES)	\
	cursor-data.c		\
	cursor-data.h		\
	cursor-example.c	\
	cursor-example.h	\
	cursor-navigator.c	\
	cursor-navigator.h	\
	cursor-search.c		\
	cursor-search.h		\
	cursor-slot.c		\
	cursor-slot.h		\
	main.c

cursor_example_CPPFLAGS =					\
	$(AM_CPPFLAGS)						\
	-DG_LOG_DOMAIN=\"cursor-example\"			\
	-I$(top_srcdir)						\
	-I$(top_builddir)					\
	-I$(top_srcdir)/addressbook				\
	-I$(top_builddir)/addressbook				\
	-I$(top_srcdir)/calendar     				\
	-I$(top_builddir)/calendar   				\
	-DSRCDIR=\""$(abs_srcdir)"\"				\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)				\
	$(GTK_CFLAGS) $(CAMEL_CFLAGS)				\
	$(CODE_COVERAGE_CFLAGS)					\
	$(NULL)
cursor_example_CFLAGS   = 
cursor_example_LDFLAGS  = \
	$(top_builddir)/addressbook/libebook/libebook-1.2.la	\
	$(top_builddir)/tests/test-server-utils/libetestserverutils.la	\
	$(top_builddir)/private/libedbus-private.la		\
	$(EVOLUTION_ADDRESSBOOK_LIBS)				\
	$(GTK_LIBS) $(CAMEL_LIBS)				\
	$(NULL)
cursor_example_LDADD =

UI_FILES = 			\
	cursor-example.ui	\
	cursor-search.ui	\
	cursor-slot.ui

# Generate resources
cursor-example-resources.h: cursor-example.gresources.xml $(UI_FILES)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $(srcdir)/cursor-example.gresources.xml \
		--target=$@ --sourcedir=$(srcdir) --c-name _cursor_example --generate-header
cursor-example-resources.c: cursor-example.gresources.xml $(UI_FILES)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $(srcdir)/cursor-example.gresources.xml \
		--target=$@ --sourcedir=$(srcdir) --c-name _cursor_example --generate-source

EXTRA_DIST = $(UI_FILES) cursor-example.gresources.xml
CLEANFILES = $(BUILT_SOURCES)
