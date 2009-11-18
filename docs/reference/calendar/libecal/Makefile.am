# The name of the module.
DOC_MODULE = libecal

# The top-level SGML file.
DOC_MAIN_SGML_FILE = libecal-docs.sgml

# Extra options to supply to gtkdoc-scan
SCAN_OPTIONS = --deprecated-guards="EDS_DISABLE_DEPRECATED"

# The directory containing the source code. Relative to $(srcdir).
DOC_SOURCE_DIR = $(top_srcdir)/calendar/libecal

# Used for dependencies. The docs will be rebuilt if any of these change.
HFILE_GLOB = $(top_srcdir)/calendar/libecal/*.h
CFILE_GLOB = $(top_srcdir)/calendar/libecal/*.c

IGNORE_HFILES = \
	e-cal-marshal.h						\
	e-data-cal-bindings.h					\
	e-data-cal-factory-bindings.h				\
	e-data-cal-view-bindings.h

GTKDOC_CFLAGS = \
	-I$(top_srcdir) 					\
	-I$(top_srcdir)/calendar				\
	-I$(top_builddir)					\
	-I$(top_builddir)/calendar				\
	-I$(top_builddir)/calendar/libecal			\
	$(LIBICAL_CFLAGS)					\
	$(EVOLUTION_CALENDAR_CFLAGS)

GTKDOC_LIBS = \
	$(top_builddir)/calendar/libecal/libecal-1.2.la		\
	$(EVOLUTION_CALENDAR_LIBS)

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

-include $(top_srcdir)/git.mk
