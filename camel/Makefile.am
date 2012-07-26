SUBDIRS = . providers tests

if OS_WIN32
LIBCAMEL_PLATFORM_DEP_SOURCES = \
	camel-win32.c
LOCK_HELPER =
else
LIBCAMEL_PLATFORM_DEP_SOURCES = \
	camel-lock-client.c \
	camel-movemail.c \
	camel-stream-process.c
LOCK_HELPER = camel-lock-helper-1.2
endif

MARSHAL_GENERATED = camel-marshal.c camel-marshal.h
@EVO_MARSHAL_RULE@

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_in_files = camel.pc.in
pkgconfig_DATA = $(pkgconfig_in_files:.pc.in=-$(API_VERSION).pc)

camel-imapx-tokenise.h: camel-imapx-tokens.txt
	@GPERF@ -H imapx_hash -N imapx_tokenise_struct -L ANSI-C -o -t -k1,$$ $< --output-file=$@

libcamelincludedir = $(privincludedir)/camel

camellibexecdir = $(libexecdir)
camellibexec_PROGRAMS =	$(LOCK_HELPER) camel-index-control-1.2

lib_LTLIBRARIES = libcamel-1.2.la

ENUM_TYPES = camel-enums.h

camel-enumtypes.h: camel-enumtypes.h.template $(ENUM_TYPES)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template camel-enumtypes.h.template $(ENUM_TYPES)) > $@

camel-enumtypes.c: camel-enumtypes.c.template $(ENUM_TYPES)
	$(AM_V_GEN) (cd $(srcdir) && $(GLIB_MKENUMS) --template camel-enumtypes.c.template $(ENUM_TYPES)) > $@

ENUM_GENERATED = camel-enumtypes.h camel-enumtypes.c camel-imapx-tokenise.h

libcamel_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS)						\
	-I$(top_srcdir) 					\
	-I$(top_builddir)					\
	-I$(srcdir)						\
	-DCAMEL_LIBEXECDIR=\""$(camellibexecdir)"\"		\
	-DCAMEL_PROVIDERDIR=\""$(camel_providerdir)"\"		\
	-DG_LOG_DOMAIN=\"camel\"				\
	-DE_DATA_SERVER_PREFIX=\"$(prefix)\"			\
	-DLOCALEDIR=\""$(localedir)"\"				\
	-DCAMEL_COMPILATION					\
	$(CAMEL_CFLAGS)						\
	$(CODE_COVERAGE_CFLAGS)					\
	$(NULL)

