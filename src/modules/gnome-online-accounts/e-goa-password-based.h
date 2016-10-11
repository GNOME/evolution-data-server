/*
 * e-goa-password-based.h
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

#ifndef E_GOA_PASSWORD_BASED_H
#define E_GOA_PASSWORD_BASED_H

#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_GOA_PASSWORD_BASED \
	(e_goa_password_based_get_type ())
#define E_GOA_PASSWORD_BASED(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_GOA_PASSWORD_BASED, EGoaPasswordBased))
#define E_GOA_PASSWORD_BASED_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_GOA_PASSWORD_BASED, EGoaPasswordBasedClass))
#define E_IS_GOA_PASSWORD_BASED(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_GOA_PASSWORD_BASED))
#define E_IS_GOA_PASSWORD_BASED_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_GOA_PASSWORD_BASED))
#define E_GOA_PASSWORD_BASED_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_GOA_PASSWORD_BASED, EGoaPasswordBasedClass))

G_BEGIN_DECLS

typedef struct _EGoaPasswordBased EGoaPasswordBased;
typedef struct _EGoaPasswordBasedClass EGoaPasswordBasedClass;
typedef struct _EGoaPasswordBasedPrivate EGoaPasswordBasedPrivate;

struct _EGoaPasswordBased {
	ESourceCredentialsProviderImpl parent;
	EGoaPasswordBasedPrivate *priv;
};

struct _EGoaPasswordBasedClass {
	ESourceCredentialsProviderImplClass parent_class;
};

GType		e_goa_password_based_get_type	(void) G_GNUC_CONST;
void		e_goa_password_based_type_register
						(GTypeModule *type_module);

G_END_DECLS

#endif /* E_GOA_PASSWORD_BASED_H */

