
set(GENERATED_DBUS_LOCALE
	e-dbus-localed.c
	e-dbus-localed.h
)

gdbus_codegen(org.freedesktop.locale1.xml
	org.freedesktop.
	E_DBus
	e-dbus-localed
	GENERATED_DBUS_LOCALE)

set(GENERATED_DBUS_SOURCE
	e-dbus-source.c
	e-dbus-source.h
	e-dbus-source-org.gnome.evolution.dataserver.Source.xml
	e-dbus-source-org.gnome.evolution.dataserver.Source.Removable.xml
	e-dbus-source-org.gnome.evolution.dataserver.Source.Writable.xml
	e-dbus-source-org.gnome.evolution.dataserver.Source.RemoteCreatable.xml
	e-dbus-source-org.gnome.evolution.dataserver.Source.RemoteDeletable.xml
	e-dbus-source-org.gnome.evolution.dataserver.Source.OAuth2Support.xml
)

gdbus_codegen_custom(org.gnome.evolution.dataserver.Source.xml
	org.gnome.evolution.dataserver.
	E_DBus
	e-dbus-source
	GENERATED_DBUS_SOURCE
	--c-generate-object-manager)

set(GENERATED_DBUS_SOURCE_MANAGER
	e-dbus-source-manager.c
	e-dbus-source-manager.h
	e-dbus-source-manager-org.gnome.evolution.dataserver.SourceManager.xml
)

gdbus_codegen(org.gnome.evolution.dataserver.SourceManager.xml
	org.gnome.evolution.dataserver.
	E_DBus
	e-dbus-source-manager
	GENERATED_DBUS_SOURCE_MANAGER)

set(GENERATED_DBUS_AUTHENTICATOR
	e-dbus-authenticator.c
	e-dbus-authenticator.h
	e-dbus-authenticator-org.gnome.evolution.dataserver.Authenticator.xml
)

gdbus_codegen(org.gnome.evolution.dataserver.Authenticator.xml
	org.gnome.evolution.dataserver.
	E_DBus
	e-dbus-authenticator
	GENERATED_DBUS_AUTHENTICATOR)

set(GENERATED_DBUS_USER_PROMPTER
	e-dbus-user-prompter.c
	e-dbus-user-prompter.h
	e-dbus-user-prompter-org.gnome.evolution.dataserver.UserPrompter.xml
)

gdbus_codegen(org.gnome.evolution.dataserver.UserPrompter.xml
	org.gnome.evolution.dataserver.
	E_DBus
	e-dbus-user-prompter
	GENERATED_DBUS_USER_PROMPTER)

set(GENERATED_DBUS_ADDRESS_BOOK
	e-dbus-address-book.c
	e-dbus-address-book.h
	e-dbus-address-book-org.gnome.evolution.dataserver.AddressBook.xml
)

gdbus_codegen(org.gnome.evolution.dataserver.AddressBook.xml
	org.gnome.evolution.dataserver.
	E_DBus
	e-dbus-address-book
	GENERATED_DBUS_ADDRESS_BOOK)

set(GENERATED_DBUS_ADDRESS_BOOK_CURSOR
	e-dbus-address-book-cursor.c
	e-dbus-address-book-cursor.h
	e-dbus-address-book-cursor-org.gnome.evolution.dataserver.AddressBookCursor.xml
)

gdbus_codegen(org.gnome.evolution.dataserver.AddressBookCursor.xml
	org.gnome.evolution.dataserver.
	E_DBus
	e-dbus-address-book-cursor
	GENERATED_DBUS_ADDRESS_BOOK_CURSOR)

set(GENERATED_DBUS_ADDRESS_BOOK_VIEW
	e-dbus-address-book-view.c
	e-dbus-address-book-view.h
	e-dbus-address-book-view-org.gnome.evolution.dataserver.AddressBookView.xml
)

gdbus_codegen(org.gnome.evolution.dataserver.AddressBookView.xml
	org.gnome.evolution.dataserver.
	E_DBus
	e-dbus-address-book-view
	GENERATED_DBUS_ADDRESS_BOOK_VIEW)

set(GENERATED_DBUS_DIRECT_BOOK
	e-dbus-direct-book.c
	e-dbus-direct-book.h
	e-dbus-direct-book-org.gnome.evolution.dataserver.DirectBook.xml
)

gdbus_codegen(org.gnome.evolution.dataserver.DirectBook.xml
	org.gnome.evolution.dataserver.
	E_DBus
	e-dbus-direct-book
	GENERATED_DBUS_DIRECT_BOOK)

