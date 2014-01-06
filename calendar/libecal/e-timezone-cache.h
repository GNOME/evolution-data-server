/*
 * e-timezone-cache.h
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

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_TIMEZONE_CACHE_H
#define E_TIMEZONE_CACHE_H

#include <glib-object.h>
#include <libical/ical.h>

/* Standard GObject macros */
#define E_TYPE_TIMEZONE_CACHE \
	(e_timezone_cache_get_type ())
#define E_TIMEZONE_CACHE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_TIMEZONE_CACHE, ETimezoneCache))
#define E_IS_TIMEZONE_CACHE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_TIMEZONE_CACHE))
#define E_TIMEZONE_CACHE_GET_INTERFACE(obj) \
	(G_TYPE_INSTANCE_GET_INTERFACE \
	((obj), E_TYPE_TIMEZONE_CACHE, ETimezoneCacheInterface))

G_BEGIN_DECLS

/**
 * ETimezoneCache:
 *
 * Since: 3.8
 **/
typedef struct _ETimezoneCache ETimezoneCache;
typedef struct _ETimezoneCacheInterface ETimezoneCacheInterface;

/**
 * ETimezoneCacheInterface:
 *
 * Since: 3.8
 **/
struct _ETimezoneCacheInterface {
	GTypeInterface parent_interface;

	/* Methods */
	void		(*add_timezone)		(ETimezoneCache *cache,
						 icaltimezone *zone);
	icaltimezone *	(*get_timezone)		(ETimezoneCache *cache,
						 const gchar *tzid);
	GList *		(*list_timezones)	(ETimezoneCache *cache);

	/* Signals */
	void		(*timezone_added)	(ETimezoneCache *cache,
						 icaltimezone *zone);
};

GType		e_timezone_cache_get_type	(void) G_GNUC_CONST;
void		e_timezone_cache_add_timezone	(ETimezoneCache *cache,
						 icaltimezone *zone);
icaltimezone *	e_timezone_cache_get_timezone	(ETimezoneCache *cache,
						 const gchar *tzid);
GList *		e_timezone_cache_list_timezones	(ETimezoneCache *cache);

G_END_DECLS

#endif /* E_TIMEZONE_CACHE_H */

