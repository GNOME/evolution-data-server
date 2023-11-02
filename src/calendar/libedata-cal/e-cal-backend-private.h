/*
 * e-cal-backend.h
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
 */

#if !defined (__LIBEDATA_CAL_H_INSIDE__) && !defined (LIBEDATA_CAL_COMPILATION)
#error "Only <libedata-cal/libedata-cal.h> should be included directly."
#endif

#ifndef E_CAL_BACKEND_PRIVATE_H
#define E_CAL_BACKEND_PRIVATE_H

#include <gio/gio.h>
#include <libedata-cal/e-cal-backend.h>

G_BEGIN_DECLS

GSimpleAsyncResult *
		e_cal_backend_prepare_for_completion
						(ECalBackend *backend,
						 guint opid,
						 GQueue **result_queue);

G_END_DECLS

#endif /* E_CAL_BACKEND_PRIVATE_H */
