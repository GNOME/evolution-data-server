set(SOURCEDIRS
	${CMAKE_SOURCE_DIR}/src/camel
	${CMAKE_SOURCE_DIR}/src/camel/providers/local
	${CMAKE_SOURCE_DIR}/src/camel/providers/imapx
	${CMAKE_SOURCE_DIR}/src/camel/providers/nntp
	${CMAKE_SOURCE_DIR}/src/camel/providers/pop3
	${CMAKE_SOURCE_DIR}/src/camel/providers/sendmail
	${CMAKE_SOURCE_DIR}/src/camel/providers/smtp
)

set(DEPENDENCIES
	camel
	camellocal
	camelimapx
	camelnntp
	camelpop3
	camelsendmail
	camelsmtp
)

set(IGNORE_HEADERS
	camel-charset-map-private.h
	camel-enumtypes.h
	camel-i18n.h
	camel-imapx-tokenise.h
	camel-imapx-utils.h
	camel-local-private.h
	camel-nntp-private.h
	camel-nntp-resp-codes.h
	camel-search-private.h
	camel-win32.h
)

add_gtkdoc(camel camel "CAMEL_DISABLE_DEPRECATED" SOURCEDIRS DEPENDENCIES IGNORE_HEADERS)
