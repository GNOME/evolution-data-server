vapidir = $(datarootdir)/vala/vapi
vapi_DATA = \
	libedataserver-1.2.vapi \
	libebook-1.2.vapi \
	libecalendar-1.2.vapi \
	$(NULL)
dist_vapi_DATA = \
	libebook-1.2.deps \
	libecalendar-1.2.deps \
	$(NULL)

EXTRA_DIST = \
	EBook-1.2.metadata \
	libebook-1.2-custom.vala \
	ECalendar-1.2.metadata \
	EDataServer-1.2.metadata \
	$(NULL)

BUILT_SOURCES = \
	$(vapi_DATA) \
	$(NULL)

pkgconfigdir = ${libdir}/pkgconfig

libedataserver-1.2.vapi: $(top_builddir)/libedataserver/EDataServer-1.2.gir
	$(VAPIGEN_V)$(VAPIGEN) \
		--pkg gio-2.0 \
		--library libedataserver-1.2 \
		--metadatadir=$(srcdir) \
		$< \
		$(NULL)
	@mv $@ libedataserver-1.2.vapi.tmp
	$(AM_V_GEN)sed \
		'-e s/xml\.NodePtr/Xml\.Node/' \
		'-e s/xml\.DocPtr/Xml\.Doc/' \
		'-e s/xml\.Doc /Xml\.Doc /' \
		< libedataserver-1.2.vapi.tmp > $@
	@rm  libedataserver-1.2.vapi.tmp

libecalendar-1.2.vapi: $(top_builddir)/calendar/libecal/ECalendar-1.2.gir libedataserver-1.2.vapi
	$(VAPIGEN_V)$(VAPIGEN) \
		--vapidir=. \
		--girdir=$(top_builddir)/libedataserver \
		--pkg libedataserver-1.2 \
		--pkg gio-2.0 \
		--pkg libxml-2.0 \
		--library libecalendar-1.2 \
		--metadatadir=$(srcdir) \
		$< \
		$(NULL)

libebook-1.2.vapi: $(top_builddir)/addressbook/libebook/EBook-1.2.gir libedataserver-1.2.vapi libebook-1.2-custom.vala
	$(VAPIGEN_V)$(VAPIGEN) \
		--vapidir=. \
		--vapidir=$(srcdir) \
		--girdir=$(top_builddir)/libedataserver \
		--pkg libedataserver-1.2 \
		--pkg gio-2.0 \
		--pkg libxml-2.0 \
		--library libebook-1.2 \
		--metadatadir=$(srcdir) \
		$< \
		$(srcdir)/libebook-1.2-custom.vala \
		$(NULL)

CLEANFILES = \
	$(BUILT_SOURCES) \
	$(NULL)

# set up the verbosity rules to avoid some build noise
VAPIGEN_V = $(VAPIGEN_V_$(V))
VAPIGEN_V_ = $(VAPIGEN_V_$(AM_DEFAULT_VERBOSITY))
VAPIGEN_V_0 = @echo "  VAPIG " $^;

-include $(top_srcdir)/git.mk
