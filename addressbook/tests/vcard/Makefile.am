INCLUDES = 				\
	-I$(top_srcdir)/addressbook	\
	$(EVOLUTION_ADDRESSBOOK_CFLAGS)

noinst_PROGRAMS = dump-vcard

dump_vcard_LDADD = 						\
	$(top_builddir)/addressbook/libebook/libebook.la	\
	$(EVOLUTION_ADDRESSBOOK_LIBS)
