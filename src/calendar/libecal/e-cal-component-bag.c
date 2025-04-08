/*
 * SPDX-FileCopyrightText: (C) 2025 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "evolution-data-server-config.h"

#include "e-cal-client.h"
#include "e-cal-component.h"
#include "e-cal-time-util.h"

#include "e-cal-component-bag.h"

/**
 * SECTION: e-cal-component-bag
 * @include: calendar/gui/e-cal-component-bag.h
 * @short_description: bag of ECalComponent-s, split into spans
 *
 * The #ECalComponentBag is a bag of the #ECalComponent-s, which sorts
 * the components into spans in a way that no two overlapping components
 * share the same span.
 *
 * The object is thread-safe, except of e_cal_component_bag_get_span().
 * If thread safety is needed, use e_cal_component_bag_dup_span().
 *
 * Since: 3.58
 **/

#define LOCK(_self) (g_rec_mutex_lock (&(_self)->lock))
#define UNLOCK(_self) (g_rec_mutex_unlock (&(_self)->lock))

struct _ECalComponentBag {
	GObject parent;

	GRecMutex lock;
	ICalTimezone *timezone;
	GPtrArray *spans; /* GPtrArray * { ECalComponentBagItem * } */
	GHashTable *items; /* ECalComponentBagItem * ~> ECalComponentBagItem *; owned by `spans` */
	guint min_duration_minutes;
};

enum {
	PROP_0,
	PROP_TIMEZONE,
	PROP_N_ITEMS,
	PROP_N_SPANS,
	N_PROPS
};

enum {
	ADDED,
	REMOVED,
	ITEM_CHANGED,
	SPAN_CHANGED,
	LAST_SIGNAL
};

static GParamSpec *properties[N_PROPS] = { NULL, };
static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (ECalComponentBag, e_cal_component_bag, G_TYPE_OBJECT)

G_DEFINE_BOXED_TYPE (ECalComponentBagItem, e_cal_component_bag_item, e_cal_component_bag_item_copy, e_cal_component_bag_item_free)

static GPtrArray *
e_cal_component_bag_hash_keys_to_array (GHashTable *hash_table,
					gboolean own_keys)
{
	GPtrArray *array;
	GHashTableIter iter;
	gpointer key;

	if (!hash_table || !g_hash_table_size (hash_table))
		return NULL;

	array = g_ptr_array_new_full (g_hash_table_size (hash_table), own_keys ? (GDestroyNotify) e_cal_component_bag_item_free : NULL);

	g_hash_table_iter_init (&iter, hash_table);
	while (g_hash_table_iter_next (&iter, &key, NULL)) {
		ECalComponentBagItem *item = key;

		g_ptr_array_add (array, item);
	}

	return array;
}

/**
 * e_cal_component_bag_item_new:
 * @client: an #ECalClient
 * @comp: an #ECalComponent
 * @min_duration_minutes: minimum duration, in minutes
 * @timezone: (nullable): an #ICalTimezone to calculate the start time for, or %NULL
 *
 * Creates a new #ECalComponentBagItem, which will have prefilled
 * all members except of those for the user data.
 *
 * The @min_duration_minutes is used to define the minimum duration, in minutes,
 * the item should have set. Any smaller duration is increased to it.
 *
 * The span_index member is always zero here.
 *
 * When the @timezone is %NULL, the UTC is assumed.
 *
 * Returns: (transfer full): a new #ECalComponentBagItem
 *
 * Since: 3.58
 **/
ECalComponentBagItem *
e_cal_component_bag_item_new (ECalClient *client,
			      ECalComponent *comp,
			      guint min_duration_minutes,
			      ICalTimezone *timezone)
{
	ECalComponentBagItem *self;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), NULL);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	if (timezone)
		g_return_val_if_fail (I_CAL_IS_TIMEZONE (timezone), NULL);
	else
		timezone = i_cal_timezone_get_utc_timezone ();

	self = g_new0 (ECalComponentBagItem, 1);
	self->client = g_object_ref (client);
	self->comp = g_object_ref (comp);
	self->uid = g_strdup (e_cal_component_get_uid (comp));
	self->rid = e_cal_component_get_recurid_as_string (comp);

	e_cal_component_bag_item_read_times (self, min_duration_minutes, timezone);

	return self;
}

/**
 * e_cal_component_bag_item_copy:
 * @self: an #ECalComponentBagItem
 *
 * Creates an independent copy of the @self. If there is set a copy_user_data,
 * then also the user_data member is copied using this function, otherwise
 * the user_data member is just carried over to the new copy.
 *
 * Returns: (transfer full): a new copy of the @self
 *
 * Since: 3.58
 **/
ECalComponentBagItem *
e_cal_component_bag_item_copy (const ECalComponentBagItem *self)
{
	ECalComponentBagItem *copy;

	if (!self)
		return NULL;

	copy = g_new0 (ECalComponentBagItem, 1);
	g_set_object (&copy->client, self->client);
	g_set_object (&copy->comp, self->comp);
	copy->uid = g_strdup (self->uid);
	copy->rid = g_strdup (self->rid);
	copy->start = self->start;
	copy->duration_minutes = self->duration_minutes;
	copy->span_index = self->span_index;

	copy->copy_user_data = self->copy_user_data;
	copy->free_user_data = self->free_user_data;

	if (self->copy_user_data)
		copy->user_data = self->copy_user_data (self->user_data);
	else
		copy->user_data = self->user_data;

	return copy;
}

/**
 * e_cal_component_bag_item_free:
 * @self: (nullable) (transfer full): an #ECalComponentBagItem
 *
 * Frees the @self. Does nothing when it's %NULL.
 *
 * Since: 3.58
 **/
