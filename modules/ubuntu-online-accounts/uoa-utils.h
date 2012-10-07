/*
 * uoa-utils.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef UOA_UTILS_H
#define UOA_UTILS_H

#include <libaccounts-glib/accounts-glib.h>

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
						 GError **error);

G_END_DECLS

#endif /* UOA_UTILS_H */
