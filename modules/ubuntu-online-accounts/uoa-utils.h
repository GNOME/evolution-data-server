/*
 * uoa-utils.h
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

#ifndef UOA_UTILS_H
#define UOA_UTILS_H

#include <libebackend/libebackend.h>
#include <libaccounts-glib/accounts-glib.h>

/* Service types we support. */
#define E_AG_SERVICE_TYPE_MAIL     "mail"
#define E_AG_SERVICE_TYPE_CALENDAR "calendar"
#define E_AG_SERVICE_TYPE_CONTACTS "contacts"

G_BEGIN_DECLS

void		e_ag_account_collect_userinfo	(AgAccount *ag_account,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	e_ag_account_collect_userinfo_finish
						(AgAccount *ag_account,
						 GAsyncResult *result,
						 gchar **out_user_identity,
						 gchar **out_email_address,
						 gchar **out_imap_user_name,
						 gchar **out_smtp_user_name,
						 GError **error);
const gchar *	e_source_get_ag_service_type	(ESource *source);

G_END_DECLS

#endif /* UOA_UTILS_H */
