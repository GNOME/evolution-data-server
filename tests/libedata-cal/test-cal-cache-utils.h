/*
 * SPDX-FileCopyrightText: (C) 2017 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef TEST_CACHE_UTILS_H
#define TEST_CACHE_UTILS_H

#include <libedata-cal/libedata-cal.h>

G_BEGIN_DECLS

void		tcu_read_args				(gint argc,
							 gchar **argv);

typedef enum {
	TCU_LOAD_COMPONENT_SET_NONE,
	TCU_LOAD_COMPONENT_SET_EVENTS,
	TCU_LOAD_COMPONENT_SET_EVENTS_WITH_0,
	TCU_LOAD_COMPONENT_SET_TASKS
} TCULoadComponentSet;

typedef struct {
	ECalCache *cal_cache;
} TCUFixture;

typedef struct {
	TCULoadComponentSet load_set;
} TCUClosure;

void		tcu_fixture_setup			(TCUFixture *fixture,
							 gconstpointer user_data);
void		tcu_fixture_teardown			(TCUFixture *fixture,
							 gconstpointer user_data);

gchar *		tcu_new_icalstring_from_test_case	(const gchar *case_name);
ECalComponent *	tcu_new_component_from_test_case	(const gchar *case_name);
void		tcu_add_component_from_test_case	(TCUFixture *fixture,
							 const gchar *case_name,
							 ECalComponent **out_component);
gchar *		tcu_get_test_case_filename		(const gchar *case_name);

G_END_DECLS

#endif /* TEST_CACHE_UTILS_H */