void
e_cal_component_bag_item_free (ECalComponentBagItem *self)
{
	if (!self)
		return;

	g_clear_object (&self->client);
	g_clear_object (&self->comp);
	g_clear_pointer (&self->uid, g_free);
	g_clear_pointer (&self->rid, g_free);

	if (self->free_user_data)
		self->free_user_data (self->user_data);

	g_free (self);
}

/**
 * e_cal_component_bag_item_hash_by_comp:
 * @self: an #ECalComponentBagItem
 *
 * Calculates hash of the @self, considering only the client, uid and
 * rid from the @self, because the client with the component ID and
 * the recurrence ID uniquely identify the component.
 *
 * See: e_cal_component_bag_item_equal_by_comp()
 *
 * Returns: hash value for the @self's component
 *
 * Since: 3.58
 **/
guint
e_cal_component_bag_item_hash_by_comp (gconstpointer self)
{
	const ECalComponentBagItem *item = self;
	ESource *source;
	guint hash = 0;

	if (!item || !item->client || !item->uid)
		return hash;

	source = e_client_get_source (E_CLIENT (item->client));
	if (source && e_source_get_uid (source))
		hash = g_str_hash (e_source_get_uid (source));
	else
		hash = g_direct_hash (item->client);

	hash = hash ^ g_str_hash (item->uid);

	if (item->rid)
		hash = hash ^ g_str_hash (item->rid);

	return hash;
}

static gboolean
e_cal_component_bag_item_client_equal (ECalClient *client_a,
				       ECalClient *client_b)
{
	ESource *source_a, *source_b;

	if (client_a == client_b)
		return TRUE;

	source_a = e_client_get_source (E_CLIENT (client_a));
	source_b = e_client_get_source (E_CLIENT (client_b));

	return source_a == source_b || (source_a && source_b &&
		g_strcmp0 (e_source_get_uid (source_a), e_source_get_uid (source_b)) == 0);
}

/**
 * e_cal_component_bag_item_equal_by_comp:
 * @item1: the first #ECalComponentBagItem
 * @item2: the second #ECalComponentBagItem
 *
 * Returns whether the @item1 and @item2 are equal regarding
 * the component reference stored in them. Only the client,
 * uid and rid members of the items are used here.
 *
 * See: e_cal_component_bag_item_hash_by_comp()
 *
 * Returns: whether the @item1 and @item2 are equal regarding
 *    the component reference stored in them
 *
 * Since: 3.58
 **/
gboolean
e_cal_component_bag_item_equal_by_comp (gconstpointer item1,
					gconstpointer item2)
{
	const ECalComponentBagItem *item_a = item1;
	const ECalComponentBagItem *item_b = item2;

	if (!item_a || !item_b || item_a == item_b)
		return item_a == item_b;

	if (g_strcmp0 (item_a->rid, item_b->rid) != 0)
		return FALSE;

	if (g_strcmp0 (item_a->uid, item_b->uid) != 0)
		return FALSE;

	return e_cal_component_bag_item_client_equal (item_a->client, item_b->client);
}

/**
 * e_cal_component_bag_item_read_times:
 * @self: an #ECalComponentBagItem
 * @min_duration_minutes: minimum duration, in minutes
 * @timezone: (nullable): an #ICalTimezone to calculate the start time for, or %NULL
 *
 * Reads the start time and the duration from the set component,
 * returning whether any of it changed.
 *
 * The @min_duration_minutes is used to define the minimum duration, in minutes,
 * the item should have set. Any smaller duration is increased to it.
 *
 * Returns: %TRUE when any of the stored times changed, %FALSE if not
 *
 * Since: 3.58
 **/
gboolean
e_cal_component_bag_item_read_times (ECalComponentBagItem *self,
				     guint min_duration_minutes,
				     ICalTimezone *timezone)
{
	ICalComponent *icomp;
	time_t old_start;
	guint64 old_duration_minutes;

	g_return_val_if_fail (self != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_COMPONENT (self->comp), FALSE);

	icomp = e_cal_component_get_icalcomponent (self->comp);
	old_start = self->start;
	old_duration_minutes = self->duration_minutes;

	self->start = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTSTART_PROPERTY, timezone, NULL, E_TIMEZONE_CACHE (self->client), NULL);
	self->duration_minutes = 0;

	if (self->start > (time_t) 0) {
		time_t end;

		end = e_cal_util_comp_time_to_zone (icomp, I_CAL_DTEND_PROPERTY, timezone, NULL, E_TIMEZONE_CACHE (self->client), NULL);

		if (end <= (time_t) 0)
			end = e_cal_util_comp_time_to_zone (icomp, I_CAL_DUE_PROPERTY, timezone, NULL, E_TIMEZONE_CACHE (self->client), NULL);

		if (end > 0 && end > self->start)
			self->duration_minutes = (end - self->start) / 60;
	}

	if (!self->duration_minutes) {
		ICalProperty *prop;

		prop = i_cal_component_get_first_property (icomp, I_CAL_DURATION_PROPERTY);
		if (prop) {
			ICalDuration *dur;

			dur = i_cal_property_get_duration (prop);
			if (dur) {
				self->duration_minutes = i_cal_duration_as_int (dur) / 60;
				g_clear_object (&dur);
			}

			g_clear_object (&prop);
		}
	}

	if (self->duration_minutes < min_duration_minutes)
		self->duration_minutes = min_duration_minutes;

	return old_start != self->start || old_duration_minutes != self->duration_minutes;
}

/**
 * e_cal_component_bag_item_set_user_data:
 * @self: an #ECalComponentBagItem
 * @user_data: (transfer full) (nullable): custom user data, or %NULL
 * @copy_user_data: (nullable) (scope forever): a copy function for the @user_data, or %NULL
 * @free_user_data: (nullable) (scope forever): a free function for the @user_data, or %NULL
 *
 * Sets the user data members of the @self in a safe way, meaning
 * the function does not change the user data when it's the same as that
 * already set; otherwise it frees the current user data, if the free
 * function was previously provided, and the assigns the three members
 * to the self.
 *
 * The function assumes owner ship of the @user_data, optionally calling
 * the @free_user_data if provided, when the user_data instance is the same
 * as the already set.
 *
 * Since: 3.58
 **/
