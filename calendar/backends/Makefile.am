if HAVE_GDATA_0_15_1
GTASKS_SUBDIR = gtasks
else
GTASKS_SUBDIR =
endif

if ENABLE_WEATHER
WEATHER_SUBDIR = weather
else
WEATHER_SUBDIR =
endif

SUBDIRS = file http contacts $(WEATHER_SUBDIR) caldav $(GTASKS_SUBDIR)

-include $(top_srcdir)/git.mk
