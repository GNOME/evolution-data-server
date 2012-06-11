noinst_PROGRAMS = 							\
	evolution-source-viewer						\
	test-category-completion					\
	test-source-combo-box						\
	test-source-selector						\
	test-contact-store						\
	test-name-selector

TEST_EDATASERVERUI_CPPFLAGS=						\
	$(AM_CPPFLAGS)							\
	-I$(top_srcdir)							\
	-I$(top_srcdir)/addressbook					\
	-I$(top_builddir)/addressbook					\
	-DLOCALEDIR=\""$(localedir)"\"					\
	-DG_LOG_DOMAIN=\"e-data-server-ui\"				\
	$(CAMEL_CFLAGS)							\
	$(GNOME_KEYRING_CFLAGS)						\
	$(E_DATA_SERVER_UI_CFLAGS)

TEST_EDATASERVERUI_LDFLAGS =						\
	$(top_builddir)/camel/libcamel-1.2.la				\
	$(top_builddir)/calendar/libecal/libecal-1.2.la			\
	$(top_builddir)/addressbook/libebook/libebook-1.2.la		\
	$(top_builddir)/libebackend/libebackend-1.2.la			\
	$(top_builddir)/libedataserver/libedataserver-1.2.la		\
	$(top_builddir)/libedataserverui/libedataserverui-3.0.la	\
	$(CAMEL_LIBS)							\
	$(GNOME_KEYRING_LIBS)						\
	$(E_DATA_SERVER_UI_LIBS)

evolution_source_viewer_CPPFLAGS = $(TEST_EDATASERVERUI_CPPFLAGS)
evolution_source_viewer_SOURCES = evolution-source-viewer.c
evolution_source_viewer_LDADD = $(TEST_EDATASERVERUI_LDFLAGS)

test_category_completion_CPPFLAGS = $(TEST_EDATASERVERUI_CPPFLAGS)
test_category_completion_SOURCES = test-category-completion.c
test_category_completion_LDADD = $(TEST_EDATASERVERUI_LDFLAGS)

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