void
e_cal_component_bag_item_set_user_data (ECalComponentBagItem *self,
					gpointer user_data,
					GBoxedCopyFunc copy_user_data,
					GBoxedFreeFunc free_user_data)
{
	g_return_if_fail (self != NULL);

	if (self->user_data == user_data) {
		if (free_user_data)
			free_user_data (user_data);
	} else {
		if (self->free_user_data)
			self->free_user_data (self->user_data);
	}

	self->user_data = user_data;
	self->copy_user_data = copy_user_data;
	self->free_user_data = free_user_data;
}

/* ------------------------------------------------------------------------- */

static void
e_cal_component_bag_set_property (GObject *object,
				  guint prop_id,
				  const GValue *value,
				  GParamSpec *pspec)
{
	ECalComponentBag *self = E_CAL_COMPONENT_BAG (object);

	switch (prop_id) {
	case PROP_TIMEZONE:
		e_cal_component_bag_set_timezone (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
e_cal_component_bag_get_property (GObject *object,
				  guint prop_id,
				  GValue *value,
				  GParamSpec *pspec)
{
	ECalComponentBag *self = E_CAL_COMPONENT_BAG (object);

	switch (prop_id) {
	case PROP_TIMEZONE:
		g_value_set_object (value, e_cal_component_bag_get_timezone (self));
		break;
	case PROP_N_ITEMS:
		g_value_set_uint (value, e_cal_component_bag_get_n_items (self));
		break;
	case PROP_N_SPANS:
		g_value_set_uint (value, e_cal_component_bag_get_n_spans (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
e_cal_component_bag_finalize (GObject *object)
{
	ECalComponentBag *self = E_CAL_COMPONENT_BAG (object);

	g_rec_mutex_clear (&self->lock);
	g_clear_pointer (&self->spans, g_ptr_array_unref);
	g_clear_pointer (&self->items, g_hash_table_unref);
	g_clear_object (&self->timezone);

	G_OBJECT_CLASS (e_cal_component_bag_parent_class)->finalize (object);
}

static void
e_cal_component_bag_class_init (ECalComponentBagClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->set_property = e_cal_component_bag_set_property;
	object_class->get_property = e_cal_component_bag_get_property;
	object_class->finalize = e_cal_component_bag_finalize;

	/**
	 * ECalComponentBag:n-items:
	 *
	 * Total count of the components in all spans. The property can be
	 * only read. It emits a notification when it changes.
	 *
	 * Since: 3.58
	 **/
	properties[PROP_N_ITEMS] = g_param_spec_uint ("n-items", NULL, NULL,
		0, G_MAXUINT32, 0,
		G_PARAM_READABLE |
		G_PARAM_STATIC_STRINGS |
		G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * ECalComponentBag:n-spans:
	 *
	 * How many spans the components are divided to. The property can be
	 * only read. It emits a notification when it changes.
	 *
	 * Since: 3.58
	 **/
	properties[PROP_N_SPANS] = g_param_spec_uint ("n-spans", NULL, NULL,
		0, G_MAXUINT32, 0,
		G_PARAM_READABLE |
		G_PARAM_STATIC_STRINGS |
		G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * ECalComponentBag:timezone:
	 *
	 * An #ICalTimezone to calculate component times in.
	 *
	 * Since: 3.58
	 **/
	properties[PROP_TIMEZONE] = g_param_spec_object ("timezone", NULL, NULL,
		I_CAL_TYPE_TIMEZONE,
		G_PARAM_READWRITE |
		G_PARAM_STATIC_STRINGS |
		G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (properties), properties);

	/*
	   void  (* added)		(ECalComponentBag *bag,
					 ECalComponentBagItem *item,
					 gpointer user_data);
	 */
	/**
	 * ECalComponentBag::added:
	 * @bag: an #ECalComponentBag
	 * @item: (type ECalComponentBagItem): an #ECalComponentBagItem
	 *
	 * A signal emitted whenever a new #ECalComponentBagItem is added.
	 *
	 * The @item can be modified, but if any properties related to the layout
	 * changes then the content needs to be recalculated, for example
	 * by calling e_cal_component_bag_rebuild().
	 *
	 * Since: 3.58
	 **/
	signals[ADDED] = g_signal_new (
		"added",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		0,
		NULL, NULL,
		NULL,
		G_TYPE_NONE, 1, G_TYPE_POINTER /* not E_TYPE_CAL_COMPONENT_BAG_ITEM, to be able to modify the content in the signal handler */);

	/*
	   void  (* removed)		(ECalComponentBag *bag,
					 GPtrArray *items, // (element-type ECalComponentBagItem): only borrowed, do not modify - neither the array content, nor the items, except of its user_data
					 gpointer user_data);
	 */
	/**
	 * ECalComponentBag::removed:
	 * @bag: an #ECalComponentBag
	 * @items: (element-type ECalComponentBagItem): a %GPtrArray with removed #ECalComponentBagItem structures
	 *
	 * A signal emitted when one or more items are removed from the @bag.
	 * This is not called for e_cal_component_bag_clear()
	 *
	 * Since: 3.58
	 **/
	signals[REMOVED] = g_signal_new (
		"removed",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		0,
		NULL, NULL,
		NULL,
		G_TYPE_NONE, 1, G_TYPE_PTR_ARRAY);

	/*
	   void  (* item_changed)	(ECalComponentBag *bag,
					 ECalComponentBagItem *item,
					 gpointer user_data);
	 */
	/**
	 * ECalComponentBag::item-changed:
	 * @bag: an #ECalComponentBag
	 * @item: (type ECalComponentBagItem): an #ECalComponentBagItem
	 *
	 * A signal emitted whenever an existing @item in the @bag is changed.
	 * It's a complement signal for the ECalComponentBag::added signal,
	 * in a sense that when adding an item to the @bag either the added
	 * or the item-changed is emitted, if the component changed.
	 *
	 * The @item can be modified, but if any properties related to the layout
	 * changes then the content needs to be recalculated, for example
	 * by calling e_cal_component_bag_rebuild().
	 *
	 * Since: 3.58
	 **/
	signals[ITEM_CHANGED] = g_signal_new (
		"item-changed",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		0,
		NULL, NULL,
		NULL,
		G_TYPE_NONE, 1, G_TYPE_POINTER /* not E_TYPE_CAL_COMPONENT_BAG_ITEM, to be able to modify the content in the signal handler */);

	/*
	   void  (* span_changed)	(ECalComponentBag *bag,
					 GPtrArray *items, // (element-type ECalComponentBagItem): only borrowed, do not modify - neither the array content, nor the items, except of its user_data
					 gpointer user_data);
	 */
	/**
	 * ECalComponentBag::span-changed:
	 * @bag: an #ECalComponentBag
	 * @items: (element-type ECalComponentBagItem): a #GPtrArray with #ECalComponentBagItem whose span changed
	 *
	 * A signal emitted with an array of #ECalComponentBagItem structures,
	 * whose span changed.
	 *
	 * The respective items can be modified, but if any properties related
	 * to the layout changes then the content needs to be recalculated,
	 * for example by calling e_cal_component_bag_rebuild().
	 *
	 * Since: 3.58
	 **/
	signals[SPAN_CHANGED] = g_signal_new (
		"span-changed",
		G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		0,
		NULL, NULL,
		NULL,
		G_TYPE_NONE, 1, G_TYPE_PTR_ARRAY);
}

static void
e_cal_component_bag_init (ECalComponentBag *self)
{
	g_rec_mutex_init (&self->lock);
	self->spans = g_ptr_array_new_with_free_func ((GDestroyNotify) g_ptr_array_unref);
	self->items = g_hash_table_new (e_cal_component_bag_item_hash_by_comp, e_cal_component_bag_item_equal_by_comp);
}

/**
 * e_cal_component_bag_new:
 *
 * Creates a new #ECalComponentBag.
 *
 * Returns: (transfer full): a new #ECalComponentBag
 *
 * Since: 3.58
 **/
ECalComponentBag *
e_cal_component_bag_new (void)
{
	return g_object_new (E_TYPE_CAL_COMPONENT_BAG, NULL);
}

/**
 * e_cal_component_bag_set_timezone:
 * @self: an #ECalComponentBag
 * @zone: an #ICalTimezone
 *
 * Sets the @zone as a time zone to be used to calculate component times in.
 *
 * Since: 3.58
 **/
void
e_cal_component_bag_set_timezone (ECalComponentBag *self,
				  ICalTimezone *zone)
{
	g_return_if_fail (E_IS_CAL_COMPONENT_BAG (self));
	g_return_if_fail (I_CAL_IS_TIMEZONE (zone));

	if (g_set_object (&self->timezone, zone))
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIMEZONE]);
}

/**
 * e_cal_component_bag_get_timezone:
 * @self: an #ECalComponentBag
 *
 * Gets an #ICalTimezone used to calculate component times in.
 *
 * Returns: (transfer none) (nullable): an #ICalTimezone used to calculate component times in,
 *    or %NULL, when not set. In such case UTC is used.
 *
 * Since: 3.58
 **/
ICalTimezone *
e_cal_component_bag_get_timezone (ECalComponentBag *self)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT_BAG (self), NULL);

	return self->timezone;
}

/**
 * e_cal_component_bag_set_min_duration_minutes:
 * @self: an #ECalComponentBag
 * @value: a value to set
 *
 * Sets minimum duration to be used for the components, in minutes.
 * When the component duration is less than this @value, it is
 * increased to it. The default value is 0.
 *
 * Changing the value does not influence already added components.
 * Use e_cal_component_bag_rebuild() to rebuild the content.
 *
 * Since: 3.58
 **/
void
e_cal_component_bag_set_min_duration_minutes (ECalComponentBag *self,
					      guint value)
{
	g_return_if_fail (E_IS_CAL_COMPONENT_BAG (self));

	if (self->min_duration_minutes == value)
		return;

	self->min_duration_minutes = value;
}

/**
 * e_cal_component_bag_get_min_duration_minutes:
 * @self: an #ECalComponentBag
 *
 * Gets minimum duration, in minutes, previously set
 * by the e_cal_component_bag_set_min_duration_minutes().
 *
 * Returns: minimum duration, in minutes
 *
 * Since: 3.58
 **/
guint
e_cal_component_bag_get_min_duration_minutes (ECalComponentBag *self)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT_BAG (self), 0);

	return self->min_duration_minutes;
}

/**
 * e_cal_component_bag_lock:
 * @self: an #ECalComponentBag
 *
 * Locks the @self, to prevent changes from other threads. It can
 * be called multiple times, only each call needs its pair call
 * of the e_cal_component_bag_unlock().
 *
 * Since: 3.58
 **/
void
e_cal_component_bag_lock (ECalComponentBag *self)
{
	g_return_if_fail (E_IS_CAL_COMPONENT_BAG (self));

	LOCK (self);
}

/**
 * e_cal_component_bag_unlock:
 * @self: an #ECalComponentBag
 *
 * Unlocks the @self, previously locked by the e_cal_component_bag_unlock().
 *
 * Since: 3.58
 **/
void
e_cal_component_bag_unlock (ECalComponentBag *self)
{
	g_return_if_fail (E_IS_CAL_COMPONENT_BAG (self));

	UNLOCK (self);
}

/* should hold the lock when calling this */
static void
e_cal_component_place_item_locked (ECalComponentBag *self,
				   ECalComponentBagItem *item,
				   gboolean is_new,
				   GPtrArray **inout_span_changed) /* ECalComponentBagItem * */
{
	GPtrArray *span_items; /* ECalComponentBagItem * */
	guint ii, jj, previous_span_index = item->span_index;
	gboolean added = FALSE;

	if (!is_new) {
		*inout_span_changed = g_ptr_array_new ();
		span_items = g_ptr_array_index (self->spans, item->span_index);

		jj = span_items->len;
		g_warn_if_fail (g_ptr_array_find (span_items, item, &jj));
		item = g_ptr_array_steal_index (span_items, jj);

		if (!span_items->len) {
			for (jj = item->span_index + 1; jj < self->spans->len; jj++) {
				span_items = g_ptr_array_index (self->spans, jj);
				for (ii = 0; ii < span_items->len; ii++) {
					ECalComponentBagItem *existing = g_ptr_array_index (span_items, ii);

					existing->span_index--;
					g_ptr_array_add (*inout_span_changed, existing);
				}
			}
		}
	}

	for (jj = 0; jj < self->spans->len; jj++) {
		gint insert_index = -1;

		added = TRUE;

		span_items = g_ptr_array_index (self->spans, jj);

		/* check whether the `item` won't collide with any other in this span */
		for (ii = 0; ii < span_items->len; ii++) {
			ECalComponentBagItem *existing = g_ptr_array_index (span_items, ii);

			if (existing->start < item->start + (item->duration_minutes * 60) &&
			    existing->start + (existing->duration_minutes * 60) > item->start) {
				added = FALSE;
				break;
			}

			if (existing->start < item->start)
				insert_index = ii + 1;
		}

		if (added) {
			item->span_index = jj;
			g_ptr_array_insert (span_items, insert_index, item);

			if (is_new)
				g_hash_table_insert (self->items, item, item);
			break;
		}
	}

	if (!added) {
		item->span_index = self->spans->len;
		span_items = g_ptr_array_new_with_free_func ((GDestroyNotify) e_cal_component_bag_item_free);
		g_ptr_array_add (self->spans, span_items);
		g_ptr_array_add (span_items, item);

		if (is_new)
			g_hash_table_insert (self->items, item, item);
	}

	if (*inout_span_changed) {
		if (item->span_index != previous_span_index)
			g_ptr_array_add (*inout_span_changed, item);
	}
}

/* should hold the lock when calling this */
static ECalComponentBagItem *
e_cal_component_bag_get_existing_str_locked (ECalComponentBag *self,
					     ECalClient *client,
					     const gchar *uid,
					     const gchar *rid)
{
	ECalComponentBagItem *existing, item_lookup = { 0, };

	item_lookup.client = client;
	item_lookup.uid = (gchar *) uid;
	item_lookup.rid = (gchar *) rid;

	existing = g_hash_table_lookup (self->items, &item_lookup);

	return existing;
}

/* should hold the lock when calling this */
static ECalComponentBagItem *
e_cal_component_bag_get_existing_locked (ECalComponentBag *self,
					 ECalClient *client,
					 ECalComponent *comp)
{
	ECalComponentBagItem *existing;
	gchar *rid;

	rid = e_cal_component_get_recurid_as_string (comp);

	existing = e_cal_component_bag_get_existing_str_locked (self, client, e_cal_component_get_uid (comp), rid);

	g_free (rid);

	return existing;
}

/**
 * e_cal_component_bag_add:
 * @self: an #ECalComponentBag
 * @client: an #ECalClient
 * @comp: an #ECalComponent
 *
 * Adds a component into the bag. It's possible to add the same component
 * from the same client multiple times, in which case any previous occurrence
 * is removed.
 *
 * The component is identified by the client and its uid/rid.
 *
 * Note this unsets any previously set user data on the item
 * by the e_cal_component_bag_add_with_user_data().
 *
 * Since: 3.58
 **/
void
e_cal_component_bag_add (ECalComponentBag *self,
			 ECalClient *client,
			 ECalComponent *comp)
{
	g_return_if_fail (E_IS_CAL_COMPONENT_BAG (self));
	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));

	e_cal_component_bag_add_with_user_data (self, client, comp, NULL, NULL, NULL);
}

/**
 * e_cal_component_bag_add_with_user_data:
 * @self: an #ECalComponentBag
 * @client: an #ECalClient
 * @comp: an #ECalComponent
 * @user_data: (transfer full) (nullable): custom user data, or %NULL
 * @copy_user_data: (nullable) (scope forever): a copy function for the @user_data, or %NULL
 * @free_user_data: (nullable) (scope forever): a free function for the @user_data, or %NULL
 *
 * Adds a component into the bag. It's possible to add the same component
 * from the same client multiple times, in which case any previous occurrence
 * is removed.
 *
 * The component is identified by the client and its uid/rid.
 *
 * The function also sets the user data member of a newly created
 * #ECalComponentBagItem. The @user_data is replaced in an existing
 * item only if it's set (non-%NULL), otherwise it's left unchanged.
 * See e_cal_component_bag_item_set_user_data() for more information.
 *
 * Since: 3.58
 **/
void
e_cal_component_bag_add_with_user_data (ECalComponentBag *self,
					ECalClient *client,
					ECalComponent *comp,
					gpointer user_data,
					GBoxedCopyFunc copy_user_data,
					GBoxedFreeFunc free_user_data)
{
	ECalComponentBagItem *item;
	GPtrArray *span_items; /* ECalComponentBagItem * */
	GPtrArray *span_changed = NULL; /* ECalComponentBagItem * */
	guint ii, spans_before;

	g_return_if_fail (E_IS_CAL_COMPONENT_BAG (self));
	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));

	LOCK (self);

	spans_before = self->spans->len;

	item = e_cal_component_bag_get_existing_locked (self, client, comp);
	if (item) {
		gboolean item_changed = FALSE;

		/* the instances can change, but still reference the same item;
		   the UID and the recurid cannot change */
		if (g_set_object (&item->client, client))
			item_changed = TRUE;

		if (g_set_object (&item->comp, comp))
			item_changed = TRUE;

		if (e_cal_component_bag_item_read_times (item, self->min_duration_minutes, self->timezone)) {
			gboolean can_keep = TRUE;

			item_changed = TRUE;

			/* check whether the `item` won't collide with any other in its current span */
			span_items = g_ptr_array_index (self->spans, item->span_index);

			for (ii = 0; ii < span_items->len; ii++) {
				ECalComponentBagItem *existing = g_ptr_array_index (span_items, ii);

				if (existing != item &&
				    existing->start < item->start + (item->duration_minutes * 60) &&
				    existing->start + (existing->duration_minutes * 60) > item->start) {
					can_keep = FALSE;
					break;
				}
			}

			if (!can_keep)
				e_cal_component_place_item_locked (self, item, FALSE, &span_changed);
		}

		/* only if the user data changes */
		if (user_data)
			e_cal_component_bag_item_set_user_data (item, user_data, copy_user_data, free_user_data);

		if (item_changed)
			g_signal_emit (self, signals[ITEM_CHANGED], 0, item);
	} else {
		item = e_cal_component_bag_item_new (client, comp, self->min_duration_minutes, self->timezone);

		e_cal_component_place_item_locked (self, item, TRUE, &span_changed);

		e_cal_component_bag_item_set_user_data (item, user_data, copy_user_data, free_user_data);

		g_signal_emit (self, signals[ADDED], 0, item);

		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_ITEMS]);
	}

	if (self->spans->len != spans_before)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_SPANS]);

	if (span_changed) {
		if (span_changed->len)
			g_signal_emit (self, signals[SPAN_CHANGED], 0, span_changed);

		g_ptr_array_unref (span_changed);
	}

	UNLOCK (self);
}