libcamel_1_2_la_SOURCES = 			\
	$(ENUM_GENERATED)			\
	$(MARSHAL_GENERATED)			\
	camel-address.c				\
	camel-block-file.c			\
	camel-certdb.c				\
	camel-charset-map.c			\
	camel-cipher-context.c			\
	camel-data-cache.c			\
	camel-data-wrapper.c			\
	camel-db.c				\
	camel-debug.c				\
	camel-disco-diary.c			\
	camel-disco-folder.c			\
	camel-disco-store.c			\
	camel-file-utils.c			\
	camel-filter-driver.c			\
	camel-filter-search.c			\
	camel-folder-search.c			\
	camel-folder-summary.c			\
	camel-folder-thread.c			\
	camel-folder.c				\
	camel-gpg-context.c			\
	camel-html-parser.c			\
	camel-iconv.c				\
	camel-index.c				\
	camel-internet-address.c		\
	camel-junk-filter.c			\
	camel-local-settings.c			\
	camel-lock.c				\
	camel-medium.c				\
	camel-memchunk.c			\
	camel-mempool.c				\
	camel-mime-filter-basic.c		\
	camel-mime-filter-bestenc.c		\
	camel-mime-filter-canon.c		\
	camel-mime-filter-charset.c		\
	camel-mime-filter-crlf.c		\
	camel-mime-filter-enriched.c		\
	camel-mime-filter-from.c		\
	camel-mime-filter-gzip.c		\
	camel-mime-filter-html.c		\
	camel-mime-filter-index.c		\
	camel-mime-filter-linewrap.c		\
	camel-mime-filter-pgp.c			\
	camel-mime-filter-progress.c		\
	camel-mime-filter-save.c		\
	camel-mime-filter-tohtml.c		\
	camel-mime-filter-windows.c		\
	camel-mime-filter-yenc.c		\
	camel-mime-filter.c			\
	camel-mime-message.c			\
	camel-mime-parser.c			\
	camel-mime-part-utils.c			\
	camel-mime-part.c			\
	camel-mime-tables.c			\
	camel-mime-utils.c			\
	camel-msgport.c				\
	camel-multipart-encrypted.c		\
	camel-multipart-signed.c		\
	camel-multipart.c			\
	camel-net-utils.c			\
	camel-network-service.c			\
	camel-network-settings.c		\
	camel-nntp-address.c			\
	camel-object-bag.c			\
	camel-object.c				\
	camel-offline-folder.c			\
	camel-offline-journal.c			\
	camel-offline-settings.c		\
	camel-offline-store.c			\
	camel-operation.c			\
	camel-partition-table.c			\
	camel-provider.c			\
	camel-sasl-anonymous.c			\
	camel-sasl-cram-md5.c			\
	camel-sasl-digest-md5.c			\
	camel-sasl-gssapi.c			\
	camel-sasl-login.c			\
	camel-sasl-ntlm.c			\
	camel-sasl-plain.c			\
	camel-sasl-popb4smtp.c			\
	camel-sasl.c				\
	camel-search-private.c			\
	camel-search-sql-sexp.c			\
	camel-service.c				\
	camel-session.c				\
	camel-settings.c			\
	camel-sexp.c				\
	camel-smime-context.c			\
	camel-store-settings.c			\
	camel-store-summary.c			\
	camel-store.c				\
	camel-stream-buffer.c			\
	camel-stream-filter.c			\
	camel-stream-fs.c			\
	camel-stream-mem.c			\
	camel-stream-null.c			\
	camel-stream-vfs.c			\
	camel-stream.c				\
	camel-string-utils.c			\
	camel-subscribable.c			\
	camel-tcp-stream-raw.c			\
	camel-tcp-stream-ssl.c			\
	camel-tcp-stream.c			\
	camel-text-index.c			\
	camel-transport.c			\
	camel-trie.c				\
	camel-uid-cache.c			\
	camel-url-scanner.c			\
	camel-url.c				\
	camel-utf8.c				\
	camel-vee-data-cache.c			\
	camel-vee-folder.c			\
	camel-vee-store.c			\
	camel-vee-summary.c			\
	camel-vtrash-folder.c			\
	camel-imapx-command.c			\
	camel-imapx-job.c			\
	camel-imapx-settings.c			\
	camel-imapx-server.c			\
	camel-imapx-conn-manager.c		\
	camel-imapx-folder.c			\
	camel-imapx-store-summary.c		\
	camel-imapx-store.c			\
	camel-imapx-stream.c			\
	camel-imapx-summary.c			\
	camel-imapx-utils.c			\
	camel.c					\
	$(LIBCAMEL_PLATFORM_DEP_SOURCES)

