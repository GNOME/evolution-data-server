/*
 * SPDX-FileCopyrightText: (C) 2015 Red Hat, Inc. (www.redhat.com)
 * SPDX-FileCopyrightText: (C) 2018 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef E_CREDENTIALS_PROMPTER_IMPL_OAUTH2_H
#define E_CREDENTIALS_PROMPTER_IMPL_OAUTH2_H

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

#if GTK_CHECK_VERSION(4, 0, 0)
#include <libedataserverui4/e-credentials-prompter-impl.h>
#else
#include <libedataserverui/e-credentials-prompter-impl.h>
#endif

/* Standard GObject macros */
#define E_TYPE_CREDENTIALS_PROMPTER_IMPL_OAUTH2 \
	(e_credentials_prompter_impl_oauth2_get_type ())
#define E_CREDENTIALS_PROMPTER_IMPL_OAUTH2(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CREDENTIALS_PROMPTER_IMPL_OAUTH2, ECredentialsPrompterImplOAuth2))
#define E_CREDENTIALS_PROMPTER_IMPL_OAUTH2_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CREDENTIALS_PROMPTER_IMPL_OAUTH2, ECredentialsPrompterImplOAuth2Class))
#define E_IS_CREDENTIALS_PROMPTER_IMPL_OAUTH2(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CREDENTIALS_PROMPTER_IMPL_OAUTH2))
#define E_IS_CREDENTIALS_PROMPTER_IMPL_OAUTH2_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CREDENTIALS_PROMPTER_IMPL_OAUTH2))
#define E_CREDENTIALS_PROMPTER_IMPL_OAUTH2_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CREDENTIALS_PROMPTER_IMPL_OAUTH2, ECredentialsPrompterImplOAuth2Class))

G_BEGIN_DECLS

typedef struct _ECredentialsPrompterImplOAuth2 ECredentialsPrompterImplOAuth2;
typedef struct _ECredentialsPrompterImplOAuth2Class ECredentialsPrompterImplOAuth2Class;
typedef struct _ECredentialsPrompterImplOAuth2Private ECredentialsPrompterImplOAuth2Private;

/**
 * ECredentialsPrompterImplOAuth2:
 * Since: 3.28
 **/
struct _ECredentialsPrompterImplOAuth2 {
	ECredentialsPrompterImpl parent;
	ECredentialsPrompterImplOAuth2Private *priv;
};

struct _ECredentialsPrompterImplOAuth2Class {
	ECredentialsPrompterImplClass parent_class;
};

GType		e_credentials_prompter_impl_oauth2_get_type	(void) G_GNUC_CONST;
ECredentialsPrompterImpl *
		e_credentials_prompter_impl_oauth2_new	(void);

G_END_DECLS

#endif /* E_CREDENTIALS_PROMPTER_IMPL_OAUTH2_H */
