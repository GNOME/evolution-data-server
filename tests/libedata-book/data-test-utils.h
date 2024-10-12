/*
 * Copyright (C) 2013, Openismus GmbH
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
 * Authors: Tristan Van Berkom <tristanvb@openismus.com>
 */

#ifndef DATA_TEST_UTILS_H
#define DATA_TEST_UTILS_H

#include <libedata-book/libedata-book.h>

G_BEGIN_DECLS

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

void	data_test_utils_read_args	(gint argc,
					 gchar **argv);

/* 13 contacts in the test data have an email address ending with ".com" */
#define N_FILTERED_CONTACTS  13
#define N_SORTED_CONTACTS    20

typedef ESourceBackendSummarySetup * (* EbSqlSetupSummary) (void);

typedef struct {
	EBookSqlite *ebsql;

	GHashTable  *contacts;
	gint         n_add_changes;
	gint         n_locale_changes;
} EbSqlFixture;

typedef struct {
	gboolean           without_vcards;
	EbSqlSetupSummary  setup_summary;
} EbSqlClosure;

typedef struct {
	EbSqlFixture     parent_fixture;

	EbSqlCursor     *cursor;
	EContact        *contacts[N_SORTED_CONTACTS];
	EBookQuery      *query;

	guint            own_id;
} EbSqlCursorFixture;

typedef struct {
	EbSqlClosure          parent;

	const gchar          *locale;
	EBookCursorSortType   sort_type;
} EbSqlCursorClosure;

typedef struct {
	/* A locale change */
	gchar *locale;

	/* count argument for move */
	gint count;

	/* An array of 'ABS (counts[i])' expected contacts */
	gint expected[N_SORTED_CONTACTS];
} StepAssertion;

typedef struct {
	EbSqlCursorClosure parent;
	gchar *path;

	GList *assertions;

	/* Whether this is a filtered test */
	gboolean filtered;
} StepData;

/* Base fixture */
void     e_sqlite_fixture_setup              (EbSqlFixture     *fixture,
					      gconstpointer     user_data);
void     e_sqlite_fixture_teardown           (EbSqlFixture *fixture,
					      gconstpointer     user_data);
ESourceBackendSummarySetup *setup_empty_book (void);

/* Cursor fixture */
void     e_sqlite_cursor_fixture_setup       (EbSqlCursorFixture *fixture,
					      gconstpointer       user_data);
void     e_sqlite_cursor_fixture_teardown    (EbSqlCursorFixture *fixture,
					      gconstpointer       user_data);
void     e_sqlite_cursor_fixture_set_locale  (EbSqlCursorFixture *fixture,
					      const gchar        *locale);

/* Filters contacts with E_CONTACT_EMAIL ending with '.com' */
void     e_sqlite_cursor_fixture_filtered_setup (EbSqlCursorFixture *fixture,
						 gconstpointer       user_data);

gchar    *new_vcard_from_test_case         (const gchar *case_name);
EContact *new_contact_from_test_case       (const gchar *case_name);

void      add_contact_from_test_case       (EbSqlFixture *fixture,
					    const gchar *case_name,
					    EContact **ret_contact);
void     assert_contacts_order_slist       (GSList      *results,
					    GSList      *uids);
void     assert_contacts_order             (GSList      *results,
					    const gchar *first_uid,
					    ...) G_GNUC_NULL_TERMINATED;

void     print_results                     (GSList      *results);

/*  Step test helpers */
void      step_test_add_assertion          (StepData    *data,
					    gint         count,
					    ...);
void      step_test_change_locale          (StepData    *data,
					    const gchar *locale,
					    gint         expected_changes);

StepData *step_test_new                    (const gchar *test_prefix,
					    const gchar *test_path,
					    const gchar *locale,
					    gboolean     store_vcards,
					    gboolean     empty_book);
StepData *step_test_new_full               (const gchar         *test_prefix,
					    const gchar         *test_path,
					    const gchar         *locale,
					    gboolean             store_vcards,
					    gboolean             empty_book,
					    EBookCursorSortType  sort_type);

void      step_test_add                    (StepData    *data,
					    gboolean     filtered);

G_END_DECLS

#endif /* DATA_TEST_UTILS_H */
