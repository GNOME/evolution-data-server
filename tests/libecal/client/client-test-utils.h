#ifndef CLIENT_TEST_UTILS_H
#define CLIENT_TEST_UTILS_H

#include <glib.h>
#include <libecal/e-cal-client.h>

void print_ecomp (ECalComponent *ecalcomp);
void print_icomp (icalcomponent *icalcomp);
void report_error (const gchar *operation, GError **error);

void main_initialize (void);
void start_main_loop (GThreadFunc func, gpointer data);
void start_in_thread_with_main_loop (GThreadFunc func, gpointer data);
void start_in_idle_with_main_loop (GThreadFunc func, gpointer data);
void stop_main_loop (gint stop_result);
gint get_main_loop_stop_result (void);

void foreach_configured_source (ECalClientSourceType source_type, void (*func) (ESource *source, ECalClientSourceType source_type));
gpointer foreach_configured_source_async_start (ECalClientSourceType source_type, ESource **source);
gboolean foreach_configured_source_async_next (gpointer *foreach_async_data, ESource **source);
ECalClientSourceType foreach_configured_source_async_get_source_type (gpointer foreach_async_data);

ECalClient *new_temp_client (ECalClientSourceType source_type, gchar **uri);

#endif /* CLIENT_TEST_UTILS_H */
