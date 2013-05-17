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
 * | 6   | No    | Bäd         | C           10 | bat         4  | bat         4  | bät         7  |
 * | 7   | No    | bät         | Muffler     19 | Bat         3  | Bat         3  | Bät         8  |
 * | 8   | Yes   | Bät         | Müller      20 | bät         7  | bät         7  | bat         4  |
 * | 9   | Yes   | côté        | bad         1  | Bät         8  | Bät         8  | Bat         3  |
 * | 10  | Yes   | C           | bäd         5  | black-bird  15 | black-bird  15 | black-bird  15 |
 * | 11  | Yes   |             | bat         4  | black-birds 17 | black-birds 17 | black-birds 17 |
 * | 12  | Yes   | coté        | bät         7  | blackbird   16 | blackbird   16 | blackbird   16 |
 * | 13  | No    | côte        | black-bird  15 | blackbirds  18 | blackbirds  18 | blackbirds  18 |
 * | 14  | Yes   | cote        | black-birds 17 | C           10 | C           10 | C           10 |
 * | 15  | No    | black-bird  | blackbird   16 | cote        14 | cote        14 | cote        14 |
 * | 16  | Yes   | blackbird   | blackbirds  18 | coté        12 | côte        13 | coté        12 | 
 * | 17  | Yes   | black-birds | cote        14 | côte        13 | coté        12 | côte        13 | 
 * | 18  | Yes   | blackbirds  | coté        12 | côté        9  | côté        9  | côté        9  | 
 * | 19  | No    | Muffler     | côte        13 | Muffler     19 | Muffler     19 | Müller      20 | 
 * | 20  | No    | Müller      | côté        9  | Müller      20 | Müller      20 | Muffler     19 |
 * +-----------------------------------------------------------------------------------------------+
 *
 * See this ICU demo to check additional sort ordering by ICU in various locales:
 *     http://demo.icu-project.org/icu-bin/locexp?_=en_US&d_=en&x=col
 */

#define SQLITEDB_FOLDER_ID   "folder_id"
#define N_SORTED_CONTACTS    20
#define MAX_MOVE_BY_COUNTS   5

typedef struct {
	ETestServerFixture parent_fixture;

	EBookBackendSqliteDB *ebsdb;
} ESqliteDBFixture;

typedef struct {
	ESqliteDBFixture parent_fixture;

	EbSdbCursor     *cursor;
	EContact        *contacts[N_SORTED_CONTACTS];
	EBookQuery      *query;
} EbSdbCursorFixture;

typedef struct {
	ETestServerClosure parent;

} EbSdbCursorClosure;

typedef struct {
	EbSdbCursorClosure parent;
	gchar *path;

	/* array of counts to move by, terminated with 0 or MAX_COUNTS */
	gint counts[MAX_MOVE_BY_COUNTS];

	/* For each move_by() command, an array of 'ABS (counts[i])' expected contacts */
	gint expected[MAX_MOVE_BY_COUNTS][N_SORTED_CONTACTS];

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
MoveByData *move_by_test_new               (const gchar *test_path);
void        move_by_test_add               (MoveByData  *data,
					    gboolean     filtered);

#endif /* DATA_TEST_UTILS_H */