set(GENERATED_DBUS_ADDRESS_BOOK_FACTORY
	e-dbus-address-book-factory.c
	e-dbus-address-book-factory.h
	e-dbus-address-book-factory-org.gnome.evolution.dataserver.AddressBookFactory.xml
)

gdbus_codegen(org.gnome.evolution.dataserver.AddressBookFactory.xml
	org.gnome.evolution.dataserver.
	E_DBus
	e-dbus-address-book-factory
	GENERATED_DBUS_ADDRESS_BOOK_FACTORY)

set(GENERATED_DBUS_CALENDAR
	e-dbus-calendar.c
	e-dbus-calendar.h
	e-dbus-calendar-org.gnome.evolution.dataserver.Calendar.xml
)

gdbus_codegen(org.gnome.evolution.dataserver.Calendar.xml
	org.gnome.evolution.dataserver.
	E_DBus
	e-dbus-calendar
	GENERATED_DBUS_CALENDAR)

set(GENERATED_DBUS_CALENDAR_FACTORY
	e-dbus-calendar-factory.c
	e-dbus-calendar-factory.h
	e-dbus-calendar-factory-org.gnome.evolution.dataserver.CalendarFactory.xml
)

gdbus_codegen(org.gnome.evolution.dataserver.CalendarFactory.xml
	org.gnome.evolution.dataserver.
	E_DBus
	e-dbus-calendar-factory
	GENERATED_DBUS_CALENDAR_FACTORY)

set(GENERATED_DBUS_CALENDAR_VIEW
	e-dbus-calendar-view.c
	e-dbus-calendar-view.h
	e-dbus-calendar-view-org.gnome.evolution.dataserver.CalendarView.xml
)

gdbus_codegen(org.gnome.evolution.dataserver.CalendarView.xml
	org.gnome.evolution.dataserver.
	E_DBus
	e-dbus-calendar-view
	GENERATED_DBUS_CALENDAR_VIEW)

set(GENERATED_DBUS_SUBPROCESS_BACKEND
	e-dbus-subprocess-backend.c
	e-dbus-subprocess-backend.h
	e-dbus-subprocess-backend-org.gnome.evolution.dataserver.Subprocess.Backend.xml
)

gdbus_codegen_custom(org.gnome.evolution.dataserver.Subprocess.Backend.xml
	org.gnome.evolution.dataserver.Subprocess
	E_DBus_Subprocess
	e-dbus-subprocess-backend
	GENERATED_DBUS_SUBPROCESS_BACKEND
	--c-generate-object-manager)

add_library(edbus-private SHARED
	${GENERATED_DBUS_LOCALE}
	${GENERATED_DBUS_SOURCE}
	${GENERATED_DBUS_SOURCE_MANAGER}
	${GENERATED_DBUS_AUTHENTICATOR}
	${GENERATED_DBUS_ADDRESS_BOOK}
	${GENERATED_DBUS_ADDRESS_BOOK_CURSOR}
	${GENERATED_DBUS_ADDRESS_BOOK_VIEW}
	${GENERATED_DBUS_DIRECT_BOOK}
	${GENERATED_DBUS_ADDRESS_BOOK_FACTORY}
	${GENERATED_DBUS_CALENDAR}
	${GENERATED_DBUS_CALENDAR_FACTORY}
	${GENERATED_DBUS_CALENDAR_VIEW}
	${GENERATED_DBUS_USER_PROMPTER}
	${GENERATED_DBUS_SUBPROCESS_BACKEND}
)

set_target_properties(edbus-private PROPERTIES
	INSTALL_RPATH ""
)

target_compile_definitions(edbus-private PRIVATE
	-DG_LOG_DOMAIN=\"libedbus-private\"
)

target_compile_options(edbus-private PUBLIC
	${DATA_SERVER_CFLAGS}
	${GIO_UNIX_CFLAGS}
)

target_include_directories(edbus-private PUBLIC
	${CMAKE_BINARY_DIR}
	${CMAKE_BINARY_DIR}/src
	${DATA_SERVER_INCLUDE_DIRS}
	${GIO_UNIX_INCLUDE_DIRS}
)

target_link_libraries(edbus-private
	${DATA_SERVER_LDFLAGS}
	${GIO_UNIX_LDFLAGS}
)

install(TARGETS edbus-private
	DESTINATION ${privsolibdir}
)
