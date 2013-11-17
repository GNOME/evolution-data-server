noinst_PROGRAMS = dump-vcard

dump_vcard_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/addressbook \
	-I$(top_builddir)/addressbook \
	$(EVOLUTION_ADDRESSBOOK_CFLAGS) \
	$(CAMEL_CFLAGS)

dump_vcard_LDADD = \
	$(top_builddir)/addressbook/libebook-contacts/libebook-contacts-1.2.la \
	$(top_builddir)/libedataserver/libedataserver-1.2.la \
	$(EVOLUTION_ADDRESSBOOK_LIBS) \
	$(CAMEL_LIBS)

EXTRA_DIST = \
	1.vcf \
	2.vcf \
	3.vcf \
	4.vcf \
	5.vcf \
	6.vcf \
	7.vcf \
	8.vcf \
	9.vcf \
	10.vcf \
	11.vcf \
	$(NULL)

-include $(top_srcdir)/git.mk
