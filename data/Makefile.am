NULL =

gsettings_SCHEMAS = \
	org.gnome.Evolution.DefaultSources.gschema.xml \
	org.gnome.evolution-data-server.calendar.gschema.xml \
	org.gnome.evolution-data-server.addressbook.gschema.xml \
	org.gnome.evolution.shell.network-config.gschema.xml \
	$(NULL)

@INTLTOOL_XML_NOMERGE_RULE@

@GSETTINGS_RULES@

# Compile the schemas locally, this is
# required for running sandboxed test cases
all-am:
	$(GLIB_COMPILE_SCHEMAS) .

convertdir = $(datadir)/GConf/gsettings
convert_DATA = evolution-data-server.convert

EXTRA_DIST = \
	$(gsettings_SCHEMAS:.xml=.xml.in) \
	$(convert_DATA) \
	$(NULL)

CLEANFILES = \
	$(gsettings_SCHEMAS) \
	gschemas.compiled \
	$(NULL)

-include $(top_srcdir)/git.mk
