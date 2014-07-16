NULL =

SUBDIRS = . providers tests

if OS_WIN32
LIBCAMEL_PLATFORM_DEP_SOURCES = \
	camel-win32.c \
	$(NULL)
LOCK_HELPER =
else
LIBCAMEL_PLATFORM_DEP_SOURCES = \
	camel-lock-client.c \
	camel-movemail.c \
	camel-stream-process.c \
	$(NULL)
LOCK_HELPER = camel-lock-helper-1.2
endif

%-$(API_VERSION).pc: %.pc
	 cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_in_files = camel.pc.in
pkgconfig_DATA = $(pkgconfig_in_files:.pc.in=-$(API_VERSION).pc)

libcamelincludedir = $(privincludedir)/camel

camellibexecdir = $(libexecdir)
camellibexec_PROGRAMS =	$(LOCK_HELPER) camel-index-control-1.2

lib_LTLIBRARIES = libcamel-1.2.la

ENUM_TYPES = camel-enums.h

camel-enumtypes.h: $(top_srcdir)/enumtypes.h.template $(ENUM_TYPES)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $(top_srcdir)/enumtypes.h.template \
		--fhead "#ifndef CAMEL_ENUMTYPES_H\n#define CAMEL_ENUMTYPES_H\n" \
		--ftail "#endif /* CAMEL_ENUMTYPES_H */\n" \
		$(addprefix $(srcdir)/,$(ENUM_TYPES)) > $@

camel-enumtypes.c: $(top_srcdir)/enumtypes.c.template $(ENUM_TYPES)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $(top_srcdir)/enumtypes.c.template \
		--fhead "#include \"camel-enumtypes.h\"" \
		$(addprefix $(srcdir)/,$(ENUM_TYPES)) > $@

ENUM_GENERATED = camel-enumtypes.h camel-enumtypes.c

libcamel_1_2_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(srcdir) \
	-DCAMEL_LIBEXECDIR=\""$(camellibexecdir)"\" \
	-DCAMEL_PROVIDERDIR=\""$(camel_providerdir)"\" \
	-DG_LOG_DOMAIN=\"camel\" \
	-DE_DATA_SERVER_PREFIX=\"$(prefix)\" \
	-DLOCALEDIR=\""$(localedir)"\" \
	-DCAMEL_COMPILATION \
	$(CAMEL_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

libcamel_1_2_la_SOURCES = \
	$(ENUM_GENERATED) \
	camel-address.c \
	camel-async-closure.c \
	camel-block-file.c \
	camel-certdb.c \
	camel-charset-map.c \
	camel-cipher-context.c \
	camel-data-cache.c \
	camel-data-wrapper.c \
	camel-db.c \
	camel-debug.c \
	camel-file-utils.c \
	camel-filter-driver.c \
	camel-filter-input-stream.c \
	camel-filter-output-stream.c \
	camel-filter-search.c \
	camel-folder-search.c \
	camel-folder-summary.c \
	camel-folder-thread.c \
	camel-folder.c \
	camel-gpg-context.c \
	camel-html-parser.c \
	camel-iconv.c \
	camel-index.c \
	camel-internet-address.c \
	camel-junk-filter.c \
	camel-local-settings.c \
	camel-lock.c \
	camel-medium.c \
	camel-memchunk.c \
	camel-mempool.c \
	camel-mime-filter-basic.c \
	camel-mime-filter-bestenc.c \
	camel-mime-filter-canon.c \
	camel-mime-filter-charset.c \
	camel-mime-filter-crlf.c \
	camel-mime-filter-enriched.c \
	camel-mime-filter-from.c \
	camel-mime-filter-gzip.c \
	camel-mime-filter-html.c \
	camel-mime-filter-index.c \
	camel-mime-filter-linewrap.c \
	camel-mime-filter-pgp.c \
	camel-mime-filter-progress.c \
	camel-mime-filter-tohtml.c \
	camel-mime-filter-windows.c \
	camel-mime-filter-yenc.c \
	camel-mime-filter.c \
	camel-mime-message.c \
	camel-mime-parser.c \
	camel-mime-part-utils.c \
	camel-mime-part.c \
	camel-mime-tables.c \
	camel-mime-utils.c \
	camel-msgport.c \
	camel-multipart-encrypted.c \
	camel-multipart-signed.c \
	camel-multipart.c \
	camel-net-utils.c \
	camel-network-service.c \
	camel-network-settings.c \
	camel-nntp-address.c \
	camel-null-output-stream.c \
	camel-object-bag.c \
	camel-object.c \
	camel-offline-folder.c \
	camel-offline-settings.c \
	camel-offline-store.c \
	camel-operation.c \
	camel-partition-table.c \
	camel-provider.c \
	camel-sasl-anonymous.c \
	camel-sasl-cram-md5.c \
	camel-sasl-digest-md5.c \
	camel-sasl-gssapi.c \
	camel-sasl-login.c \
	camel-sasl-ntlm.c \
	camel-sasl-plain.c \
	camel-sasl-popb4smtp.c \
	camel-sasl.c \
	camel-search-private.c \
	camel-search-sql-sexp.c \
	camel-service.c \
	camel-session.c \
	camel-settings.c \
	camel-sexp.c \
	camel-smime-context.c \
	camel-store-settings.c \
	camel-store-summary.c \
	camel-store.c \
	camel-stream-buffer.c \
	camel-stream-filter.c \
	camel-stream-fs.c \
	camel-stream-mem.c \
	camel-stream-null.c \
	camel-stream.c \
	camel-string-utils.c \
	camel-subscribable.c \
	camel-text-index.c \
	camel-transport.c \
	camel-trie.c \
	camel-uid-cache.c \
	camel-url-scanner.c \
	camel-url.c \
	camel-utf8.c \
	camel-vee-data-cache.c \
	camel-vee-folder.c \
	camel-vee-store.c \
	camel-vee-summary.c \
	camel-vtrash-folder.c \
	camel.c \
	$(LIBCAMEL_PLATFORM_DEP_SOURCES) \
	$(NULL)

