/*
 * SPDX-FileCopyrightText: (C) 2015 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef E_CREDENTIALS_PROMPTER_IMPL_PASSWORD_H
#define E_CREDENTIALS_PROMPTER_IMPL_PASSWORD_H

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
#define E_TYPE_CREDENTIALS_PROMPTER_IMPL_PASSWORD \
	(e_credentials_prompter_impl_password_get_type ())
#define E_CREDENTIALS_PROMPTER_IMPL_PASSWORD(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CREDENTIALS_PROMPTER_IMPL_PASSWORD, ECredentialsPrompterImplPassword))
#define E_CREDENTIALS_PROMPTER_IMPL_PASSWORD_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CREDENTIALS_PROMPTER_IMPL_PASSWORD, ECredentialsPrompterImplPasswordClass))
#define E_IS_CREDENTIALS_PROMPTER_IMPL_PASSWORD(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CREDENTIALS_PROMPTER_IMPL_PASSWORD))
#define E_IS_CREDENTIALS_PROMPTER_IMPL_PASSWORD_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CREDENTIALS_PROMPTER_IMPL_PASSWORD))
#define E_CREDENTIALS_PROMPTER_IMPL_PASSWORD_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CREDENTIALS_PROMPTER_IMPL_PASSWORD, ECredentialsPrompterImplPasswordClass))

G_BEGIN_DECLS

typedef struct _ECredentialsPrompterImplPassword ECredentialsPrompterImplPassword;
typedef struct _ECredentialsPrompterImplPasswordClass ECredentialsPrompterImplPasswordClass;
typedef struct _ECredentialsPrompterImplPasswordPrivate ECredentialsPrompterImplPasswordPrivate;

/**
 * ECredentialsPrompterImplPassword:
 * Since: 3.16
 **/
struct _ECredentialsPrompterImplPassword {
	ECredentialsPrompterImpl parent;
	ECredentialsPrompterImplPasswordPrivate *priv;
};

struct _ECredentialsPrompterImplPasswordClass {
	ECredentialsPrompterImplClass parent_class;
};

GType		e_credentials_prompter_impl_password_get_type	(void) G_GNUC_CONST;
ECredentialsPrompterImpl *
		e_credentials_prompter_impl_password_new	(void);

G_END_DECLS

#endif /* E_CREDENTIALS_PROMPTER_IMPL_PASSWORD_H */
