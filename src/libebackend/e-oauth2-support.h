/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEBACKEND_H_INSIDE__) && !defined (LIBEBACKEND_COMPILATION)
#error "Only <libebackend/libebackend.h> should be included directly."
#endif

#ifndef E_OAUTH2_SUPPORT_H
#define E_OAUTH2_SUPPORT_H

#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_OAUTH2_SUPPORT \
	(e_oauth2_support_get_type ())
#define E_OAUTH2_SUPPORT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_OAUTH2_SUPPORT, EOAuth2Support))
#define E_IS_OAUTH2_SUPPORT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_OAUTH2_SUPPORT))
#define E_OAUTH2_SUPPORT_GET_INTERFACE(obj) \
	(G_TYPE_INSTANCE_GET_INTERFACE \
	((obj), E_TYPE_OAUTH2_SUPPORT, EOAuth2SupportInterface))

G_BEGIN_DECLS

/**
 * EOAuth2Support:
 *
 * Since: 3.8
 **/
typedef struct _EOAuth2Support EOAuth2Support;
typedef struct _EOAuth2SupportInterface EOAuth2SupportInterface;

/**
 * EOAuth2SupportInterface:
 * @get_access_token_sync: synchronous interface method to get the access token
 * @get_access_token: asynchronous interface method to get the access token
 * @get_access_token_finish: counterpart of the asynchronous interface method to finishd get of the access token
 *
 * Since: 3.8
 **/
struct _EOAuth2SupportInterface {
	/*< private >*/
	GTypeInterface parent_interface;

	/*< public >*/
	/* Synchronous I/O Methods */
	gboolean	(*get_access_token_sync)
						(EOAuth2Support *support,
						 ESource *source,
						 GCancellable *cancellable,
						 gchar **out_access_token,
						 gint *out_expires_in,
						 GError **error);

	/* Asynchronous I/O Methods (all have defaults) */
	void		(*get_access_token)	(EOAuth2Support *support,
						 ESource *source,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	gboolean	(*get_access_token_finish)
						(EOAuth2Support *support,
						 GAsyncResult *result,
						 gchar **out_access_token,
						 gint *out_expires_in,
						 GError **error);
};

GType		e_oauth2_support_get_type	(void) G_GNUC_CONST;
gboolean	e_oauth2_support_get_access_token_sync
						(EOAuth2Support *support,
						 ESource *source,
						 GCancellable *cancellable,
						 gchar **out_access_token,
						 gint *out_expires_in,
						 GError **error);
void		e_oauth2_support_get_access_token
						(EOAuth2Support *support,
						 ESource *source,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_oauth2_support_get_access_token_finish
						(EOAuth2Support *support,
						 GAsyncResult *result,
						 gchar **out_access_token,
						 gint *out_expires_in,
						 GError **error);

G_END_DECLS

#endif /* E_OAUTH2_SUPPORT_H */