libcamelinclude_HEADERS = \
	camel-address.h \
	camel-async-closure.h \
	camel-block-file.h \
	camel-certdb.h \
	camel-charset-map.h \
	camel-cipher-context.h \
	camel-data-cache.h \
	camel-data-wrapper.h \
	camel-db.h \
	camel-debug.h \
	camel-enums.h \
	camel-enumtypes.h \
	camel-file-utils.h \
	camel-filter-driver.h \
	camel-filter-input-stream.h \
	camel-filter-output-stream.h \
	camel-filter-search.h \
	camel-folder-search.h \
	camel-folder-summary.h \
	camel-folder-thread.h \
	camel-folder.h \
	camel-gpg-context.h \
	camel-html-parser.h \
	camel-iconv.h \
	camel-index.h \
	camel-internet-address.h \
	camel-junk-filter.h \
	camel-local-settings.h \
	camel-lock-client.h \
	camel-lock-helper.h \
	camel-lock.h \
	camel-medium.h \
	camel-memchunk.h \
	camel-mempool.h \
	camel-mime-filter-basic.h \
	camel-mime-filter-bestenc.h \
	camel-mime-filter-canon.h \
	camel-mime-filter-charset.h \
	camel-mime-filter-crlf.h \
	camel-mime-filter-enriched.h \
	camel-mime-filter-from.h \
	camel-mime-filter-gzip.h \
	camel-mime-filter-html.h \
	camel-mime-filter-index.h \
	camel-mime-filter-linewrap.h \
	camel-mime-filter-pgp.h \
	camel-mime-filter-progress.h \
	camel-mime-filter-tohtml.h \
	camel-mime-filter-windows.h \
	camel-mime-filter-yenc.h \
	camel-mime-filter.h \
	camel-mime-message.h \
	camel-mime-parser.h \
	camel-mime-part-utils.h \
	camel-mime-part.h \
	camel-mime-utils.h \
	camel-movemail.h \
	camel-msgport.h \
	camel-multipart-encrypted.h \
	camel-multipart-signed.h \
	camel-multipart.h \
	camel-net-utils.h \
	camel-network-service.h \
	camel-network-settings.h \
	camel-nntp-address.h \
	camel-null-output-stream.h \
	camel-object-bag.h \
	camel-object.h \
	camel-offline-folder.h \
	camel-offline-settings.h \
	camel-offline-store.h \
	camel-operation.h \
	camel-partition-table.h \
	camel-provider.h \
	camel-sasl-anonymous.h \
	camel-sasl-cram-md5.h \
	camel-sasl-digest-md5.h \
	camel-sasl-gssapi.h \
	camel-sasl-login.h \
	camel-sasl-ntlm.h \
	camel-sasl-plain.h \
	camel-sasl-popb4smtp.h \
	camel-sasl.h \
	camel-search-private.h \
	camel-search-sql-sexp.h \
	camel-service.h \
	camel-session.h \
	camel-settings.h \
	camel-sexp.h \
	camel-smime-context.h \
	camel-store-settings.h \
	camel-store-summary.h \
	camel-store.h \
	camel-stream-buffer.h \
	camel-stream-filter.h \
	camel-stream-fs.h \
	camel-stream-mem.h \
	camel-stream-null.h \
	camel-stream-process.h \
	camel-stream.h \
	camel-string-utils.h \
	camel-subscribable.h \
	camel-text-index.h \
	camel-transport.h \
	camel-trie.h \
	camel-uid-cache.h \
	camel-url-scanner.h \
	camel-url.h \
	camel-utf8.h \
	camel-vee-data-cache.h \
	camel-vee-folder.h \
	camel-vee-store.h \
	camel-vee-summary.h \
	camel-vtrash-folder.h \
	camel.h \
	$(NULL)

