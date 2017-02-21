/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2017 Red Hat, Inc. (www.redhat.com)
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
 */

#ifndef TEST_CACHE_UTILS_H
#define TEST_CACHE_UTILS_H

#include <libedata-cal/libedata-cal.h>

G_BEGIN_DECLS

typedef enum {
	TCU_LOAD_COMPONENT_SET_NONE,
	TCU_LOAD_COMPONENT_SET_EVENTS,
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

G_END_DECLS

#endif /* TEST_CACHE_UTILS_H */
