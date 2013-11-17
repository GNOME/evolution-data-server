NULL =

# The name of the module.
DOC_MODULE = libedbus-private

# The top-level SGML file.
DOC_MAIN_SGML_FILE = $(DOC_MODULE)-docs.sgml

# Extra options to supply to gtkdoc-scan
SCAN_OPTIONS = --deprecated-guards="EDS_DISABLE_DEPRECATED"

# The directory containing the source code. Relative to $(srcdir).
DOC_SOURCE_DIR = $(top_srcdir)/private

# Used for dependencies. The docs will be rebuilt if any of these change.
HFILE_GLOB = $(top_srcdir)/private/*.h
CFILE_GLOB = $(top_srcdir)/private/*.c

# Ignore interfaces from freedesktop.org.
IGNORE_HFILES = \
	e-dbus-localed.h \
	$(NULL)

GTKDOC_CFLAGS = \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	$(E_DATA_SERVER_CFLAGS) \
	$(NULL)

GTKDOC_LIBS = \
	$(top_builddir)/private/libedbus-private.la \
	$(E_DATA_SERVER_LIBS) \
	$(NULL)

# Extra options to supply to gtkdoc-mkdb
# This is needed for cross-references to D-Bus interfaces to work.
MKDB_OPTIONS = --output-format=xml --sgml-mode --name-space=e-dbus

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

#TESTS = $(GTKDOC_CHECK)

-include $(top_srcdir)/git.mk