/**
 * e_cal_component_bag_remove:
 * @self: an #ECalComponentBag
 * @client: an #ECalClient
 * @uid: a unique identifier of the component to remove
 * @rid: (nullable): a recurrence ID of an instance, or %NULL to remove all instances
 *
 * Removes a component from the bag. The function does nothing
 * when the component is not part of the bag.
 *
 * Since: 3.58
 **/
void
e_cal_component_bag_remove (ECalComponentBag *self,
			    ECalClient *client,
			    const gchar *uid,
			    const gchar *rid)
{
	GHashTable *removed;
	GHashTable *span_changed = NULL;
	GPtrArray *array;
	ECalComponentBagItem *item;
	ECalComponentBagItem lookup_item = { 0, };
	guint ii, jj, spans_removed = 0;

	g_return_if_fail (E_IS_CAL_COMPONENT_BAG (self));
	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (uid != NULL);

	removed = g_hash_table_new (g_direct_hash, g_direct_equal);

	if (rid && !*rid)
		rid = NULL;

	LOCK (self);

	lookup_item.client = client;
	lookup_item.uid = (gchar *) uid;
	lookup_item.rid = (gchar *) rid;

	item = g_hash_table_lookup (self->items, &lookup_item);
	/* when it's a recurring event, without recurid, then needs to traverse everything,
	   to remove each occurrence */
	if (item && (rid || e_cal_component_has_recurrences (item->comp))) {
		guint span_index = item->span_index;
		GPtrArray *span_items = g_ptr_array_index (self->spans, span_index);

		for (ii = 0; ii < span_items->len; ii++) {
			ECalComponentBagItem *existing = g_ptr_array_index (span_items, ii);

			if (existing == item) {
				g_hash_table_remove (self->items, item);
				g_hash_table_add (removed, g_ptr_array_steal_index (span_items, ii));
				ii--;

				if (span_changed)
					g_hash_table_remove (span_changed, item);
			} else if (spans_removed) {
				if (!span_changed)
					span_changed = g_hash_table_new (g_direct_hash, g_direct_equal);

				existing->span_index -= spans_removed;
				g_hash_table_add (span_changed, existing);
			}
		}

		if (!span_items->len) {
			spans_removed++;

			g_ptr_array_remove_index (self->spans, span_index);

			for (jj = span_index; jj < self->spans->len; jj++) {
				span_items = g_ptr_array_index (self->spans, jj);

				for (ii = 0; ii < span_items->len; ii++) {
					ECalComponentBagItem *existing = g_ptr_array_index (span_items, ii);

					if (!span_changed)
						span_changed = g_hash_table_new (g_direct_hash, g_direct_equal);

					existing->span_index -= spans_removed;
					g_hash_table_add (span_changed, existing);
				}
			}
		}
	} else {
		for (jj = 0; jj < self->spans->len; jj++) {
			GPtrArray *span_items = g_ptr_array_index (self->spans, jj);

			for (ii = 0; ii < span_items->len; ii++) {
				item = g_ptr_array_index (span_items, ii);

				if (e_cal_component_bag_item_client_equal (item->client, client) &&
				    g_strcmp0 (item->uid, uid) == 0 &&
				    (!rid || g_strcmp0 (item->rid, rid) == 0)) {
					g_hash_table_remove (self->items, item);
					g_hash_table_add (removed, g_ptr_array_steal_index (span_items, ii));
					ii--;

					if (span_changed)
						g_hash_table_remove (span_changed, item);
				} else if (spans_removed) {
					if (!span_changed)
						span_changed = g_hash_table_new (g_direct_hash, g_direct_equal);

					item->span_index -= spans_removed;
					g_hash_table_add (span_changed, item);
				}
			}

			if (!span_items->len) {
				spans_removed++;

				g_ptr_array_remove_index (self->spans, jj);
				jj--;
			}
		}
	}

	array = e_cal_component_bag_hash_keys_to_array (removed, TRUE);
	if (array) {
		g_signal_emit (self, signals[REMOVED], 0, array);
		g_ptr_array_unref (array);

		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_ITEMS]);
	}

	array = e_cal_component_bag_hash_keys_to_array (span_changed, FALSE);
	if (array) {
		g_signal_emit (self, signals[SPAN_CHANGED], 0, array);
		g_ptr_array_unref (array);
	}

	if (spans_removed)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_SPANS]);

	UNLOCK (self);

	g_clear_pointer (&span_changed, g_hash_table_unref);
	g_clear_pointer (&removed, g_hash_table_unref);
}

