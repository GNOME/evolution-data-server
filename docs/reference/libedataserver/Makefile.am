# The name of the module.
DOC_MODULE = libedataserver

# The top-level SGML file.
DOC_MAIN_SGML_FILE = libedataserver-docs.sgml

# Extra options to supply to gtkdoc-scan
SCAN_OPTIONS = --deprecated-guards="EDS_DISABLE_DEPRECATED"

# The directory containing the source code. Relative to $(srcdir).
DOC_SOURCE_DIR = $(top_srcdir)/libedataserver

# Used for dependencies. The docs will be rebuilt if any of these change.
HFILE_GLOB = $(top_srcdir)/libedataserver/*.h
CFILE_GLOB = $(top_srcdir)/libedataserver/*.c
IGNORE_HFILES = 			\
	libedataserver-private.h

GTKDOC_CFLAGS  =					\
	-I$(top_srcdir)/libedataserver			\
	-I$(top_srcdir) 				\
	-I$(top_builddir)				\
	$(E_DATA_SERVER_CFLAGS)

GTKDOC_LIBS = 							\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(E_DATA_SERVER_LIBS)

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

-include $(top_srcdir)/git.mk
