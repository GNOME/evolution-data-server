/* e-dbus-manager.c */

#include "e-dbus-manager.h"
#include "e-mail-data-session.h"
#include <gio/gio.h>

#define E_MAIL_DATA_FACTORY_SERVICE_NAME \
	"org.gnome.evolution.dataserver.Mail"


G_DEFINE_TYPE (EDBusManager, e_dbus_manager, G_TYPE_OBJECT)

#define MANAGER_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EDBUS_TYPE_MANAGER, EDBusManagerPrivate))

typedef struct _EDBusManagerPrivate EDBusManagerPrivate;

/* This needs to be global ala the CamelSession*/
EMailDataSession *data_session;

struct _EDBusManagerPrivate
{
	guint owner_id;	

	GMutex *books_lock;
	/* A hash of object paths for book URIs to EDataBooks */
	GHashTable *books;

	GMutex *connections_lock;
	/* This is a hash of client addresses to GList* of EDataBooks */
	GHashTable *connections;

	guint exit_timeout;	
};

/* Convenience function to print an error and exit */
G_GNUC_NORETURN static void
die (const gchar *prefix, GError *error)
{
	g_error("%s: %s", prefix, error->message);
	g_error_free (error);
	exit(1);
}

static void
e_dbus_manager_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
e_dbus_manager_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  switch (property_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
e_dbus_manager_dispose (GObject *object)
{
  G_OBJECT_CLASS (e_dbus_manager_parent_class)->dispose (object);
}

static void
e_dbus_manager_finalize (GObject *object)
{
  G_OBJECT_CLASS (e_dbus_manager_parent_class)->finalize (object);
}

static void
e_dbus_manager_class_init (EDBusManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (EDBusManagerPrivate));

  object_class->get_property = e_dbus_manager_get_property;
  object_class->set_property = e_dbus_manager_set_property;
  object_class->dispose = e_dbus_manager_dispose;
  object_class->finalize = e_dbus_manager_finalize;
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
	EDBusManager *manager = user_data;
	EDBusManagerPrivate *priv = MANAGER_PRIVATE(manager);
	guint registration_id;
	GError *error = NULL;

	registration_id = e_mail_data_session_register_gdbus_object (
		data_session,
		connection,
		"/org/gnome/evolution/dataserver/Mail/Session",
		&error);

	if (error)
		die ("Failed to register a Mail Session object", error);

	g_assert (registration_id > 0);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
	EDBusManager *manager = user_data;
	EDBusManagerPrivate *priv = MANAGER_PRIVATE(manager);

	e_mail_data_session_release (data_session, connection, name);
#if 0
	g_mutex_lock (factory->priv->connections_lock);
	while (g_hash_table_lookup_extended (
		factory->priv->connections, name,
		(gpointer) &key, (gpointer) &list)) {
		GList *copy = g_list_copy (list);

		/* this should trigger the book's weak ref notify
		 * function, which will remove it from the list before
		 * it's freed, and will remove the connection from
		 * priv->connections once they're all gone */
		g_list_foreach (copy, remove_data_book_cb, NULL);
		g_list_free (copy);
	}

	g_mutex_unlock (factory->priv->connections_lock);
#endif	
}

static void
e_dbus_manager_init (EDBusManager *self)
{
	EDBusManagerPrivate *priv = MANAGER_PRIVATE(self);

	data_session = e_mail_data_session_new ();

	priv->owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
				E_MAIL_DATA_FACTORY_SERVICE_NAME,
				G_BUS_NAME_OWNER_FLAGS_NONE,
				on_bus_acquired,
				on_name_acquired,
				on_name_lost,
				self,
				NULL);
			
}

EDBusManager*
e_dbus_manager_new (void)
{
  return g_object_new (EDBUS_TYPE_MANAGER, NULL);
}


