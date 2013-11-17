NULL =

# The name of the module.
DOC_MODULE = camel

# The top-level SGML file.
DOC_MAIN_SGML_FILE = $(DOC_MODULE)-docs.sgml

# The directory containing the source code (if it contains documentation).
DOC_SOURCE_DIR = $(top_srcdir)/camel

HTML_DIR = $(datadir)/gtk-doc/html

# Extra options to supply to gtkdoc-scan
SCAN_OPTIONS = --deprecated-guards="CAMEL_DISABLE_DEPRECATED"

# Extra options to pass to gtkdoc-scangobj
SCANGOBJ_OPTIONS =

# Extra options to supply to gtkdoc-mkdb
MKDB_OPTIONS = --sgml-mode --output-format=xml --name-space=camel

# CFLAGS and LDFLAGS for compiling gtkdoc-scangobj with your library.
GTKDOC_CFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/camel/providers \
	-I$(top_builddir) \
	-I$(top_builddir)/camel \
	-I$(top_builddir)/camel/providers \
	$(CAMEL_CFLAGS) \
	$(NULL)

GTKDOC_LIBS = \
	$(top_builddir)/camel/libcamel-1.2.la \
	$(top_builddir)/camel/providers/local/libcamellocal.la \
	$(top_builddir)/camel/providers/imapx/libcamelimapx.la \
	$(top_builddir)/camel/providers/nntp/libcamelnntp.la \
	$(top_builddir)/camel/providers/pop3/libcamelpop3.la \
	$(top_builddir)/camel/providers/sendmail/libcamelsendmail.la \
	$(top_builddir)/camel/providers/smtp/libcamelsmtp.la \
	$(CAMEL_LIBS) \
	$(NULL)

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE)
content_files = tree_index.sgml

# Used for dependencies. The docs will be rebuilt if any of these change.
HFILE_GLOB=$(top_srcdir)/camel/*.h
CFILE_GLOB=$(top_srcdir)/camel/*.c

# Header files to ignore when scanning
# XXX Ignore the imapx provider for now.
IGNORE_HFILES = \
	camel-charset-map-private.h \
	camel-enumtypes.h \
	camel-i18n.h \
	camel-imapx-tokenise.h \
	camel-imapx-utils.h \
	camel-local-private.h \
	camel-nntp-private.h \
	camel-nntp-resp-codes.h \
	camel-search-private.h \
	camel-win32.h \
	tests \
	$(NULL)

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

# Other files to distribute
EXTRA_DIST += \
	$(content_files) \
	$(DOC_MODULE)-docs.sgml \
	$(DOC_MODULE)-sections.txt \
	camel.hierarchy \
	$(NULL)

#TESTS = $(GTKDOC_CHECK)

-include $(top_srcdir)/git.mk
