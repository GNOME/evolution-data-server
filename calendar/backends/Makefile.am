if ENABLE_WEATHER
WEATHER_SUBDIR = weather
else
WEATHER_SUBDIR =
endif

SUBDIRS = file groupwise http contacts $(WEATHER_SUBDIR) caldav google

-include $(top_srcdir)/git.mk