libcamelinclude_HEADERS =			\
	camel-address.h				\
	camel-block-file.h			\
	camel-certdb.h				\
	camel-charset-map.h			\
	camel-cipher-context.h			\
	camel-data-cache.h			\
	camel-data-wrapper.h			\
	camel-db.h				\
	camel-debug.h				\
	camel-disco-diary.h			\
	camel-disco-folder.h			\
	camel-disco-store.h			\
	camel-enums.h				\
	camel-enumtypes.h			\
	camel-file-utils.h			\
	camel-filter-driver.h			\
	camel-filter-search.h			\
	camel-folder-search.h			\
	camel-folder-summary.h			\
	camel-folder-thread.h			\
	camel-folder.h				\
	camel-gpg-context.h			\
	camel-html-parser.h			\
	camel-iconv.h				\
	camel-index.h				\
	camel-internet-address.h		\
	camel-junk-filter.h			\
	camel-local-settings.h			\
	camel-lock-client.h			\
	camel-lock-helper.h			\
	camel-lock.h				\
	camel-medium.h				\
	camel-memchunk.h			\
	camel-mempool.h				\
	camel-mime-filter-basic.h		\
	camel-mime-filter-bestenc.h		\
	camel-mime-filter-canon.h		\
	camel-mime-filter-charset.h		\
	camel-mime-filter-crlf.h		\
	camel-mime-filter-enriched.h		\
	camel-mime-filter-from.h		\
	camel-mime-filter-gzip.h		\
	camel-mime-filter-html.h		\
	camel-mime-filter-index.h		\
	camel-mime-filter-linewrap.h		\
	camel-mime-filter-pgp.h			\
	camel-mime-filter-progress.h		\
	camel-mime-filter-save.h		\
	camel-mime-filter-tohtml.h		\
	camel-mime-filter-windows.h		\
	camel-mime-filter-yenc.h		\
	camel-mime-filter.h			\
	camel-mime-message.h			\
	camel-mime-parser.h			\
	camel-mime-part-utils.h			\
	camel-mime-part.h			\
	camel-mime-utils.h			\
	camel-movemail.h			\
	camel-msgport.h				\
	camel-multipart-encrypted.h		\
	camel-multipart-signed.h		\
	camel-multipart.h			\
	camel-net-utils.h			\
	camel-network-service.h			\
	camel-network-settings.h		\
	camel-nntp-address.h			\
	camel-object-bag.h			\
	camel-object.h				\
	camel-offline-folder.h			\
	camel-offline-journal.h			\
	camel-offline-settings.h		\
	camel-offline-store.h			\
	camel-operation.h			\
	camel-partition-table.h			\
	camel-provider.h			\
	camel-sasl-anonymous.h			\
	camel-sasl-cram-md5.h			\
	camel-sasl-digest-md5.h			\
	camel-sasl-gssapi.h			\
	camel-sasl-login.h			\
	camel-sasl-ntlm.h			\
	camel-sasl-plain.h			\
	camel-sasl-popb4smtp.h			\
	camel-sasl.h				\
	camel-search-private.h			\
	camel-search-sql-sexp.h			\
	camel-service.h				\
	camel-session.h				\
	camel-settings.h			\
	camel-sexp.h				\
	camel-smime-context.h			\
	camel-store-settings.h			\
	camel-store-summary.h			\
	camel-store.h				\
	camel-stream-buffer.h			\
	camel-stream-filter.h			\
	camel-stream-fs.h			\
	camel-stream-mem.h			\
	camel-stream-null.h			\
	camel-stream-process.h			\
	camel-stream-vfs.h			\
	camel-stream.h				\
	camel-string-utils.h			\
	camel-subscribable.h			\
	camel-tcp-stream-raw.h			\
	camel-tcp-stream-ssl.h			\
	camel-tcp-stream.h			\
	camel-text-index.h			\
	camel-transport.h			\
	camel-trie.h				\
	camel-uid-cache.h			\
	camel-url-scanner.h			\
	camel-url.h				\
	camel-utf8.h				\
	camel-vee-data-cache.h			\
	camel-vee-folder.h			\
	camel-vee-store.h			\
	camel-vee-summary.h			\
	camel-vtrash-folder.h			\
	camel-imapx-command.h			\
	camel-imapx-conn-manager.h		\
	camel-imapx-folder.h			\
	camel-imapx-job.h			\
	camel-imapx-settings.h			\
	camel-imapx-server.h			\
	camel-imapx-store-summary.h		\
	camel-imapx-store.h			\
	camel-imapx-stream.h			\
	camel-imapx-summary.h			\
	camel-imapx-utils.h			\
	camel.h

libcamel_1_2_la_LDFLAGS = -version-info $(LIBCAMEL_CURRENT):$(LIBCAMEL_REVISION):$(LIBCAMEL_AGE) $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

libcamel_1_2_la_LIBADD =				\
	$(CAMEL_LIBS)					\
	$(SOCKET_LIBS)					\
	$(ICONV_LIBS)					\
	$(REGEX_LIBS)					\
	$(LIBDWFL_LIBS)

camel_lock_helper_1_2_CPPFLAGS = \
	$(AM_CPPFLAGS)						\
	-I$(top_srcdir) 					\
	-I$(top_builddir)					\
	-I$(srcdir)						\
	-DCAMEL_LIBEXECDIR=\""$(camellibexecdir)"\"		\
	-DCAMEL_PROVIDERDIR=\""$(camel_providerdir)"\"		\
	-DG_LOG_DOMAIN=\"camel\"				\
	-DE_DATA_SERVER_PREFIX=\"$(prefix)\"			\
	-DLOCALEDIR=\""$(localedir)"\"				\
	-DCAMEL_COMPILATION					\
	$(CAMEL_CFLAGS)

