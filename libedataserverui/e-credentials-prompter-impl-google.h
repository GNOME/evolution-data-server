/*
 * Copyright (C) 2015 Red Hat, Inc. (www.redhat.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#if !defined (__LIBEDATASERVERUI_H_INSIDE__) && !defined (LIBEDATASERVERUI_COMPILATION)
#error "Only <libedataserverui/libedataserverui.h> should be included directly."
#endif

#ifndef E_CREDENTIALS_PROMPTER_IMPL_GOOGLE_H
#define E_CREDENTIALS_PROMPTER_IMPL_GOOGLE_H

#include <glib.h>
#include <glib-object.h>

#include <libedataserverui/e-credentials-prompter-impl.h>

/* Standard GObject macros */
#define E_TYPE_CREDENTIALS_PROMPTER_IMPL_GOOGLE \
	(e_credentials_prompter_impl_google_get_type ())
#define E_CREDENTIALS_PROMPTER_IMPL_GOOGLE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CREDENTIALS_PROMPTER_IMPL_GOOGLE, ECredentialsPrompterImplGoogle))
#define E_CREDENTIALS_PROMPTER_IMPL_GOOGLE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CREDENTIALS_PROMPTER_IMPL_GOOGLE, ECredentialsPrompterImplGoogleClass))
#define E_IS_CREDENTIALS_PROMPTER_IMPL_GOOGLE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CREDENTIALS_PROMPTER_IMPL_GOOGLE))
#define E_IS_CREDENTIALS_PROMPTER_IMPL_GOOGLE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CREDENTIALS_PROMPTER_IMPL_GOOGLE))
#define E_CREDENTIALS_PROMPTER_IMPL_GOOGLE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CREDENTIALS_PROMPTER_IMPL_GOOGLE, ECredentialsPrompterImplGoogleClass))

G_BEGIN_DECLS

typedef struct _ECredentialsPrompterImplGoogle ECredentialsPrompterImplGoogle;
typedef struct _ECredentialsPrompterImplGoogleClass ECredentialsPrompterImplGoogleClass;
typedef struct _ECredentialsPrompterImplGooglePrivate ECredentialsPrompterImplGooglePrivate;

/**
 * ECredentialsPrompterImplGoogle:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.20
 **/
struct _ECredentialsPrompterImplGoogle {
	ECredentialsPrompterImpl parent;
	ECredentialsPrompterImplGooglePrivate *priv;
};

struct _ECredentialsPrompterImplGoogleClass {
	ECredentialsPrompterImplClass parent_class;
};

GType		e_credentials_prompter_impl_google_get_type	(void) G_GNUC_CONST;
ECredentialsPrompterImpl *
		e_credentials_prompter_impl_google_new	(void);

G_END_DECLS

#endif /* E_CREDENTIALS_PROMPTER_IMPL_GOOGLE_H */
