NULL =

$(GENERATED_DBUS_LOCALE) : Makefile.am org.freedesktop.locale1.xml
	$(AM_V_GEN) gdbus-codegen \
	--interface-prefix org.freedesktop. \
	--c-namespace E_DBus \
	--generate-c-code e-dbus-localed \
	$(top_srcdir)/private/org.freedesktop.locale1.xml \
	$(NULL)

$(GENERATED_DBUS_SOURCE) : Makefile.am org.gnome.evolution.dataserver.Source.xml
	$(AM_V_GEN) gdbus-codegen \
	--interface-prefix org.gnome.evolution.dataserver. \
	--c-namespace E_DBus \
	--c-generate-object-manager \
	--generate-c-code e-dbus-source \
	--generate-docbook e-dbus-source \
	$(top_srcdir)/private/org.gnome.evolution.dataserver.Source.xml \
	$(NULL)

$(GENERATED_DBUS_SOURCE_MANAGER) : Makefile.am org.gnome.evolution.dataserver.SourceManager.xml
	$(AM_V_GEN) gdbus-codegen \
	--interface-prefix org.gnome.evolution.dataserver. \
	--c-namespace E_DBus \
	--generate-c-code e-dbus-source-manager \
	--generate-docbook e-dbus-source-manager \
	$(top_srcdir)/private/org.gnome.evolution.dataserver.SourceManager.xml \
	$(NULL)

$(GENERATED_DBUS_AUTHENTICATOR) : Makefile.am org.gnome.evolution.dataserver.Authenticator.xml
	$(AM_V_GEN) gdbus-codegen \
	--interface-prefix org.gnome.evolution.dataserver \
	--c-namespace E_DBus \
	--generate-c-code e-dbus-authenticator \
	--generate-docbook e-dbus-authenticator \
	$(top_srcdir)/private/org.gnome.evolution.dataserver.Authenticator.xml \
	$(NULL)

$(GENERATED_DBUS_ADDRESS_BOOK) : Makefile.am org.gnome.evolution.dataserver.AddressBook.xml
	$(AM_V_GEN) gdbus-codegen \
	--interface-prefix org.gnome.evolution.dataserver \
	--c-namespace E_DBus \
	--generate-c-code e-dbus-address-book \
	--generate-docbook e-dbus-address-book \
	$(top_srcdir)/private/org.gnome.evolution.dataserver.AddressBook.xml \
	$(NULL)

$(GENERATED_DBUS_ADDRESS_BOOK_CURSOR) : Makefile.am org.gnome.evolution.dataserver.AddressBookCursor.xml
	$(AM_V_GEN) gdbus-codegen \
	--interface-prefix org.gnome.evolution.dataserver \
	--c-namespace E_DBus \
	--generate-c-code e-dbus-address-book-cursor \
	--generate-docbook e-dbus-address-book-cursor \
	$(top_srcdir)/private/org.gnome.evolution.dataserver.AddressBookCursor.xml \
	$(NULL)

$(GENERATED_DBUS_DIRECT_BOOK) : Makefile.am org.gnome.evolution.dataserver.DirectBook.xml
	$(AM_V_GEN) gdbus-codegen \
	--interface-prefix org.gnome.evolution.dataserver \
	--c-namespace E_DBus \
	--generate-c-code e-dbus-direct-book \
	--generate-docbook e-dbus-direct-book \
	$(top_srcdir)/private/org.gnome.evolution.dataserver.DirectBook.xml \
	$(NULL)

$(GENERATED_DBUS_ADDRESS_BOOK_FACTORY) : Makefile.am org.gnome.evolution.dataserver.AddressBookFactory.xml
	$(AM_V_GEN) gdbus-codegen \
	--interface-prefix org.gnome.evolution.dataserver \
	--c-namespace E_DBus \
	--generate-c-code e-dbus-address-book-factory \
	--generate-docbook e-dbus-address-book-factory \
	$(top_srcdir)/private/org.gnome.evolution.dataserver.AddressBookFactory.xml \
	$(NULL)

