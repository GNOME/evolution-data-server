# The name of the module.
DOC_MODULE = libebackend

# The top-level SGML file.
DOC_MAIN_SGML_FILE = libebackend-docs.xml

# Extra options to supply to gtkdoc-scan
SCAN_OPTIONS = --deprecated-guards="EDS_DISABLE_DEPRECATED"

# The directory containing the source code. Relative to $(srcdir).
DOC_SOURCE_DIR = $(top_srcdir)/libebackend

# Used for dependencies. The docs will be rebuilt if any of these change.
HFILE_GLOB = $(top_srcdir)/libebackend/*.h
CFILE_GLOB = $(top_srcdir)/libebackend/*.c
IGNORE_HFILES = 

GTKDOC_CFLAGS  =					\
	-I$(top_srcdir)/libebackend			\
	-I$(top_srcdir) 				\
	-I$(top_builddir)				\
	$(E_BACKEND_CFLAGS)

GTKDOC_LIBS = 							\
	$(top_builddir)/libebackend/libebackend-1.2.la		\
	$(top_builddir)/libedataserver/libedataserver-1.2.la	\
	$(E_BACKEND_LIBS)

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

-include $(top_srcdir)/git.mk