camel_lock_helper_1_2_SOURCES = \
	camel-lock.c					\
	camel-lock.h					\
	camel-lock-helper.c 			\
	camel-lock-helper.h

# NOTE: This needs to be cleaned up, it shouldn't link with all this crap,
# because it might be setuid.
camel_lock_helper_1_2_LDADD = \
	$(libcamel_1_2_la_LIBADD)

camel_index_control_1_2_CPPFLAGS = \
	$(AM_CPPFLAGS)						\
	-I$(top_srcdir) 					\
	-I$(top_builddir)					\
	-I$(srcdir)						\
	-DCAMEL_LIBEXECDIR=\""$(camellibexecdir)"\"		\
	-DCAMEL_PROVIDERDIR=\""$(camel_providerdir)"\"		\
	-DG_LOG_DOMAIN=\"camel\"				\
	-DE_DATA_SERVER_PREFIX=\"$(prefix)\"			\
	-DLOCALEDIR=\""$(localedir)"\"				\
	-DCAMEL_COMPILATION					\
	$(CAMEL_CFLAGS)

camel_index_control_1_2_SOURCES = \
	camel-index-control.c

camel_index_control_1_2_LDADD = \
	libcamel-1.2.la				\
	$(libcamel_1_2_la_LIBADD)

camel-mime-tables.c: $(srcdir)/gentables.pl
	perl $(srcdir)/gentables.pl > $@

install-exec-hook:
	@if test -n "$(CAMEL_LOCK_HELPER_USER)"; then \
	    if test `whoami` = root ; then \
	 	chown $(CAMEL_LOCK_HELPER_USER) $(DESTDIR)$(camellibexecdir)/camel-lock-helper-$(API_VERSION) ; \
		chmod u+s $(DESTDIR)$(camellibexecdir)/camel-lock-helper-$(API_VERSION) ; \
	    else \
	    	echo '*** WARNING ***' ; \
		    echo "Camel will not be able to open mbox files until you perform the following steps:" ; \
		    echo "    1. Become root" ; \
		    echo "    2. chown $(CAMEL_LOCK_HELPER_USER) $(DESTDIR)$(sbindir)/camel-lock-helper" ; \
		    echo "    3. chmod u+s $(DESTDIR)$(sbindir)/camel-lock-helper" ; \
		    echo '*** WARNING ***' ; \
	    fi \
	else \
	    echo "No user to chown to"; \
	fi
	@if test -n "$(CAMEL_LOCK_HELPER_GROUP)"; then \
	    if test `whoami` = root ; then \
	    	chgrp $(CAMEL_LOCK_HELPER_GROUP) $(DESTDIR)$(camellibexecdir)/camel-lock-helper-$(API_VERSION) ; \
		    chmod g+s $(DESTDIR)$(camellibexecdir)/camel-lock-helper-$(API_VERSION) ; \
	    else \
	    	echo '*** WARNING ***' ; \
		    echo "Camel will not be able to open mbox files until you perform the following steps:" ; \
		    echo "    1. Become root" ; \
		    echo "    2. chgrp $(CAMEL_LOCK_HELPER_GROUP) $(DESTDIR)$(camellibexecdir)/camel-lock-helper-$(API_VERSION)" ; \
		    echo "    3. chmod g+s $(DESTDIR)$(camellibexecdir)/camel-lock-helper-$(API_VERSION)" ; \
		    echo '*** WARNING ***' ; \
	    fi \
	else \
	    echo "No user to chown to"; \
	fi

noinst_HEADERS =				\
	camel-charset-map-private.h		\
	camel-win32.h				

BUILT_SOURCES =					\
	camel-mime-tables.c			\
	$(ENUM_GENERATED)			\
	$(MARSHAL_GENERATED)

EXTRA_DIST =					\
	$(pkgconfig_in_files)	 		\
	camel-enumtypes.h.template		\
	camel-enumtypes.c.template		\
	camel-imapx-tokens.txt			\
	camel-marshal.list			\
	gentables.pl				\
	README

CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

-include $(top_srcdir)/git.mk
