/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef E_CAL_COMPONENT_BAG_H
#define E_CAL_COMPONENT_BAG_H

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#include <libecal/e-cal-client.h>
#include <libecal/e-cal-component.h>

G_BEGIN_DECLS

#define E_TYPE_CAL_COMPONENT_BAG e_cal_component_bag_get_type ()

G_DECLARE_FINAL_TYPE (ECalComponentBag, e_cal_component_bag, E, CAL_COMPONENT_BAG, GObject)

#define E_TYPE_CAL_COMPONENT_BAG_ITEM e_cal_component_bag_item_get_type ()

typedef struct _ECalComponentBagItem {
	ECalClient *client;
	ECalComponent *comp;
	gchar *uid;
	gchar *rid;
	time_t start;
	guint64 duration_minutes;
	guint span_index; /* from 0 */

	gpointer user_data;
	GBoxedCopyFunc copy_user_data;
	GBoxedFreeFunc free_user_data;
} ECalComponentBagItem; /* a boxed type */

/**
 * ECalComponentBagForeachFunc:
 * @bag: an #ECalComponentBag
 * @item: an #ECalComponentBagItem
 * @user_data: user data passed to e_cal_component_bag_foreach()
 *
 * A callback function used by e_cal_component_bag_foreach(), called
 * for each item in the @bag. The @item is owned by the @bag.
 *
 * Returns: %TRUE, to continue traversal, %FALSE to stop
 *
 * Since: 3.58
 **/
typedef gboolean(* ECalComponentBagForeachFunc)	(ECalComponentBag *bag,
						 ECalComponentBagItem *item,
						 gpointer user_data);

GType		e_cal_component_bag_item_get_type
						(void);
ECalComponentBagItem *
		e_cal_component_bag_item_new	(ECalClient *client,
						 ECalComponent *comp,
						 guint min_duration_minutes,
						 ICalTimezone *timezone);
ECalComponentBagItem *
		e_cal_component_bag_item_copy	(const ECalComponentBagItem *self);
void		e_cal_component_bag_item_free	(ECalComponentBagItem *self);
guint		e_cal_component_bag_item_hash_by_comp
						(gconstpointer self);  /* ECalComponentBagItem */
gboolean	e_cal_component_bag_item_equal_by_comp
						(gconstpointer item1,  /* ECalComponentBagItem */
						 gconstpointer item2); /* ECalComponentBagItem */
gboolean	e_cal_component_bag_item_read_times
						(ECalComponentBagItem *self,
						 guint min_duration_minutes,
						 ICalTimezone *timezone);
void		e_cal_component_bag_item_set_user_data
						(ECalComponentBagItem *self,
						 gpointer user_data,
						 GBoxedCopyFunc copy_user_data,
						 GBoxedFreeFunc free_user_data);
ECalComponentBag *
		e_cal_component_bag_new		(void);
void		e_cal_component_bag_set_timezone(ECalComponentBag *self,
						 ICalTimezone *zone);
ICalTimezone *	e_cal_component_bag_get_timezone(ECalComponentBag *self);
void		e_cal_component_bag_set_min_duration_minutes
						(ECalComponentBag *self,
						 guint value);
guint		e_cal_component_bag_get_min_duration_minutes
						(ECalComponentBag *self);
void		e_cal_component_bag_lock	(ECalComponentBag *self);
void		e_cal_component_bag_unlock	(ECalComponentBag *self);
void		e_cal_component_bag_add		(ECalComponentBag *self,
						 ECalClient *client,
						 ECalComponent *comp);
void		e_cal_component_bag_add_with_user_data
						(ECalComponentBag *self,
						 ECalClient *client,
						 ECalComponent *comp,
						 gpointer user_data,
						 GBoxedCopyFunc copy_user_data,
						 GBoxedFreeFunc free_user_data);
void		e_cal_component_bag_remove	(ECalComponentBag *self,
						 ECalClient *client,
						 const gchar *uid,
						 const gchar *rid);
void		e_cal_component_bag_clear	(ECalComponentBag *self);
void		e_cal_component_bag_rebuild	(ECalComponentBag *self);
GPtrArray *	e_cal_component_bag_list	(ECalComponentBag *self); /* ECalComponentBagItem * */
void		e_cal_component_bag_foreach	(ECalComponentBag *self,
						 ECalComponentBagForeachFunc func,
						 gpointer user_data);
guint		e_cal_component_bag_get_n_items	(ECalComponentBag *self);
guint		e_cal_component_bag_get_n_spans	(ECalComponentBag *self);
const GPtrArray * /* ECalComponentBagItem * */
		e_cal_component_bag_get_span	(ECalComponentBag *self,
						 guint index);
GPtrArray *	e_cal_component_bag_dup_span	(ECalComponentBag *self, /* ECalComponentBagItem * */
						 guint index);
const ECalComponentBagItem *
		e_cal_component_bag_get_item	(ECalComponentBag *self,
						 ECalClient *client,
						 const gchar *uid,
						 const gchar *rid);
ECalComponentBagItem *
		e_cal_component_bag_dup_item	(ECalComponentBag *self,
						 ECalClient *client,
						 const gchar *uid,
						 const gchar *rid);

G_END_DECLS

#endif /* E_CAL_COMPONENT_BAG_H */