libcamel_1_2_la_LDFLAGS = -version-info $(LIBCAMEL_CURRENT):$(LIBCAMEL_REVISION):$(LIBCAMEL_AGE) $(NO_UNDEFINED) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

libcamel_1_2_la_LIBADD = \
	$(CAMEL_LIBS) \
	$(SOCKET_LIBS) \
	$(ICONV_LIBS) \
	$(REGEX_LIBS) \
	$(LIBDWFL_LIBS) \
	$(NULL)

camel_lock_helper_1_2_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(srcdir) \
	-DCAMEL_LIBEXECDIR=\""$(camellibexecdir)"\" \
	-DCAMEL_PROVIDERDIR=\""$(camel_providerdir)"\" \
	-DG_LOG_DOMAIN=\"camel\" \
	-DE_DATA_SERVER_PREFIX=\"$(prefix)\" \
	-DLOCALEDIR=\""$(localedir)"\" \
	-DCAMEL_COMPILATION \
	$(CAMEL_CFLAGS) \
	$(NULL)

camel_lock_helper_1_2_SOURCES = \
	camel-lock.c \
	camel-lock.h \
	camel-lock-helper.c \
	camel-lock-helper.h \
	$(NULL)

# NOTE: This needs to be cleaned up, it shouldn't link with all this crap,
# because it might be setuid.
camel_lock_helper_1_2_LDADD = \
	$(libcamel_1_2_la_LIBADD) \
	$(NULL)

camel_index_control_1_2_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(srcdir) \
	-DCAMEL_LIBEXECDIR=\""$(camellibexecdir)"\" \
	-DCAMEL_PROVIDERDIR=\""$(camel_providerdir)"\" \
	-DG_LOG_DOMAIN=\"camel\" \
	-DE_DATA_SERVER_PREFIX=\"$(prefix)\" \
	-DLOCALEDIR=\""$(localedir)"\" \
	-DCAMEL_COMPILATION \
	$(CAMEL_CFLAGS) \
	$(NULL)

camel_index_control_1_2_SOURCES = \
	camel-index-control.c \
	$(NULL)

camel_index_control_1_2_LDADD = \
	libcamel-1.2.la \
	$(libcamel_1_2_la_LIBADD) \
	$(NULL)

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

noinst_HEADERS = \
	camel-charset-map-private.h \
	camel-win32.h \
	$(NULL)

BUILT_SOURCES = \
	camel-mime-tables.c \
	$(ENUM_GENERATED) \
	$(NULL)

EXTRA_DIST = \
	$(pkgconfig_in_files) \
	gentables.pl \
	README \
	$(NULL)

CLEANFILES = $(BUILT_SOURCES)
DISTCLEANFILES = $(pkgconfig_DATA)

dist-hook:
	cd $(distdir); rm -f $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk
