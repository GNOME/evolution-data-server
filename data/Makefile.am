SUBDIRS = sources

gsettings_SCHEMAS = \
	org.gnome.Evolution.DefaultSources.gschema.xml

@GSETTINGS_RULES@

EXTRA_DIST = $(gsettings_SCHEMAS)

CLEANFILES = $(gsettings_SCHEMAS:.xml=.valid)

-include $(top_srcdir)/git.mk
