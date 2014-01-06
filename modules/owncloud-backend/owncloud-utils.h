/*
 * owncloud-utils.h
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

#ifndef OWNCLOUD_UTILS_H
#define OWNCLOUD_UTILS_H

#include <libebackend/libebackend.h>
#include <libsoup/soup.h>

G_BEGIN_DECLS

typedef enum {
	OwnCloud_Source_Contacts = 1,
	OwnCloud_Source_Events,
	OwnCloud_Source_Memos,
	OwnCloud_Source_Tasks
} OwnCloudSourceType;

typedef void	(*OwnCloudSourceFoundCb)	(ECollectionBackend *collection,
						 OwnCloudSourceType source_type,
						 SoupURI *uri,
						 const gchar *display_name,
						 const gchar *color,
						 gpointer user_data);

gboolean	owncloud_utils_search_server	(ECollectionBackend *collection,
						 OwnCloudSourceFoundCb found_cb,
						 gpointer user_data);

G_END_DECLS

#endif /* OWNCLOUD_UTILS_H */