/**
 * e_cal_component_bag_clear:
 * @self: an #ECalComponentBag
 *
 * Removes all components from the bag.
 *
 * This does not emit the ECalComponentBag::removed for each span,
 * but it notifies about a change in the ECalComponentBag:n-spans
 * and in the ECalComponentBag:n-items properties, which will be zero.
 *
 * Since: 3.58
 **/
void
e_cal_component_bag_clear (ECalComponentBag *self)
{
	g_return_if_fail (E_IS_CAL_COMPONENT_BAG (self));

	LOCK (self);

	if (self->spans->len) {
		g_hash_table_remove_all (self->items);
		g_ptr_array_remove_range (self->spans, 0, self->spans->len);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_ITEMS]);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_SPANS]);
	}

	UNLOCK (self);
}

/**
 * e_cal_component_bag_rebuild:
 * @self: an #ECalComponentBag
 *
 * Rebuilds the spans with the current content, like if all
 * the items are added from scratch.
 *
 * Since: 3.58
 **/
void
e_cal_component_bag_rebuild (ECalComponentBag *self)
{
	GPtrArray *items;
	guint previous_n_spans;

	g_return_if_fail (E_IS_CAL_COMPONENT_BAG (self));

	LOCK (self);

	previous_n_spans = self->spans->len;
	items = e_cal_component_bag_list (self);

	if (items && items->len) {
		GPtrArray *span_changed = g_ptr_array_new ();
		guint ii;

		g_hash_table_remove_all (self->items);
		g_ptr_array_remove_range (self->spans, 0, self->spans->len);

		for (ii = 0; ii < items->len; ii++) {
			ECalComponentBagItem *item = g_ptr_array_index (items, ii);

			/* steal the item */
			items->pdata[ii] = NULL;

			e_cal_component_bag_item_read_times (item, self->min_duration_minutes, self->timezone);
			e_cal_component_place_item_locked (self, item, TRUE, &span_changed);
		}

		if (span_changed->len)
			g_signal_emit (self, signals[SPAN_CHANGED], 0, span_changed);

		g_ptr_array_unref (span_changed);
	}

	if (previous_n_spans != self->spans->len)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_SPANS]);

	UNLOCK (self);

	g_clear_pointer (&items, g_ptr_array_unref);
}

