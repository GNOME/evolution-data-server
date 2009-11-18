## Process this file with automake to produce Makefile.in
# The name of the module.
DOC_MODULE = libedata-cal

content_files=
HTML_IMAGES=

# The top-level SGML file.
DOC_MAIN_SGML_FILE = libedata-cal-docs.sgml

# Extra options to supply to gtkdoc-scan
SCAN_OPTIONS = --deprecated-guards="EDS_DISABLE_DEPRECATED"

# The directory containing the source code. Relative to $(srcdir).
DOC_SOURCE_DIR = $(top_srcdir)/calendar/libedata-cal

# Used for dependencies. The docs will be rebuilt if any of these change.
HFILE_GLOB = $(top_srcdir)/calendar/libedata-cal/*.h
CFILE_GLOB = $(top_srcdir)/calendar/libedata-cal/*.c

IGNORE_HFILES = \
	e-data-cal-glue.h				\
	e-data-cal-factory-glue.h			\
	e-data-cal-view-glue.h				\
	e-data-cal-marshal.h				\
	e-data-cal-types.h

GTKDOC_CFLAGS = \
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)				\
	-I$(top_builddir)/calendar			\
	$(LIBICAL_CFLAGS)				\
	$(EVOLUTION_CALENDAR_CFLAGS)

GTKDOC_LIBS = \
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la	\
	$(top_builddir)/libebackend/libebackend-1.2.la			\
	$(EVOLUTION_CALENDAR_LIBS)					\
	$(DB_LIBS)

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

-include $(top_srcdir)/git.mk
