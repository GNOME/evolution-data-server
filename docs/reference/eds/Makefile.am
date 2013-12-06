## Process this file with automake to produce Makefile.in

# This is a blank Makefile.am for using gtk-doc.
# Copy this to your project's API docs directory and modify the variables to
# suit your project. See the GTK+ Makefiles in gtk+/docs/reference for examples
# of using the various options.

# Some local variables
IMAGE_DIR = $(top_srcdir)/docs/reference/eds/images
IMAGE_SOURCE_DIR = $(top_srcdir)/docs/reference/eds/image-sources

# The name of the module, e.g. 'glib'.
DOC_MODULE = eds

# The top-level SGML file. You can change this if you want to.
DOC_MAIN_SGML_FILE = $(DOC_MODULE)-docs.sgml

# The directory containing the source code. Relative to $(srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting the functions and macros.
DOC_SOURCE_DIR = \
	$(top_srcdir)/libebackend			\
	$(top_srcdir)/libedataserver			\
	$(top_srcdir)/addressbook/libebook		\
	$(top_srcdir)/addressbook/libebook-contacts	\
	$(top_srcdir)/addressbook/libedata-book		\
	$(top_srcdir)/calendar/libecal			\
	$(top_srcdir)/calendar/libedata-cal		\
	$(top_srcdir)/tests/test-server-utils

# Extra options to pass to gtkdoc-scangobj. Not normally needed.
SCANGOBJ_OPTIONS =

# Extra options to supply to gtkdoc-scan
SCAN_OPTIONS = --deprecated-guards="EDS_DISABLE_DEPRECATED"

# Extra options to supply to gtkdoc-mkdb
# We must enable the SGML mode so that we can have HTML tables
# in the documentation (e.g. to illustrate phone number matching).
MKDB_OPTIONS = --sgml-mode --output-format=xml --name-space=e

# Extra options to supply to gtkdoc-mktmpl
# e.g. MKTMPL_OPTIONS=--only-section-tmpl
MKTMPL_OPTIONS=

# Extra options to supply to gtkdoc-fixref. Not normally needed.
# e.g. FIXXREF_OPTIONS=--extra-dir=../gdk-pixbuf/html --extra-dir=../gdk/html
# XXX We'll maybe want to use this to cross-ref into GTK+ docs...
FIXXREF_OPTIONS=

# Used for dependencies. The docs will be rebuilt if any of these change.
# e.g. HFILE_GLOB=$(top_srcdir)/gtk/*.h
# e.g. CFILE_GLOB=$(top_srcdir)/gtk/*.c
HFILE_GLOB = \
	$(top_srcdir)/libedataserver/*.h		\
	$(top_srcdir)/libebackend/*.h			\
	$(top_srcdir)/private/*.h			\
	$(top_srcdir)/addressbook/libebook-contacts/*.h	\
	$(top_srcdir)/addressbook/libebook/*.h		\
	$(top_srcdir)/addressbook/libedata-book/*.h	\
	$(top_srcdir)/calendar/libecal/*.h		\
	$(top_srcdir)/calendar/libedata-cal/*.h		\
	$(top_srcdir)/tests/test-server-utils/*.h

CFILE_GLOB = \
	$(top_srcdir)/libedataserver/*.c		\
	$(top_srcdir)/libebackend/*.c			\
	$(top_srcdir)/private/*.c			\
	$(top_srcdir)/addressbook/libebook-contacts/*.c	\
	$(top_srcdir)/addressbook/libebook/*.c		\
	$(top_srcdir)/addressbook/libedata-book/*.c	\
	$(top_srcdir)/calendar/libecal/*.c		\
	$(top_srcdir)/calendar/libedata-cal/*.c		\
	$(top_srcdir)/tests/test-server-utils/*.c

# Exceptions, files which should not be documented
LIBEBOOK_IGNORE_FILES =	\
	e-book-enumtypes.h		\
	e-book-contacts-enumtypes.h	\
	e-phone-number-private.h

LIBECAL_IGNORE_FILES = \
	e-cal-enumtypes.h		\
	e-data-cal-types.h

IGNORE_HFILES = config.h \
	$(LIBEBOOK_IGNORE_FILES)	\
	$(LIBECAL_IGNORE_FILES)

HTML_IMAGES = \
	$(IMAGE_DIR)/cursor-positions.png		\
	$(IMAGE_DIR)/cursor-positions-filtered.png	\
	$(IMAGE_DIR)/cursor-positions-step.png		\
	$(IMAGE_DIR)/cursor-alphabetic-indexes.png

HTML_IMAGE_SOURCE_FILES = \
	$(IMAGE_SOURCE_DIR)/cursor-positions.odg		\
	$(IMAGE_SOURCE_DIR)/cursor-positions-filtered.odg	\
	$(IMAGE_SOURCE_DIR)/cursor-positions-step.odg		\
	$(IMAGE_SOURCE_DIR)/cursor-alphabetic-indexes.odg

SGML_FILES= \
	eds-cursor-example.sgml

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
# e.g. content_files=running.sgml building.sgml changes-2.0.sgml
content_files= version.xml $(SGML_FILES)

# SGML files where gtk-doc abbrevations (#GtkWidget) are expanded
# These files must be listed here *and* in content_files
# e.g. expand_content_files=running.sgml
expand_content_files= $(SGML_FILES)

# CFLAGS and LDFLAGS for compiling gtkdoc-scangobj with your library.
# Only needed if you are using gtkdoc-scangobj to dynamically query widget
# signals and properties.
# e.g. INCLUDES=-I$(top_srcdir) -I$(top_builddir) $(GTK_DEBUG_FLAGS)
# e.g. GTKDOC_LIBS=$(top_builddir)/gtk/$(gtktargetlib)
GTKDOC_CFLAGS = \
	-I$(top_srcdir) 				\
	-I$(top_srcdir)/addressbook			\
	-I$(top_srcdir)/calendar			\
	-I$(top_builddir)				\
	-I$(top_builddir)/addressbook			\
	-I$(top_builddir)/calendar			\
	$(CAMEL_CFLAGS) $(E_DATA_SERVER_CFLAGS) $(E_BACKEND_CFLAGS)	\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) $(EVOLUTION_CALENDAR_CFLAGS)

GTKDOC_LIBS = \
	$(top_builddir)/camel/libcamel-1.2.la					\
	$(top_builddir)/libebackend/libebackend-1.2.la				\
	$(top_builddir)/libedataserver/libedataserver-1.2.la			\
	$(top_builddir)/addressbook/libebook-contacts/libebook-contacts-1.2.la	\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la			\
	$(top_builddir)/addressbook/libedata-book/libedata-book-1.2.la		\
	$(top_builddir)/calendar/libecal/libecal-1.2.la				\
	$(top_builddir)/calendar/libedata-cal/libedata-cal-1.2.la		\
	$(CAMEL_LIBS) $(E_DATA_SERVER_LIBS) $(E_BACKEND_LIBS)			\
	$(EVOLUTION_ADDRESSBOOK_LIBS) $(EVOLUTION_CALENDAR_LIBS)


# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

# Other files to distribute
# e.g. EXTRA_DIST += version.xml.in
EXTRA_DIST += version.xml.in $(HTML_IMAGE_SOURCE_FILES)

# Uncomment this to enable tests, currently we are still
# quite far from passing them
#TESTS = $(GTKDOC_CHECK)

-include $(top_srcdir)/git.mk