/**
 * e_cal_component_bag_list:
 * @self: an #ECalComponentBag
 *
 * Lists all components stored in the @self. The items are of the #ECalComponentBagItem type.
 * Even only the container is needed to be freed, the items are owned by the container and
 * can be modified in any way without influencing the @self.
 *
 * Returns: (transfer container) (element-type ECalComponentBagItem): a #GPtrArray containing
 *    all the currently stored components in the bag as #ECalComponentBagItem objects.
 *
 * Since: 3.58
 **/
GPtrArray *
e_cal_component_bag_list (ECalComponentBag *self)
{
	GPtrArray *items;
	guint ii, jj, total = 0;

	g_return_val_if_fail (E_IS_CAL_COMPONENT_BAG (self), NULL);

	LOCK (self);

	for (jj = 0; jj < self->spans->len; jj++) {
		GPtrArray *span_items = g_ptr_array_index (self->spans, jj);

		total += span_items->len;
	}

	items = g_ptr_array_new_full (total, (GDestroyNotify) e_cal_component_bag_item_free);

	for (jj = 0; jj < self->spans->len; jj++) {
		GPtrArray *span_items = g_ptr_array_index (self->spans, jj);

		for (ii = 0; ii < span_items->len; ii++) {
			ECalComponentBagItem *item = g_ptr_array_index (span_items, ii);

			g_ptr_array_add (items, e_cal_component_bag_item_copy (item));
		}
	}

	UNLOCK (self);

	return items;
}

