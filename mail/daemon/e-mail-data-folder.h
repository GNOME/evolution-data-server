/* e-mail-data-folder.h */

#ifndef _E_MAIL_DATA_FOLDER_H
#define _E_MAIL_DATA_FOLDER_H

#include <glib-object.h>
#include <camel/camel.h>

G_BEGIN_DECLS

#define EMAIL_TYPE_DATA_FOLDER e_mail_data_folder_get_type()

#define EMAIL_DATA_FOLDER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  EMAIL_TYPE_DATA_FOLDER, EMailDataFolder))

#define EMAIL_DATA_FOLDER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  EMAIL_TYPE_DATA_FOLDER, EMailDataFolderClass))

#define EMAIL_IS_DATA_FOLDER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  EMAIL_TYPE_DATA_FOLDER))

#define EMAIL_IS_DATA_FOLDER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  EMAIL_TYPE_DATA_FOLDER))

#define EMAIL_DATA_FOLDER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  EMAIL_TYPE_DATA_FOLDER, EMailDataFolderClass))

typedef struct {
  GObject parent;
} EMailDataFolder;

typedef struct {
  GObjectClass parent_class;
} EMailDataFolderClass;

GType e_mail_data_folder_get_type (void);

EMailDataFolder* e_mail_data_folder_new (CamelFolder *folder);
guint e_mail_data_folder_register_gdbus_object (EMailDataFolder *mfolder, GDBusConnection *connection, const gchar *object_path, GError **error);
const char * e_mail_data_folder_get_path (EMailDataFolder *mfolder);
CamelFolder * e_mail_data_folder_get_camel_folder (EMailDataFolder *efolder);

G_END_DECLS

#endif /* _E_MAIL_DATA_FOLDER_H */
