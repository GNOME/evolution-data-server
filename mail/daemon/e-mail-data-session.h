/* e-mail-data-session.h */

#ifndef _E_MAIL_DATA_SESSION_H
#define _E_MAIL_DATA_SESSION_H

#include <glib-object.h>
#include <gio/gio.h>
#include <camel/camel.h>

G_BEGIN_DECLS

#define EMAIL_TYPE_DATA_SESSION e_mail_data_session_get_type()

#define EMAIL_DATA_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  EMAIL_TYPE_DATA_SESSION, EMailDataSession))

#define EMAIL_DATA_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  EMAIL_TYPE_DATA_SESSION, EMailDataSessionClass))

#define EMAIL_IS_DATA_SESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  EMAIL_TYPE_DATA_SESSION))

#define EMAIL_IS_DATA_SESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  EMAIL_TYPE_DATA_SESSION))

#define EMAIL_DATA_SESSION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  EMAIL_TYPE_DATA_SESSION, EMailDataSessionClass))

typedef struct {
  GObject parent;
} EMailDataSession;

typedef struct {
  GObjectClass parent_class;
} EMailDataSessionClass;

GType e_mail_data_session_get_type (void);

EMailDataSession* e_mail_data_session_new (void);
guint e_mail_data_session_register_gdbus_object (EMailDataSession *msession, GDBusConnection *connection, const gchar *object_path, GError **error);
void e_mail_data_session_release (EMailDataSession *session, GDBusConnection *connection, const char *name);
const char * e_mail_data_session_get_path_from_store (EMailDataSession *msession, gpointer store);
CamelFolder * e_mail_session_get_folder_from_path (EMailDataSession *msession, const char *path);
void e_mail_session_emit_ask_password (EMailDataSession *msession, const char *title, const gchar *prompt, const gchar *key);
void e_mail_session_emit_send_receive_completed (EMailDataSession *msession);


G_END_DECLS

#endif /* _E_MAIL_DATA_SESSION_H */
