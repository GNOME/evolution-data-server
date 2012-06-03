ebook_backend_LTLIBRARIES = libebookbackendvcf.la

libebookbackendvcf_la_CPPFLAGS = \
	$(AM_CPPFLAGS)					\
	-DG_LOG_DOMAIN=\"libebookbackend\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)/addressbook			\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)			\
	$(CODE_COVERAGE_CFLAGS)				\
	$(CAMEL_CFLAGS)					\
	$(NULL)

libebookbackendvcf_la_SOURCES =			\
	e-book-backend-vcf.c			\
	e-book-backend-vcf.h			\
	e-book-backend-vcf-factory.c		\
	e-source-vcf.c				\
	e-source-vcf.h

libebookbackendvcf_la_LIBADD =						\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la		\
	$(top_builddir)/addressbook/libedata-book/libedata-book-1.2.la	\
	$(top_builddir)/libedataserver/libedataserver-1.2.la		\
	$(top_builddir)/libebackend/libebackend-1.2.la		\
	$(EVOLUTION_ADDRESSBOOK_LIBS) \
	$(CAMEL_LIBS)

libebookbackendvcf_la_LDFLAGS =		\
	-module -avoid-version $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

-include $(top_srcdir)/git.mk
