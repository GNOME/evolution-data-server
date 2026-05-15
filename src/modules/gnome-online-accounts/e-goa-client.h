/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/* This is an improved GoaClient.  It handles goa-daemon crashes/restarts
 * gracefully by emitting an "account-swapped" signal for each pair of old
 * and new proxy objects for the same "online account" once the goa-daemon
 * restarts.  Contrast with GoaClient, which emits false "account-removed"
 * and "account-added" signals and forces apps to distinguish them from an
 * actual removal or addition of an "online account". */

#ifndef E_GOA_CLIENT_H
#define E_GOA_CLIENT_H

/* XXX Yeah, yeah... */
#define GOA_API_IS_SUBJECT_TO_CHANGE

#include <goa/goa.h>

/* Standard GObject macros */
#define E_TYPE_GOA_CLIENT \
	(e_goa_client_get_type ())
#define E_GOA_CLIENT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_GOA_CLIENT, EGoaClient))
#define E_GOA_CLIENT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_GOA_CLIENT, EGoaClientClass))
#define E_IS_GOA_CLIENT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_GOA_CLIENT))
#define E_IS_GOA_CLIENT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_GOA_CLIENT))
#define E_GOA_CLIENT_CLASS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_GOA_CLIENT, EGoaClientClass))

G_BEGIN_DECLS

typedef struct _EGoaClient EGoaClient;
typedef struct _EGoaClientClass EGoaClientClass;
typedef struct _EGoaClientPrivate EGoaClientPrivate;

struct _EGoaClient {
	GObject parent;
	EGoaClientPrivate *priv;
};

struct _EGoaClientClass {
	GObjectClass parent_class;

	/* Signals */
	void		(*account_added)	(EGoaClient *client,
						 GoaObject *object);
	void		(*account_removed)	(EGoaClient *client,
						 GoaObject *object);
	void		(*account_swapped)	(EGoaClient *client,
						 GoaObject *old_object,
						 GoaObject *new_object);
};

GType		e_goa_client_get_type		(void) G_GNUC_CONST;
void		e_goa_client_type_register	(GTypeModule *type_module);
void		e_goa_client_new		(GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
EGoaClient *	e_goa_client_new_finish		(GAsyncResult *result,
						 GError **error);
GDBusObjectManager *
		e_goa_client_ref_object_manager	(EGoaClient *client);
GList *		e_goa_client_list_accounts	(EGoaClient *client);
GoaObject *	e_goa_client_lookup_by_id	(EGoaClient *client,
						 const gchar *id);

G_END_DECLS

#endif /* E_GOA_CLIENT_H */

