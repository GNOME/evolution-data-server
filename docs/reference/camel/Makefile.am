# The name of the module.
DOC_MODULE = camel
DOC_MODULE_VERSIONED = $(DOC_MODULE)-$(CAMEL_MAJOR_VERSION).$(CAMEL_MINOR_VERSION)

# The top-level SGML file.
DOC_MAIN_SGML_FILE = camel-docs.sgml

# The directory containing the source code (if it contains documentation).
DOC_SOURCE_DIR = $(top_srcdir)/camel

HTML_DIR = $(datadir)/gtk-doc/html

# Extra options to supply to gtkdoc-scan
SCAN_OPTIONS = --deprecated-guards="CAMEL_DISABLE_DEPRECATED"

# Extra options to pass to gtkdoc-scangobj
SCANGOBJ_OPTIONS=--type-init-func="camel_init (\".\", 0)"

# Extra options to supply to gtkdoc-mkdb
MKDB_OPTIONS = --sgml-mode --output-format=xml --ignore-files=trio

# CFLAGS and LDFLAGS for compiling gtkdoc-scangobj with your library.
GTKDOC_CFLAGS = 		\
	-I$(top_srcdir)		\
	-I$(top_builddir)	\
	-I$(top_builddir)/camel	\
	$(GLIB_CFLAGS)

GTKDOC_LIBS = 					\
	$(top_builddir)/camel/libcamel-2.0.la	\
	$(GLIB_LIBS)

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE)
content_files = 			\
	tree_index.sgml

# Used for dependencies. The docs will be rebuilt if any of these change.
HFILE_GLOB=$(top_srcdir)/camel/*.h
CFILE_GLOB=$(top_srcdir)/camel/*.c

# Header files to ignore when scanning
# XXX Ignore the imapx provider for now.
IGNORE_HFILES = 			\
	broken-date-parser.h		\
	camel-charset-map-private.h	\
	camel-groupwise-private.h	\
	camel-i18n.h			\
	camel-imap-private.h		\
	camel-imap-types.h		\
	camel-local-private.h		\
	camel-net-utils-win32.h		\
	camel-nntp-private.h		\
	camel-nntp-types.h		\
	camel-private.h			\
	camel-search-private.h		\
	camel-types.h			\
	camel-imapx-exception.h		\
	camel-imapx-folder.h		\
	camel-imapx-server.h		\
	camel-imapx-store.h		\
	camel-imapx-store-summary.h	\
	camel-imapx-stream.h		\
	camel-imapx-summary.h		\
	camel-imapx-tokenise.h		\
	camel-imapx-utils.h		\
	camel-imapx-view-summary.h

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

# Other files to distribute
EXTRA_DIST += 				\
	$(content_files)		\
	$(DOC_MODULE)-docs.sgml		\
	$(DOC_MODULE)-sections.txt	\
	camel.hierarchy

-include $(top_srcdir)/git.mk
