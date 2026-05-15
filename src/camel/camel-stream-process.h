/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: David Woodhouse <dwmw2@infradead.org>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_STREAM_PROCESS_H
#define CAMEL_STREAM_PROCESS_H

#include <camel/camel-stream.h>

/* Standard GObject macros */
#define CAMEL_TYPE_STREAM_PROCESS \
	(camel_stream_process_get_type ())
#define CAMEL_STREAM_PROCESS(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_STREAM_PROCESS, CamelStreamProcess))
#define CAMEL_STREAM_PROCESS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_STREAM_PROCESS, CamelStreamProcessClass))
#define CAMEL_IS_STREAM_PROCESS(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_STREAM_PROCESS))
#define CAMEL_IS_STREAM_PROCESS_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_STREAM_PROCESS))
#define CAMEL_STREAM_PROCESS_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_STREAM_PROCSS, CamelStreamProcessClass))

G_BEGIN_DECLS

typedef struct _CamelStreamProcess CamelStreamProcess;
typedef struct _CamelStreamProcessClass CamelStreamProcessClass;
typedef struct _CamelStreamProcessPrivate CamelStreamProcessPrivate;

struct _CamelStreamProcess {
	CamelStream parent;
	CamelStreamProcessPrivate *priv;
};

struct _CamelStreamProcessClass {
	CamelStreamClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_stream_process_get_type	(void);
CamelStream *	camel_stream_process_new	(void);
gint		camel_stream_process_connect	(CamelStreamProcess *stream,
						 const gchar *command,
						 const gchar **env,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_STREAM_PROCESS_H */
