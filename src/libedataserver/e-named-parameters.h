/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2012 Intel Corporation
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
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_NAMED_PARAMETERS_H
#define E_NAMED_PARAMETERS_H

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * ENamedParameters:
 *
 * Since: 3.8
 **/
struct _ENamedParameters;
typedef struct _ENamedParameters ENamedParameters;

#define E_TYPE_NAMED_PARAMETERS (e_named_parameters_get_type ())

GType           e_named_parameters_get_type     (void) G_GNUC_CONST;
ENamedParameters *
		e_named_parameters_new		(void);
ENamedParameters *
		e_named_parameters_new_strv	(const gchar * const *strv);
ENamedParameters *
		e_named_parameters_new_string	(const gchar *str);
ENamedParameters *
		e_named_parameters_new_clone	(const ENamedParameters *parameters);
void		e_named_parameters_free		(ENamedParameters *parameters);
void		e_named_parameters_clear	(ENamedParameters *parameters);
void		e_named_parameters_assign	(ENamedParameters *parameters,
						 const ENamedParameters *from);
void		e_named_parameters_set		(ENamedParameters *parameters,
						 const gchar *name,
						 const gchar *value);
const gchar *	e_named_parameters_get		(const ENamedParameters *parameters,
						 const gchar *name);
gchar **	e_named_parameters_to_strv	(const ENamedParameters *parameters);
gchar *		e_named_parameters_to_string	(const ENamedParameters *parameters);
gboolean	e_named_parameters_test		(const ENamedParameters *parameters,
						 const gchar *name,
						 const gchar *value,
						 gboolean case_sensitively);
gboolean	e_named_parameters_exists	(const ENamedParameters *parameters,
						 const gchar *name);
guint		e_named_parameters_count	(const ENamedParameters *parameters);
gchar *		e_named_parameters_get_name	(const ENamedParameters *parameters,
						 gint index);
gboolean	e_named_parameters_equal	(const ENamedParameters *parameters1,
						 const ENamedParameters *parameters2);

G_END_DECLS

#endif /* E_NAMED_PARAMETERS_H */
