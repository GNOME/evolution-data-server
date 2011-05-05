if ENABLE_WEATHER
WEATHER_SUBDIR = weather
else
WEATHER_SUBDIR =
endif

SUBDIRS = file http contacts $(WEATHER_SUBDIR) caldav

-include $(top_srcdir)/git.mk
