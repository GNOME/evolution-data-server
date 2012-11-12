/*
 * module-data-factory-goa.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#include <libebackend/libebackend.h>

#define GOA_API_IS_SUBJECT_TO_CHANGE
#include <goa/goa.h>

/* Standard GObject macros */
#define E_TYPE_DATA_FACTORY_GOA \
	(e_data_factory_goa_get_type ())
#define E_DATA_FACTORY_GOA(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_DATA_FACTORY_GOA, EDataFactoryGoa))

typedef struct _EDataFactoryGoa EDataFactoryGoa;
typedef struct _EDataFactoryGoaClass EDataFactoryGoaClass;

struct _EDataFactoryGoa {
	EExtension parent;

	GoaClient *goa_client;
	GHashTable *goa_accounts;

	gulong account_added_handler_id;
	gulong account_removed_handler_id;
};

struct _EDataFactoryGoaClass {
	EExtensionClass parent_class;
};

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_data_factory_goa_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	EDataFactoryGoa,
	e_data_factory_goa,
	E_TYPE_EXTENSION)

static void
data_factory_goa_collect_accounts (EDataFactoryGoa *extension)
{
	GList *list, *link;

	g_hash_table_remove_all (extension->goa_accounts);

	list = goa_client_get_accounts (extension->goa_client);

	for (link = list; link != NULL; link = g_list_next (link)) {
		GoaObject *goa_object;
		GoaAccount *goa_account;
		const gchar *goa_account_id;

		goa_object = GOA_OBJECT (link->data);

		goa_account = goa_object_peek_account (goa_object);
		goa_account_id = goa_account_get_id (goa_account);
		g_return_if_fail (goa_account_id != NULL);

		g_hash_table_insert (
			extension->goa_accounts,
			g_strdup (goa_account_id),
			g_object_ref (goa_object));
	}

	g_list_free_full (list, (GDestroyNotify) g_object_unref);
}

static void
data_factory_goa_account_added_cb (GoaClient *goa_client,
                                   GoaObject *goa_object,
                                   EDataFactoryGoa *extension)
{
	GoaAccount *goa_account;
	const gchar *goa_account_id;

	goa_account = goa_object_peek_account (goa_object);
	goa_account_id = goa_account_get_id (goa_account);
	g_return_if_fail (goa_account_id != NULL);

	g_hash_table_insert (
		extension->goa_accounts,
		g_strdup (goa_account_id),
		g_object_ref (goa_object));
}

static void
data_factory_goa_account_removed_cb (GoaClient *goa_client,
                                     GoaObject *goa_object,
                                     EDataFactoryGoa *extension)
{
	GoaAccount *goa_account;
	const gchar *goa_account_id;

	goa_account = goa_object_peek_account (goa_object);
	goa_account_id = goa_account_get_id (goa_account);
	g_return_if_fail (goa_account_id != NULL);

	g_hash_table_remove (extension->goa_accounts, goa_account_id);
}

static void
data_factory_goa_backend_created_cb (EDataFactory *factory,
                                     EBackend *backend,
                                     EDataFactoryGoa *extension)
{
	ESource *source;
	GoaObject *goa_object;
	ESourceGoa *goa_extension;
	ESourceRegistry *registry = NULL;
	const gchar *extension_name;
	gchar *goa_account_id;

	/* Embed the corresponding GoaObject in the EBackend so the
	 * backend can retrieve it by name with g_object_get_data(). */

	source = e_backend_get_source (backend);
	extension_name = E_SOURCE_EXTENSION_GOA;

	/* XXX Both EDataBookFactory and EDataCalFactory have an
	 *     ESourceRegistry property, so retrieve it by name. */
	g_object_get (factory, "registry", &registry, NULL);
	g_return_if_fail (E_IS_SOURCE_REGISTRY (registry));

	/* Check source and its ancestors for a GOA extension. */
	source = e_source_registry_find_extension (
		registry, source, extension_name);

	g_object_unref (registry);

	if (source == NULL)
		return;

	goa_extension = e_source_get_extension (source, extension_name);
	goa_account_id = e_source_goa_dup_account_id (goa_extension);
	g_return_if_fail (goa_account_id != NULL);

	goa_object = g_hash_table_lookup (
		extension->goa_accounts, goa_account_id);
	if (goa_object != NULL) {
		g_object_set_data_full (
			G_OBJECT (backend),
			"GNOME Online Account",
			g_object_ref (goa_object),
			(GDestroyNotify) g_object_unref);
	}

	g_free (goa_account_id);
	g_object_unref (source);
}

static void
data_factory_goa_dispose (GObject *object)
{
	EDataFactoryGoa *extension;

	extension = E_DATA_FACTORY_GOA (object);

	if (extension->goa_client != NULL) {
		g_signal_handler_disconnect (
			extension->goa_client,
			extension->account_added_handler_id);
		g_signal_handler_disconnect (
			extension->goa_client,
			extension->account_removed_handler_id);
		g_object_unref (extension->goa_client);
		extension->goa_client = NULL;
	}

	g_hash_table_remove_all (extension->goa_accounts);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_data_factory_goa_parent_class)->dispose (object);
}

static void
data_factory_goa_finalize (GObject *object)
{
	EDataFactoryGoa *extension;

	extension = E_DATA_FACTORY_GOA (object);

	g_hash_table_destroy (extension->goa_accounts);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_factory_goa_parent_class)->finalize (object);
}

static void
data_factory_goa_constructed (GObject *object)
{
	EDataFactoryGoa *extension;
	EExtensible *extensible;
	GError *error = NULL;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_data_factory_goa_parent_class)->constructed (object);

	extension = E_DATA_FACTORY_GOA (object);
	extensible = e_extension_get_extensible (E_EXTENSION (extension));

	/* XXX This blocks to make sure we don't miss any
	 *     "backend-created" signals from EDataFactory. */
	extension->goa_client = goa_client_new_sync (NULL, &error);

	if (extension->goa_client != NULL) {
		gulong handler_id;

		data_factory_goa_collect_accounts (extension);

		handler_id = g_signal_connect (
			extension->goa_client, "account-added",
			G_CALLBACK (data_factory_goa_account_added_cb),
			extension);
		extension->account_added_handler_id = handler_id;

		handler_id = g_signal_connect (
			extension->goa_client, "account-removed",
			G_CALLBACK (data_factory_goa_account_removed_cb),
			extension);
		extension->account_removed_handler_id = handler_id;

		g_signal_connect (
			extensible, "backend-created",
			G_CALLBACK (data_factory_goa_backend_created_cb),
			extension);
	} else {
		g_warning (
			"Failed to create a GoaClient: %s",
			error->message);
		g_error_free (error);
	}
}

static void
e_data_factory_goa_class_init (EDataFactoryGoaClass *class)
{
	GObjectClass *object_class;
	EExtensionClass *extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = data_factory_goa_dispose;
	object_class->finalize = data_factory_goa_finalize;
	object_class->constructed = data_factory_goa_constructed;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_DATA_FACTORY;
}

static void
e_data_factory_goa_class_finalize (EDataFactoryGoaClass *class)
{
}

static void
e_data_factory_goa_init (EDataFactoryGoa *extension)
{
	extension->goa_accounts = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_object_unref);
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_data_factory_goa_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}

