noinst_PROGRAMS = 							\
	test-category-completion					\
	test-client-examine-auth					\
	test-source-combo-box						\
	test-source-selector						\
	test-contact-store						\
	test-name-selector

TEST_EDATASERVERUI_CPPFLAGS=						\
	$(AM_CPPFLAGS)							\
	-I$(top_srcdir)							\
	-I$(top_srcdir)/addressbook					\
	-I$(top_builddir)/addressbook					\
	-DG_LOG_DOMAIN=\"e-data-server-ui\"				\
	$(CAMEL_CFLAGS)							\
	$(GNOME_KEYRING_CFLAGS)						\
	$(E_DATA_SERVER_UI_CFLAGS)

TEST_EDATASERVERUI_LDFLAGS =						\
	$(top_builddir)/camel/libcamel-1.2.la				\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la		\
	$(top_builddir)/libedataserver/libedataserver-1.2.la		\
	$(top_builddir)/libedataserverui/libedataserverui-3.0.la	\
	$(GNOME_KEYRING_LIBS)						\
	$(E_DATA_SERVER_UI_LIBS)

test_category_completion_CPPFLAGS = $(TEST_EDATASERVERUI_CPPFLAGS)
test_category_completion_SOURCES = test-category-completion.c
test_category_completion_LDADD = $(TEST_EDATASERVERUI_LDFLAGS)

test_client_examine_auth_CPPFLAGS = $(TEST_EDATASERVERUI_CPPFLAGS)
test_client_examine_auth_SOURCES = test-client-examine-auth.c
test_client_examine_auth_LDADD = $(TEST_EDATASERVERUI_LDFLAGS)

test_source_selector_CPPFLAGS = $(TEST_EDATASERVERUI_CPPFLAGS)
test_source_selector_SOURCES = test-source-selector.c
test_source_selector_LDADD =  $(TEST_EDATASERVERUI_LDFLAGS)

test_source_combo_box_CPPFLAGS = $(TEST_EDATASERVERUI_CPPFLAGS)
test_source_combo_box_SOURCES = test-source-combo-box.c
test_source_combo_box_LDADD =  $(TEST_EDATASERVERUI_LDFLAGS)

test_contact_store_CPPFLAGS = $(TEST_EDATASERVERUI_CPPFLAGS)
test_contact_store_SOURCES = test-contact-store.c
test_contact_store_LDADD = $(TEST_EDATASERVERUI_LDFLAGS)

test_name_selector_CPPFLAGS = $(TEST_EDATASERVERUI_CPPFLAGS)
test_name_selector_SOURCES = test-name-selector.c
test_name_selector_LDADD = $(TEST_EDATASERVERUI_LDFLAGS)

-include $(top_srcdir)/git.mk
