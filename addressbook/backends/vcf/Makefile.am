INCLUDES =						\
	-DG_LOG_DOMAIN=\"libebookbackend\"		\
	-I$(top_srcdir)					\
	-I$(top_builddir)				\
	-I$(top_srcdir)/addressbook			\
	-I$(top_builddir)/addressbook			\
        $(EVOLUTION_ADDRESSBOOK_CFLAGS)

noinst_LTLIBRARIES = libebookbackendvcf.la

libebookbackendvcf_la_SOURCES =			\
	e-book-backend-vcf.c			\
	e-book-backend-vcf.h

libebookbackendvcf_la_LIBADD =						\
	$(top_builddir)/addressbook/libedata-book/libedata-book-1.2.la	\
	$(top_builddir)/libedataserver/libedataserver-1.2.la