NULL =

# These are non-removable and non-writable.
ro_sources_in_files = \
	caldav-stub.source.in \
	contacts-stub.source.in \
	google-stub.source.in \
	ldap-stub.source.in \
	local-stub.source.in \
	weather-stub.source.in \
	webcal-stub.source.in \
	webdav-stub.source.in \
	$(NULL)
ro_sources_DATA = $(ro_sources_in_files:.in=)

# These are non-removable, but can be changed
# and written to the user's sources directory.
rw_sources_in_files = \
	birthdays.source.in \
	local.source.in \
	sendmail.source.in \
	system-address-book.source.in \
	system-calendar.source.in \
	system-memo-list.source.in \
	system-task-list.source.in \
	vfolder.source.in \
	$(NULL)
rw_sources_DATA = $(rw_sources_in_files:.in=)

%: %.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*.po)
	LC_ALL=C $(INTLTOOL_MERGE) -d -u -c $(top_builddir)/po/.intltool-merge-cache $(top_srcdir)/po $< $@

EXTRA_DIST = $(ro_sources_in_files) $(rw_sources_in_files)

CLEANFILES = $(ro_sources_DATA) $(rw_sources_DATA)

-include $(top_srcdir)/git.mk