$(GENERATED_DBUS_CALENDAR) : Makefile.am org.gnome.evolution.dataserver.Calendar.xml
	$(AM_V_GEN) gdbus-codegen \
	--interface-prefix org.gnome.evolution.dataserver \
	--c-namespace E_DBus \
	--generate-c-code e-dbus-calendar \
	--generate-docbook e-dbus-calendar \
	$(top_srcdir)/private/org.gnome.evolution.dataserver.Calendar.xml \
	$(NULL)

$(GENERATED_DBUS_CALENDAR_FACTORY) : Makefile.am org.gnome.evolution.dataserver.CalendarFactory.xml
	$(AM_V_GEN) gdbus-codegen \
	--interface-prefix org.gnome.evolution.dataserver \
	--c-namespace E_DBus \
	--generate-c-code e-dbus-calendar-factory \
	--generate-docbook e-dbus-calendar-factory \
	$(top_srcdir)/private/org.gnome.evolution.dataserver.CalendarFactory.xml \
	$(NULL)

$(GENERATED_DBUS_USER_PROMPTER) : Makefile.am org.gnome.evolution.dataserver.UserPrompter.xml
	$(AM_V_GEN) gdbus-codegen \
	--interface-prefix org.gnome.evolution.dataserver. \
	--c-namespace E_DBus \
	--generate-c-code e-dbus-user-prompter \
	--generate-docbook e-dbus-user-prompter \
	$(top_srcdir)/private/org.gnome.evolution.dataserver.UserPrompter.xml \
	$(NULL)

$(GENERATED_DBUS_SUBPROCESS_BACKEND) : Makefile.am org.gnome.evolution.dataserver.Subprocess.Backend.xml
	$(AM_V_GEN) gdbus-codegen \
	--interface-prefix org.gnome.evolution.dataserver.Subprocess \
	--c-namespace E_DBus_Subprocess \
	--c-generate-object-manager \
	--generate-c-code e-dbus-subprocess-backend \
	--generate-docbook e-dbus-subprocess-backend \
	$(top_srcdir)/private/org.gnome.evolution.dataserver.Subprocess.Backend.xml \
	$(NULL)

GENERATED_DBUS_LOCALE = \
	e-dbus-localed.c \
	e-dbus-localed.h \
	$(NULL)

GENERATED_DBUS_SOURCE = \
	e-dbus-source.c \
	e-dbus-source.h \
	e-dbus-source-org.gnome.evolution.dataserver.Source.xml \
	e-dbus-source-org.gnome.evolution.dataserver.Source.Removable.xml \
	e-dbus-source-org.gnome.evolution.dataserver.Source.Writable.xml \
	e-dbus-source-org.gnome.evolution.dataserver.Source.RemoteCreatable.xml \
	e-dbus-source-org.gnome.evolution.dataserver.Source.RemoteDeletable.xml \
	e-dbus-source-org.gnome.evolution.dataserver.Source.OAuth2Support.xml \
	$(NULL)

GENERATED_DBUS_SOURCE_MANAGER = \
	e-dbus-source-manager.c \
	e-dbus-source-manager.h \
	e-dbus-source-manager-org.gnome.evolution.dataserver.SourceManager.xml \
	$(NULL)

GENERATED_DBUS_AUTHENTICATOR = \
	e-dbus-authenticator.c \
	e-dbus-authenticator.h \
	e-dbus-authenticator-org.gnome.evolution.dataserver.Authenticator.xml \
	$(NULL)

GENERATED_DBUS_USER_PROMPTER = \
	e-dbus-user-prompter.c \
	e-dbus-user-prompter.h \
	e-dbus-user-prompter-org.gnome.evolution.dataserver.UserPrompter.xml \
	$(NULL)

GENERATED_DBUS_ADDRESS_BOOK = \
	e-dbus-address-book.c \
	e-dbus-address-book.h \
	e-dbus-address-book-org.gnome.evolution.dataserver.AddressBook.xml \
	$(NULL)

