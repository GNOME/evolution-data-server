/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_NULL_OUTPUT_STREAM_H
#define CAMEL_NULL_OUTPUT_STREAM_H

#include <gio/gio.h>

/* Standard GObject macros */
#define CAMEL_TYPE_NULL_OUTPUT_STREAM \
	(camel_null_output_stream_get_type ())
#define CAMEL_NULL_OUTPUT_STREAM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_NULL_OUTPUT_STREAM, CamelNullOutputStream))
#define CAMEL_NULL_OUTPUT_STREAM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_NULL_OUTPUT_STREAM, CamelNullOutputStreamClass))
#define CAMEL_IS_NULL_OUTPUT_STREAM(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_NULL_OUTPUT_STREAM))
#define CAMEL_IS_NULL_OUTPUT_STREAM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_NULL_OUTPUT_STREAM))
#define CAMEL_NULL_OUTPUT_STREAM_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_NULL_OUTPUT_STREAM, CamelNullOutputStreamClass))

G_BEGIN_DECLS

typedef struct _CamelNullOutputStream CamelNullOutputStream;
typedef struct _CamelNullOutputStreamClass CamelNullOutputStreamClass;
typedef struct _CamelNullOutputStreamPrivate CamelNullOutputStreamPrivate;

struct _CamelNullOutputStream {
	GOutputStream parent;
	CamelNullOutputStreamPrivate *priv;
};

struct _CamelNullOutputStreamClass {
	GOutputStreamClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_null_output_stream_get_type
					(void) G_GNUC_CONST;
GOutputStream *	camel_null_output_stream_new
					(void);
gsize		camel_null_output_stream_get_bytes_written
					(CamelNullOutputStream *null_stream);
gboolean	camel_null_output_stream_get_ends_with_crlf
					(CamelNullOutputStream *null_stream);

G_END_DECLS

#endif /* CAMEL_NULL_OUTPUT_STREAM_H */

