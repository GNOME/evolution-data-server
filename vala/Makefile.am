vapidir = $(datarootdir)/vala/vapi
vapi_DATA = \
	libedataserver-1.2.vapi \
	libebook-1.2.vapi \
	$(NULL)
dist_vapi_DATA = \
	libebook-1.2.deps \
	$(NULL)

BUILT_SOURCES = \
	$(vapi_DATA) \
	$(NULL)

pkgconfigdir = ${libdir}/pkgconfig

libedataserver-1.2.vapi: $(top_builddir)/libedataserver/EDataServer-1.2.gir
	$(VAPIGEN_V)$(VAPIGEN) \
		--library libedataserver-1.2 \
		$< \
		$(NULL)

libebook-1.2.vapi: $(top_builddir)/addressbook/libebook/EBook-1.2.gir libedataserver-1.2.vapi
	$(VAPIGEN_V)$(VAPIGEN) \
		--vapidir=. \
		--pkg libedataserver-1.2 \
		--library libebook-1.2 \
		$< \
		$(NULL)

CLEANFILES = \
	$(BUILT_SOURCES) \
	$(NULL)

# set up the verbosity rules to avoid some build noise
VAPIGEN_V = $(VAPIGEN_V_$(V))
VAPIGEN_V_ = $(VAPIGEN_V_$(AM_DEFAULT_VERBOSITY))
VAPIGEN_V_0 = @echo "  VAPIG " $^;

-include $(top_srcdir)/git.mk