GENERATED_DBUS_ADDRESS_BOOK_CURSOR = \
	e-dbus-address-book-cursor.c \
	e-dbus-address-book-cursor.h \
	e-dbus-address-book-cursor-org.gnome.evolution.dataserver.AddressBookCursor.xml \
	$(NULL)

GENERATED_DBUS_DIRECT_BOOK = \
	e-dbus-direct-book.c \
	e-dbus-direct-book.h \
	e-dbus-direct-book-org.gnome.evolution.dataserver.DirectBook.xml \
	$(NULL)

GENERATED_DBUS_ADDRESS_BOOK_FACTORY = \
	e-dbus-address-book-factory.c \
	e-dbus-address-book-factory.h \
	e-dbus-address-book-factory-org.gnome.evolution.dataserver.AddressBookFactory.xml \
	$(NULL)

GENERATED_DBUS_CALENDAR = \
	e-dbus-calendar.c \
	e-dbus-calendar.h \
	e-dbus-calendar-org.gnome.evolution.dataserver.Calendar.xml \
	$(NULL)

GENERATED_DBUS_CALENDAR_FACTORY = \
	e-dbus-calendar-factory.c \
	e-dbus-calendar-factory.h \
	e-dbus-calendar-factory-org.gnome.evolution.dataserver.CalendarFactory.xml \
	$(NULL)

GENERATED_DBUS_SUBPROCESS_BACKEND = \
	e-dbus-subprocess-backend.c \
	e-dbus-subprocess-backend.h \
	e-dbus-subprocess-backend-org.gnome.evolution.dataserver.Subprocess.Backend.xml \
	$(NULL)

BUILT_SOURCES = \
	$(GENERATED_DBUS_LOCALE) \
	$(GENERATED_DBUS_SOURCE) \
	$(GENERATED_DBUS_SOURCE_MANAGER) \
	$(GENERATED_DBUS_AUTHENTICATOR) \
	$(GENERATED_DBUS_ADDRESS_BOOK) \
	$(GENERATED_DBUS_ADDRESS_BOOK_CURSOR) \
	$(GENERATED_DBUS_DIRECT_BOOK) \
	$(GENERATED_DBUS_ADDRESS_BOOK_FACTORY) \
	$(GENERATED_DBUS_CALENDAR) \
	$(GENERATED_DBUS_CALENDAR_FACTORY) \
	$(GENERATED_DBUS_USER_PROMPTER) \
	$(GENERATED_DBUS_SUBPROCESS_BACKEND) \
	$(NULL)

privsolib_LTLIBRARIES = libedbus-private.la

# We omit AM_CPPFLAGS -- which is mostly warning flags -- since
# we have no control over the C code generated by gdbus-codegen.
libedbus_private_la_CPPFLAGS = \
	-I$(top_srcdir) \
	$(E_DATA_SERVER_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libedbus_private_la_CFLAGS = \
	-Wno-redundant-decls \
	$(NULL)

libedbus_private_la_SOURCES = \
	$(BUILT_SOURCES) \
	$(NULL)

libedbus_private_la_LIBADD = \
	$(E_DATA_SERVER_LIBS) \
	$(GIO_UNIX_LIBS) \
	$(NULL)

libedbus_private_la_LDFLAGS = \
	-avoid-version \
	$(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

EXTRA_DIST = \
	org.freedesktop.locale1.xml	\
	org.gnome.evolution.dataserver.Source.xml \
	org.gnome.evolution.dataserver.SourceManager.xml \
	org.gnome.evolution.dataserver.Authenticator.xml \
	org.gnome.evolution.dataserver.AddressBook.xml \
	org.gnome.evolution.dataserver.AddressBookCursor.xml \
	org.gnome.evolution.dataserver.DirectBook.xml \
	org.gnome.evolution.dataserver.AddressBookFactory.xml \
	org.gnome.evolution.dataserver.Calendar.xml \
	org.gnome.evolution.dataserver.CalendarFactory.xml \
	org.gnome.evolution.dataserver.UserPrompter.xml \
	org.gnome.evolution.dataserver.Subprocess.Backend.xml \
	$(NULL)

CLEANFILES = \
	$(BUILT_SOURCES) \
	$(NULL)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk
