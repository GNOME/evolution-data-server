/*
 * e-signon-session-password.h
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef E_SIGNON_SESSION_PASSWORD_H
#define E_SIGNON_SESSION_PASSWORD_H

#include <libebackend/libebackend.h>

/* Standard GObject macros */
#define E_TYPE_SIGNON_SESSION_PASSWORD \
	(e_signon_session_password_get_type ())
#define E_SIGNON_SESSION_PASSWORD(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SIGNON_SESSION_PASSWORD, ESignonSessionPassword))
#define E_SIGNON_SESSION_PASSWORD_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SIGNON_SESSION_PASSWORD, ESignonSessionPasswordClass))
#define E_IS_SIGNON_SESSION_PASSWORD(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SIGNON_SESSION_PASSWORD))
#define E_IS_SIGNON_SESSION_PASSWORD_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SIGNON_SESSION_PASSWORD))
#define E_SIGNON_SESSION_PASSWORD_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SIGNON_SESSION_PASSWORD, ESignonSessionPasswordClass))

G_BEGIN_DECLS

typedef struct _ESignonSessionPassword ESignonSessionPassword;
typedef struct _ESignonSessionPasswordClass ESignonSessionPasswordClass;
typedef struct _ESignonSessionPasswordPrivate ESignonSessionPasswordPrivate;

struct _ESignonSessionPassword {
	EAuthenticationSession parent;
	ESignonSessionPasswordPrivate *priv;
};

struct _ESignonSessionPasswordClass {
	EAuthenticationSessionClass parent_class;
};

GType		e_signon_session_password_get_type
						(void) G_GNUC_CONST;
void		e_signon_session_password_type_register
						(GTypeModule *type_module);

G_END_DECLS

#endif /* E_SIGNON_SESSION_PASSWORD_H */

