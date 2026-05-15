/*
 * SPDX-FileCopyrightText: (C) 2015 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef E_CREDENTIALS_PROMPTER_IMPL_H
#define E_CREDENTIALS_PROMPTER_IMPL_H

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#if !defined (__LIBEDATASERVERUI_H_INSIDE__) && !defined (LIBEDATASERVERUI_COMPILATION)
#if GTK_CHECK_VERSION(4, 0, 0)
#error "Only <libedataserverui4/libedataserverui4.h> should be included directly."
#else
#error "Only <libedataserverui/libedataserverui.h> should be included directly."
#endif
#endif

#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_CREDENTIALS_PROMPTER_IMPL \
	(e_credentials_prompter_impl_get_type ())
#define E_CREDENTIALS_PROMPTER_IMPL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CREDENTIALS_PROMPTER_IMPL, ECredentialsPrompterImpl))
#define E_CREDENTIALS_PROMPTER_IMPL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CREDENTIALS_PROMPTER_IMPL, ECredentialsPrompterImplClass))
#define E_IS_CREDENTIALS_PROMPTER_IMPL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CREDENTIALS_PROMPTER_IMPL))
#define E_IS_CREDENTIALS_PROMPTER_IMPL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CREDENTIALS_PROMPTER_IMPL))
#define E_CREDENTIALS_PROMPTER_IMPL_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CREDENTIALS_PROMPTER_IMPL, ECredentialsPrompterImplClass))

G_BEGIN_DECLS

typedef struct _ECredentialsPrompterImpl ECredentialsPrompterImpl;
typedef struct _ECredentialsPrompterImplClass ECredentialsPrompterImplClass;
typedef struct _ECredentialsPrompterImplPrivate ECredentialsPrompterImplPrivate;

struct _ECredentialsPrompter;

/**
 * ECredentialsPrompterImpl:
 *
 * Credentials prompter implementation base structure. The descendants
 * implement ECredentialsPrompterImpl::prompt(), which is used to
 * prompt for credentials. The descendants are automatically registered
 * into an #ECredentialsPrompter.
 *
 * Since: 3.16
 **/
struct _ECredentialsPrompterImpl {
	EExtension parent;
	ECredentialsPrompterImplPrivate *priv;
};

struct _ECredentialsPrompterImplClass {
	EExtensionClass parent_class;

	const gchar * const *authentication_methods; /* NULL-terminated array of methods to register with */

	/* Methods */

	void	(*process_prompt)	(ECredentialsPrompterImpl *prompter_impl,
					 gpointer prompt_id,
					 ESource *auth_source,
					 ESource *cred_source,
					 const gchar *error_text,
					 const ENamedParameters *credentials);
	void	(*cancel_prompt)	(ECredentialsPrompterImpl *prompter_impl,
					 gpointer prompt_id);

	/* Signals */

	void	(*prompt_finished)	(ECredentialsPrompterImpl *prompter_impl,
					 gpointer prompt_id,
					 const ENamedParameters *credentials);
};

GType		e_credentials_prompter_impl_get_type	(void);
struct _ECredentialsPrompter *
		e_credentials_prompter_impl_get_credentials_prompter
							(ECredentialsPrompterImpl *prompter_impl);
void		e_credentials_prompter_impl_prompt	(ECredentialsPrompterImpl *prompter_impl,
							 gpointer prompt_id,
							 ESource *auth_source,
							 ESource *cred_source,
							 const gchar *error_text,
							 const ENamedParameters *credentials);
void		e_credentials_prompter_impl_prompt_finish
							(ECredentialsPrompterImpl *prompter_impl,
							 gpointer prompt_id,
							 const ENamedParameters *credentials);
void		e_credentials_prompter_impl_cancel_prompt
							(ECredentialsPrompterImpl *prompter_impl,
							 gpointer prompt_id);

G_END_DECLS

#endif /* E_CREDENTIALS_PROMPTER_IMPL_H */
