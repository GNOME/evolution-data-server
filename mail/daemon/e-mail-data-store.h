/* e-mail-data-store.h */

#ifndef _E_MAIL_DATA_STORE_H
#define _E_MAIL_DATA_STORE_H

#include <glib-object.h>
#include <camel/camel.h>

G_BEGIN_DECLS

#define EMAIL_TYPE_DATA_STORE e_mail_data_store_get_type()

#define EMAIL_DATA_STORE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  EMAIL_TYPE_DATA_STORE, EMailDataStore))

#define EMAIL_DATA_STORE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  EMAIL_TYPE_DATA_STORE, EMailDataStoreClass))

#define EMAIL_IS_DATA_STORE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  EMAIL_TYPE_DATA_STORE))

#define EMAIL_IS_DATA_STORE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  EMAIL_TYPE_DATA_STORE))

#define EMAIL_DATA_STORE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  EMAIL_TYPE_DATA_STORE, EMailDataStoreClass))

typedef struct {
  GObject parent;
} EMailDataStore;

typedef struct {
  GObjectClass parent_class;
} EMailDataStoreClass;

GType e_mail_data_store_get_type (void);

EMailDataStore* e_mail_data_store_new (CamelStore *store, const char *url);
guint e_mail_data_store_register_gdbus_object (EMailDataStore *estore, GDBusConnection *connection, const gchar *object_path, GError **error);
const char * e_mail_data_store_get_path (EMailDataStore *estore);
CamelFolder * e_mail_data_store_get_camel_folder (EMailDataStore *estore, const char *path);
const char * e_mail_data_store_get_folder_path (EMailDataStore *estore, GDBusConnection *connection, CamelFolder *folder);


G_END_DECLS

#endif /* _E_MAIL_DATA_STORE_H */
