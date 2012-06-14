SUBDIRS = sources

gsettings_SCHEMAS =						\
	org.gnome.Evolution.DefaultSources.gschema.xml		\
	org.gnome.evolution-data-server.calendar.gschema.xml	\
	org.gnome.evolution-data-server.addressbook.gschema.xml

@INTLTOOL_XML_NOMERGE_RULE@

@GSETTINGS_RULES@

convertdir = $(datadir)/GConf/gsettings
convert_DATA = evolution-data-server.convert

EXTRA_DIST = $(gsettings_SCHEMAS:.xml=.xml.in) \
	     $(convert_DATA)

CLEANFILES = $(gsettings_SCHEMAS)

-include $(top_srcdir)/git.mk