/**
 * e_cal_component_bag_foreach:
 * @self: an #ECalComponentBag
 * @func: (scope call) (closure user_data): an #ECalComponentBagForeachFunc to call
 * @user_data: user data passed to the @func
 *
 * Calls the @func for each item in the @self, or until the @func returns %FALSE,
 * to stop the traversal earlier.
 *
 * Since: 3.58
 **/
void
e_cal_component_bag_foreach (ECalComponentBag *self,
			     ECalComponentBagForeachFunc func,
			     gpointer user_data)
{
	guint ii, jj;

	g_return_if_fail (E_IS_CAL_COMPONENT_BAG (self));
	g_return_if_fail (func != NULL);

	LOCK (self);

	for (jj = 0; jj < self->spans->len; jj++) {
		GPtrArray *span_items = g_ptr_array_index (self->spans, jj);
		gboolean can_continue = TRUE;

		for (ii = 0; ii < span_items->len; ii++) {
			ECalComponentBagItem *item = g_ptr_array_index (span_items, ii);

			can_continue = func (self, item, user_data);
			if (!can_continue)
				break;
		}

		if (!can_continue)
			break;
	}

	UNLOCK (self);
}

/**
 * e_cal_component_bag_get_n_items:
 * @self: an #ECalComponentBag
 *
 * Gets how many items are in the bag across all the spans.
 *
 * Returns: how many items are in the bag across all the spans
 *
 * Since: 3.58
 **/
