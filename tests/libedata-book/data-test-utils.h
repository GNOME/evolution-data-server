/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2013, Openismus GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

#ifndef DATA_TEST_UTILS_H
#define DATA_TEST_UTILS_H

#include <libebook/libebook.h>
#include <libedata-book/libedata-book.h>
#include "e-test-server-utils.h"
#include "e-dbus-localed.h"

/* This legend shows the add order, and various sort order of the sorted
 * vcards. The UIDs of these contacts are formed as 'sorted-1', 'sorted-2' etc
 * and the numbering of the contacts is according to the 'N' column in the
 * following legend.
 *
 * The Email column indicates whether the contact has a .com email address
 * (in order to test filtered cursor results) and corresponds to the natural
 * order in the 'N' column.
 *
 * +-----------------------------------------------------------------------------------------------+
 * | N   | Email | Last Name   | en_US_POSIX    | en_US / de_DE  | fr_CA          | de_DE          |
 * |     |       |             |                |                |                | (phonebook)    |
 * +-----------------------------------------------------------------------------------------------+
 * | 1   | Yes   | bad         |             11 |             11 |             11 |             11 |
 * | 2   | Yes   | Bad         | Bad         2  | bad         1  | bad         1  | bad         1  |
 * | 3   | Yes   | Bat         | Bäd         6  | Bad         2  | Bad         2  | Bad         2  |
 * | 4   | No    | bat         | Bat         3  | bäd         5  | bäd         5  | bäd         5  |
 * | 5   | Yes   | bäd         | Bät         8  | Bäd         6  | Bäd         6  | Bäd         6  |
 * | 6   | No    | Bäd         | bad         1  | bat         4  | bat         4  | bät         7  |
 * | 7   | No    | bät         | bäd         5  | Bat         3  | Bat         3  | Bät         8  |
 * | 8   | Yes   | Bät         | bat         4  | bät         7  | bät         7  | bat         4  |
 * | 9   | Yes   | côté        | bät         7  | Bät         8  | Bät         8  | Bat         3  |
 * | 10  | Yes   | C           | black-bird  15 | black-bird  15 | black-bird  15 | black-bird  15 |
 * | 11  | Yes   |             | black-birds 17 | black-birds 17 | black-birds 17 | black-birds 17 |
 * | 12  | Yes   | coté        | blackbird   16 | blackbird   16 | blackbird   16 | blackbird   16 |
 * | 13  | No    | côte        | blackbirds  18 | blackbirds  18 | blackbirds  18 | blackbirds  18 |
 * | 14  | Yes   | cote        | C           10 | C           10 | C           10 | C           10 |
 * | 15  | No    | black-bird  | cote        14 | cote        14 | cote        14 | cote        14 |
 * | 16  | Yes   | blackbird   | coté        12 | coté        12 | côte        13 | coté        12 | 
 * | 17  | Yes   | black-birds | côte        13 | côte        13 | coté        12 | côte        13 | 
 * | 18  | Yes   | blackbirds  | côté        9  | côté        9  | côté        9  | côté        9  | 
 * | 19  | No    | Muffler     | Muffler     19 | Muffler     19 | Muffler     19 | Müller      20 | 
 * | 20  | No    | Müller      | Müller      20 | Müller      20 | Müller      20 | Muffler     19 |
 * +-----------------------------------------------------------------------------------------------+
 *
 * See this ICU demo to check additional sort ordering by ICU in various locales:
 *     http://demo.icu-project.org/icu-bin/locexp?_=en_US&d_=en&x=col
 */

#define SQLITEDB_FOLDER_ID   "folder_id"
#define N_SORTED_CONTACTS    20
#define MAX_MOVE_BY_COUNTS   5

/* 13 contacts in the test data have an email address ending with ".com" */
#define N_FILTERED_CONTACTS  13


typedef struct {
	ETestServerFixture parent_fixture;

	EBookBackendSqliteDB *ebsdb;
} ESqliteDBFixture;

typedef struct {
	ESqliteDBFixture parent_fixture;

	EbSdbCursor     *cursor;
	EContact        *contacts[N_SORTED_CONTACTS];
	EBookQuery      *query;

	EDBusLocale1    *locale1;
	guint            own_id;
} EbSdbCursorFixture;

typedef struct {
	ETestServerClosure parent;

	const gchar   *locale;
	EBookSortType  sort_type;
} EbSdbCursorClosure;

typedef struct {
	EbSdbCursorClosure parent;
	gchar *path;

	/* array of counts to move by, terminated with 0 or MAX_COUNTS */
	gint counts[MAX_MOVE_BY_COUNTS];

	/* For each move_by() command, an array of 'ABS (counts[i])' expected contacts */
	gint expected[MAX_MOVE_BY_COUNTS][N_SORTED_CONTACTS];

	/* Whether this is a filtered test */
	gboolean filtered;

	/* Private detail */
	gsize struct_size;
} MoveByData;

void     e_sqlitedb_fixture_setup          (ESqliteDBFixture *fixture,
					    gconstpointer     user_data);
void     e_sqlitedb_fixture_teardown       (ESqliteDBFixture *fixture,
					    gconstpointer     user_data);

void     e_sqlitedb_cursor_fixture_setup_book (ESource            *scratch,
					       ETestServerClosure *closure);
void     e_sqlitedb_cursor_fixture_setup    (EbSdbCursorFixture *fixture,
					     gconstpointer       user_data);
void     e_sqlitedb_cursor_fixture_teardown (EbSdbCursorFixture *fixture,
					     gconstpointer       user_data);
void     e_sqlitedb_cursor_fixture_set_locale (EbSdbCursorFixture *fixture,
					       const gchar        *locale);

/* Filters contacts with E_CONTACT_EMAIL ending with '.com' */
void     e_sqlitedb_cursor_fixture_filtered_setup (EbSdbCursorFixture *fixture,
						   gconstpointer  user_data);


gchar    *new_vcard_from_test_case         (const gchar *case_name);
EContact *new_contact_from_test_case       (const gchar *case_name);

gboolean add_contact_from_test_case_verify (EBookClient *book_client,
					    const gchar *case_name,
					    EContact   **contact);

void     assert_contacts_order_slist       (GSList      *results,
					    GSList      *uids);
void     assert_contacts_order             (GSList      *results,
					    const gchar *first_uid,
					    ...) G_GNUC_NULL_TERMINATED;

void     print_results                     (GSList      *results);

/*  MoveBy test helpers */
void        move_by_test_add_assertion     (MoveByData  *data,
					    gint         count,
					    ...);
MoveByData *move_by_test_new               (const gchar *test_path,
					    const gchar *locale);
MoveByData *move_by_test_new_full          (const gchar   *test_path,
					    const gchar   *locale,
					    EBookSortType  sort_type);
void        move_by_test_add               (MoveByData  *data,
					    gboolean     filtered);

#endif /* DATA_TEST_UTILS_H */
