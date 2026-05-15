/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEDATA_CAL_H_INSIDE__) && !defined (LIBEDATA_CAL_COMPILATION)
#error "Only <libedata-cal/libedata-cal.h> should be included directly."
#endif

#ifndef E_CAL_BACKEND_PRIVATE_H
#define E_CAL_BACKEND_PRIVATE_H

#include <gio/gio.h>
#include <libedata-cal/e-cal-backend.h>

G_BEGIN_DECLS

typedef struct _ECalQueueTuple ECalQueueTuple;

struct _ECalQueueTuple {
	GQueue first;
	GQueue second;
	GQueue third;
	GDestroyNotify first_free_func;
	GDestroyNotify second_free_func;
	GDestroyNotify third_free_func;
};

GTask *		e_cal_backend_prepare_for_completion
						(ECalBackend *backend,
						 guint opid);

void		e_cal_queue_free_strings (GQueue *queue);

ECalQueueTuple *
		e_cal_queue_tuple_new (GDestroyNotify first_free_func,
		                       GDestroyNotify second_free_func,
		                       GDestroyNotify third_free_func);

void		e_cal_queue_tuple_free (ECalQueueTuple *queue_tuple);

G_END_DECLS

#endif /* E_CAL_BACKEND_PRIVATE_H */
