/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* Contains private functions not meant to be exposed as public API */

#ifndef CAMEL_STORE_SEARCH_PRIVATE_H
#define CAMEL_STORE_SEARCH_PRIVATE_H

#include <sqlite3.h>

#include "camel-db.h"
#include "camel-search-private.h"
#include "camel-store-db.h"
#include "camel-store-search.h"
#include "camel-store.h"

G_BEGIN_DECLS

typedef enum {
	CMP_HEADER_CONTAINS,
	CMP_HEADER_MATCHES,
	CMP_HEADER_STARTS_WITH,
	CMP_HEADER_ENDS_WITH,
	CMP_HEADER_EXISTS,
	CMP_HEADER_SOUNDEX,
	CMP_HEADER_REGEX,
	CMP_HEADER_FULL_REGEX,
	CMP_HEADER_HAS_WORDS
} CmpHeaderKind;

typedef enum {
	CMP_BODY_TEXT,
	CMP_BODY_REGEX
} CmpBodyKind;

sqlite3 *	_camel_db_get_sqlite_db			(CamelDB *self);

void		_camel_store_db_register_search		(CamelStoreDB *self,
							 CamelStoreSearch *search);
void		_camel_store_db_unregister_search	(CamelStoreDB *self,
							 CamelStoreSearch *search);

gboolean	_camel_store_search_compare_text	(CamelStoreSearch *self,
							 const gchar *uid,
							 const gchar *default_charset,
							 const gchar *header_name,
							 CmpHeaderKind cmp_kind,
							 const gchar *haystack,
							 const gchar *needle);
gboolean	_camel_store_search_search_body		(CamelStoreSearch *self,
							 const gchar *uid,
							 CmpBodyKind cmp_kind,
							 const gchar *encoded_words);
gboolean	_camel_store_search_search_header	(CamelStoreSearch *self,
							 const gchar *uid,
							 const gchar *header_name,
							 CmpHeaderKind cmp_kind,
							 const gchar *needle,
							 const gchar *db_value);
gchar *		_camel_store_search_dup_user_tag	(CamelStoreSearch *self,
							 const gchar *uid,
							 const gchar *tag_name,
							 const gchar *dbvalue);
gchar *		_camel_store_search_from_loaded_info_or_db
							(CamelStoreSearch *self,
							 const gchar *uid,
							 const gchar *column_name,
							 const gchar *dbvalue);
gboolean	_camel_store_search_addressbook_contains(CamelStoreSearch *self,
							 const gchar *book_uid,
							 const gchar *email);
gboolean	_camel_store_search_check_labels	(CamelStoreSearch *self,
							 const gchar *uid,
							 const gchar *label_to_check,
							 const gchar *dbvalue);
gboolean	_camel_store_search_check_flags		(CamelStoreSearch *self,
							 const gchar *uid,
							 guint32 flags_to_check,
							 guint32 dbvalue);
gboolean	_camel_store_search_in_result_index	(CamelStoreSearch *self,
							 const gchar *uid);
gboolean	_camel_store_search_in_match_index	(CamelStoreSearch *self,
							 const gchar *index_id,
							 const gchar *uid);
gboolean	_camel_store_search_is_folder_id	(CamelStoreSearch *self,
							 guint32 folder_id);

G_END_DECLS

#endif /* CAMEL_STORE_SEARCH_PRIVATE_H */
