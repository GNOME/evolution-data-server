INCLUDES = 				\
	-I$(top_srcdir)			\
	-I$(top_builddir)		\
	-I$(top_srcdir)/addressbook	\
	-I$(top_builddir)/addressbook	\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)

noinst_PROGRAMS = dump-vcard

dump_vcard_LDADD = 						\
	$(top_builddir)/addressbook/libebook/libebook.la	\
	$(EVOLUTION_ADDRESSBOOK_LIBS)

EXTRA_DIST=1.vcf 2.vcf 3.vcf 4.vcf 5.vcf 6.vcf 7.vcf 8.vcf 9.vcf 10.vcf
