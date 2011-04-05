/* e-dbus-manager.h */

#ifndef _E_DBUS_MANAGER_H
#define _E_DBUS_MANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define EDBUS_TYPE_MANAGER e_dbus_manager_get_type()

#define EDBUS_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  EDBUS_TYPE_MANAGER, EDBusManager))

#define EDBUS_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  EDBUS_TYPE_MANAGER, EDBusManagerClass))

#define EDBUS_IS_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  EDBUS_TYPE_MANAGER))

#define EDBUS_IS_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  EDBUS_TYPE_MANAGER))

#define EDBUS_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  EDBUS_TYPE_MANAGER, EDBusManagerClass))

typedef struct {
  GObject parent;
} EDBusManager;

typedef struct {
  GObjectClass parent_class;
} EDBusManagerClass;

GType e_dbus_manager_get_type (void);

EDBusManager* e_dbus_manager_new (void);

G_END_DECLS

#endif /* _E_DBUS_MANAGER_H */