guint
e_cal_component_bag_get_n_items (ECalComponentBag *self)
{
	guint n_items;

	g_return_val_if_fail (E_IS_CAL_COMPONENT_BAG (self), 0);

	LOCK (self);
	n_items = g_hash_table_size (self->items);
	UNLOCK (self);

	return n_items;
}

/**
 * e_cal_component_bag_get_n_spans:
 * @self: an #ECalComponentBag
 *
 * Gets how many spans all the components in the @self occupy.
 *
 * Returns: how many spans all the components in the @self occupy
 *
 * Since: 3.58
 **/
guint
e_cal_component_bag_get_n_spans (ECalComponentBag *self)
{
	guint n_spans;

	g_return_val_if_fail (E_IS_CAL_COMPONENT_BAG (self), 0);

	LOCK (self);
	n_spans = self->spans->len;
	UNLOCK (self);

	return n_spans;
}

/**
 * e_cal_component_bag_get_span:
 * @self: an #ECalComponentBag
 * @index: span index, counting from 0
 *
 * Peeks a current span content. The @index should be between 0
 * and one less than e_cal_component_bag_get_n_spans().
 *
 * Unlike the e_cal_component_bag_list(), the returned array is
 * owned by the @self and is valid until the next content change
 * (addition, removal, clear) of the @self is done.
 *
 * If thread safety is required, use e_cal_component_bag_dup_span()
 * instead.
 *
 * Returns: (transfer none) (nullable) (element-type ECalComponentBagItem):
 *    an array of #ECalComponentBag in the @index span, or %NULL when
 *    the @index is out of bounds
 *
 * Since: 3.58
 **/
const GPtrArray *
e_cal_component_bag_get_span (ECalComponentBag *self,
			      guint index)
{
	const GPtrArray *array;

	g_return_val_if_fail (E_IS_CAL_COMPONENT_BAG (self), NULL);

	LOCK (self);

	if (index < self->spans->len)
		array = g_ptr_array_index (self->spans, index);
	else
		array = NULL;

	UNLOCK (self);

	return array;
}

/**
 * e_cal_component_bag_dup_span:
 * @self: an #ECalComponentBag
 * @index: span index, counting from 0
 *
 * Similar to the e_cal_component_bag_get_span(),
 * except the returned array is a copy of the span,
 * thus it is thread safe.
 *
 * Returns: (transfer container) (nullable) (element-type ECalComponentBagItem):
 *    a copy of a span at @index, or %NULL, when @index is out of bounds
 *
 * Since: 3.58
 **/
GPtrArray *
e_cal_component_bag_dup_span (ECalComponentBag *self,
			      guint index)
{
	GPtrArray *array;

	g_return_val_if_fail (E_IS_CAL_COMPONENT_BAG (self), NULL);

	LOCK (self);

	if (index < self->spans->len) {
		GPtrArray *span_items;
		guint ii;

		span_items = g_ptr_array_index (self->spans, index);
		array = g_ptr_array_new_full (span_items->len, (GDestroyNotify) e_cal_component_bag_item_free);

		for (ii = 0; ii < span_items->len; ii++) {
			ECalComponentBagItem *item = g_ptr_array_index (span_items, ii);

			g_ptr_array_add (array, e_cal_component_bag_item_copy (item));
		}
	} else {
		array = NULL;
	}

	UNLOCK (self);

	return array;
}

/**
 * e_cal_component_bag_get_item:
 * @self: an #ECalComponentBag
 * @client: an #ECalClient
 * @uid: a component UID
 * @rid: (nullable): a component recurrence ID, or %NULL
 *
 * Looks up for an item identified by the @client, @uid and optionally @rid,
 * and returns it.
 *
 * This is a not thread safe, it's valid until the next change of it
 * is received by the @self. If thread safety is required, use the e_cal_component_bag_dup_item()
 * instead.
 *
 * Returns: (transfer none) (nullable): a stored #ECalComponentBagItem, or %NULL when not found
 *
 * Since: 3.58
 **/
const ECalComponentBagItem *
e_cal_component_bag_get_item (ECalComponentBag *self,
			      ECalClient *client,
			      const gchar *uid,
			      const gchar *rid)
{
	ECalComponentBagItem *item;

	g_return_val_if_fail (E_IS_CAL_COMPONENT_BAG (self), NULL);
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	LOCK (self);

	item = e_cal_component_bag_get_existing_str_locked (self, client, uid, rid);

	UNLOCK (self);

	return item;
}

/**
 * e_cal_component_bag_dup_item:
 * @self: an #ECalComponentBag
 * @client: an #ECalClient
 * @uid: a component UID
 * @rid: (nullable): a component recurrence ID, or %NULL
 *
 * Looks up for an item identified by the @client, @uid and optionally @rid,
 * and returns a copy of it. Free the returned item with e_cal_component_bag_item_free(),
 * when no longer needed
 *
 * This is a thread safe variant of the e_cal_component_bag_get_item().
 *
 * Returns: (transfer full) (nullable): an #ECalComponentBagItem copy of a stored
 *    item, or %NULL when not found
 *
 * Since: 3.58
 **/
ECalComponentBagItem *
e_cal_component_bag_dup_item (ECalComponentBag *self,
			      ECalClient *client,
			      const gchar *uid,
			      const gchar *rid)
{
	ECalComponentBagItem *item;

	g_return_val_if_fail (E_IS_CAL_COMPONENT_BAG (self), NULL);
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	LOCK (self);

	item = e_cal_component_bag_get_existing_str_locked (self, client, uid, rid);
	if (item)
		item = e_cal_component_bag_item_copy (item);

	UNLOCK (self);

	return item;
}
