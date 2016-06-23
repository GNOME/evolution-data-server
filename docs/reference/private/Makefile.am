NULL =

# The name of the module.
DOC_MODULE = libedbus-private

# The top-level SGML file.
DOC_MAIN_SGML_FILE = $(DOC_MODULE)-docs.sgml

# Extra options to supply to gtkdoc-scan
SCAN_OPTIONS = --deprecated-guards="EDS_DISABLE_DEPRECATED" --rebuild-sections --rebuild-types

# The directory containing the source code. Relative to $(srcdir).
DOC_SOURCE_DIR = $(top_srcdir)/private

# Used for dependencies. The docs will be rebuilt if any of these change.
HFILE_GLOB = $(top_builddir)/private/*.h
CFILE_GLOB = $(top_builddir)/private/*.c

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

$(DOC_MAIN_SGML_FILE): $(DOC_MAIN_SGML_FILE).in
	sed -e 's|\@SRCDIR\@|$(abs_top_builddir)|g' $< > $(@)

GITIGNOREFILES = \
	$(DOC_MODULE)-overrides.txt \
	$(DOC_MODULE)-sections.txt \
	$(DOC_MODULE).hierarchy \
	$(DOC_MODULE).types \
	$(NULL)

BUILT_SOURCES = $(DOC_MAIN_SGML_FILE)
DISTCLEANFILES = $(BUILT_SOURCES) $(GITIGNOREFILES)

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

EXTRA_DIST += $(DOC_MAIN_SGML_FILE).in

CLEANFILES += $(BUILT_SOURCES)

#TESTS = $(GTKDOC_CHECK)

-include $(top_srcdir)/git.mk
