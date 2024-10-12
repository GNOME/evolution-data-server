/*
 * SPDX-FileCopyrightText: (C) 2022 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_GDATA_QUERY_H
#define E_GDATA_QUERY_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * EGDataQuery:
 *
 * Since: 3.46
 **/
typedef struct _EGDataQuery EGDataQuery;

#define E_TYPE_GDATA_QUERY (e_gdata_query_get_type ())

GType		e_gdata_query_get_type		(void) G_GNUC_CONST;
EGDataQuery *	e_gdata_query_new		(void);
EGDataQuery *	e_gdata_query_ref		(EGDataQuery *self);
void		e_gdata_query_unref		(EGDataQuery *self);
gchar *		e_gdata_query_to_string		(EGDataQuery *self);
void		e_gdata_query_set_max_results	(EGDataQuery *self,
						 gint value);
gint		e_gdata_query_get_max_results	(EGDataQuery *self,
						 gboolean *out_exists);
void		e_gdata_query_set_completed_max	(EGDataQuery *self,
						 gint64 value);
gint64		e_gdata_query_get_completed_max	(EGDataQuery *self,
						 gboolean *out_exists);
void		e_gdata_query_set_completed_min	(EGDataQuery *self,
						 gint64 value);
gint64		e_gdata_query_get_completed_min	(EGDataQuery *self,
						 gboolean *out_exists);
void		e_gdata_query_set_due_max	(EGDataQuery *self,
						 gint64 value);
gint64		e_gdata_query_get_due_max	(EGDataQuery *self,
						 gboolean *out_exists);
void		e_gdata_query_set_due_min	(EGDataQuery *self,
						 gint64 value);
gint64		e_gdata_query_get_due_min	(EGDataQuery *self,
						 gboolean *out_exists);
void		e_gdata_query_set_show_completed(EGDataQuery *self,
						 gboolean value);
gboolean	e_gdata_query_get_show_completed(EGDataQuery *self,
						 gboolean *out_exists);
void		e_gdata_query_set_show_deleted	(EGDataQuery *self,
						 gboolean value);
gboolean	e_gdata_query_get_show_deleted	(EGDataQuery *self,
						 gboolean *out_exists);
void		e_gdata_query_set_show_hidden	(EGDataQuery *self,
						 gboolean value);
gboolean	e_gdata_query_get_show_hidden	(EGDataQuery *self,
						 gboolean *out_exists);
void		e_gdata_query_set_updated_min	(EGDataQuery *self,
						 gint64 value);
gint64		e_gdata_query_get_updated_min	(EGDataQuery *self,
						 gboolean *out_exists);

G_END_DECLS

#endif /* E_GDATA_SESSION_H */
