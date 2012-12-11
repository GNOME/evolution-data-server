#ifndef CLIENT_TEST_UTILS_H
#define CLIENT_TEST_UTILS_H

#include <libebook/libebook.h>

typedef void (*SourceFunc) (ESource *source);

void report_error (const gchar *operation, GError **error);
void print_email (EContact *contact);
EBookClient *open_system_book (ESourceRegistry *registry, gboolean only_if_exists);

void main_initialize (void);
void start_main_loop (GThreadFunc func, gpointer data);
void start_in_thread_with_main_loop (GThreadFunc func, gpointer data);
void start_in_idle_with_main_loop (GThreadFunc func, gpointer data);
void stop_main_loop (gint stop_result);
gint get_main_loop_stop_result (void);

void foreach_configured_source (ESourceRegistry *registry, SourceFunc func);
gpointer foreach_configured_source_async_start (ESourceRegistry *registry, ESource **source);
gboolean foreach_configured_source_async_next (gpointer *foreach_async_data, ESource **source);

EBookClient *new_temp_client (gchar **uri);
EBookClient *new_custom_temp_client (gchar **uri, SourceFunc on_setup);
gboolean add_contact_from_test_case_verify (EBookClient *book_client, const gchar *case_name, EContact **contact);
gboolean add_contact_verify (EBookClient *book_client, EContact *contact);
gchar *new_vcard_from_test_case (const gchar *case_name);

#endif /* CLIENT_TEST_UTILS_H */
