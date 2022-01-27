/* Evolution calendar - iCalendar component object
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 */

/**
 * SECTION:e-cal-component
 * @short_description: A convenience interface for interacting with events
 * @include: libecal/libecal.h
 *
 * This is the main user facing interface used for representing an event
 * or other component in a given calendar.
 **/

#include "evolution-data-server-config.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include <libedataserver/libedataserver.h>

#include "e-cal-component.h"
#include "e-cal-time-util.h"
#include "e-cal-util.h"

#ifdef G_OS_WIN32
#define getgid() 0
#define getppid() 0
#endif

struct _ECalComponentPrivate {
	/* The icalcomponent we wrap */
	ICalComponent *icalcomp;

	/* Whether we should increment the sequence number when piping the
	 * object over the wire.
	 */
	guint need_sequence_inc : 1;
};

G_DEFINE_TYPE_WITH_PRIVATE (ECalComponent, e_cal_component, G_TYPE_OBJECT)

/* Frees the internal icalcomponent only if it does not have a parent.  If it
 * does, it means we don't own it and we shouldn't free it.
 */
static void
free_icalcomponent (ECalComponent *comp,
                    gboolean free)
{
	if (!comp->priv->icalcomp)
		return;

	if (free) {
		g_clear_object (&comp->priv->icalcomp);
	}

	/* Clean up */
	comp->priv->need_sequence_inc = FALSE;
}

/* The 'func' returns TRUE to continue */
static void
foreach_subcomponent (ICalComponent *icalcomp,
		      ICalComponentKind comp_kind,
		      gboolean (* func) (ICalComponent *icalcomp,
					 ICalComponent *subcomp,
					 gpointer user_data),
		      gpointer user_data)
{
	ICalCompIter *iter;
	ICalComponent *subcomp;

	g_return_if_fail (icalcomp != NULL);
	g_return_if_fail (func != NULL);

	iter = i_cal_component_begin_component (icalcomp, comp_kind);
	subcomp = i_cal_comp_iter_deref (iter);
	while (subcomp) {
		ICalComponent *next_subcomp;

		i_cal_object_set_owner (I_CAL_OBJECT (subcomp), G_OBJECT (icalcomp));

		next_subcomp = i_cal_comp_iter_next (iter);

		if (!func (icalcomp, subcomp, user_data)) {
			g_clear_object (&next_subcomp);
			g_object_unref (subcomp);
			break;
		}

		g_object_unref (subcomp);
		subcomp = next_subcomp;
	}

	g_clear_object (&iter);
}

/* The 'func' returns TRUE to continue */
static void
foreach_property (ICalComponent *icalcomp,
		  ICalPropertyKind prop_kind,
		  gboolean (* func) (ICalComponent *icalcomp,
				     ICalProperty *prop,
				     gpointer user_data),
		  gpointer user_data)
{
	ICalProperty *prop;

	g_return_if_fail (func != NULL);

	for (prop = i_cal_component_get_first_property (icalcomp, prop_kind);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (icalcomp, prop_kind)) {
		if (!func (icalcomp, prop, user_data))
			break;
	}
}

static gboolean
gather_all_properties_cb (ICalComponent *icalcomp,
			  ICalProperty *prop,
			  gpointer user_data)
{
	GSList **pprops = user_data;

	g_return_val_if_fail (pprops != NULL, FALSE);

	*pprops = g_slist_prepend (*pprops, g_object_ref (prop));

	return TRUE;
}

static GSList * /* ICalProperty * */
gather_all_properties (ICalComponent *icalcomp,
		       ICalPropertyKind prop_kind,
		       gboolean in_original_order)
{
	GSList *props = NULL;

	foreach_property (icalcomp, prop_kind, gather_all_properties_cb, &props);

	return in_original_order ? g_slist_reverse (props) : props;
}

static void
remove_all_properties_of_kind (ICalComponent *icalcomp,
			       ICalPropertyKind prop_kind)
{
	ICalProperty *prop;
	GSList *to_remove, *link;

	to_remove = gather_all_properties (icalcomp, prop_kind, FALSE);

	for (link = to_remove; link; link = g_slist_next (link)) {
		prop = link->data;

		i_cal_component_remove_property (icalcomp, prop);
	}

	g_slist_free_full (to_remove, g_object_unref);
}

/* returns NULL when value is NULL or empty string */
static ECalComponentText *
get_text_from_prop (ICalProperty *prop,
		    const gchar *(* get_prop_func) (ICalProperty *prop))
{
	ICalParameter *altrep_param;
	const gchar *value, *altrep;

	g_return_val_if_fail (prop != NULL, NULL);
	g_return_val_if_fail (get_prop_func != NULL, NULL);

	value = get_prop_func (prop);

	/* Skip empty values */
	if (!value || !*value)
		return NULL;

	altrep_param = i_cal_property_get_first_parameter (prop, I_CAL_ALTREP_PARAMETER);
	altrep = altrep_param ? i_cal_parameter_get_altrep (altrep_param) : NULL;
	g_clear_object (&altrep_param);

	if (altrep && !*altrep)
		altrep = NULL;

	return e_cal_component_text_new (value, altrep);
}

static void
set_text_altrep_on_prop (ICalProperty *prop,
			 const ECalComponentText *text)
{
	ICalParameter *param;
	const gchar *altrep;

	g_return_if_fail (prop != NULL);
	g_return_if_fail (text != NULL);

	altrep = e_cal_component_text_get_altrep (text);
	param = i_cal_property_get_first_parameter (prop, I_CAL_ALTREP_PARAMETER);

	if (altrep && *altrep) {
		if (param) {
			i_cal_parameter_set_altrep (param, (gchar *) altrep);
		} else {
			param = i_cal_parameter_new_altrep ((gchar *) altrep);
			i_cal_property_take_parameter (prop, param);
			param = NULL;
		}
	} else if (param) {
		i_cal_property_remove_parameter_by_kind (prop, I_CAL_ALTREP_PARAMETER);
	}

	g_clear_object (&param);
}


static void
cal_component_finalize (GObject *object)
{
	ECalComponent *comp = E_CAL_COMPONENT (object);

	free_icalcomponent (comp, TRUE);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_cal_component_parent_class)->finalize (object);
}

/* Class initialization function for the calendar component object */
static void
e_cal_component_class_init (ECalComponentClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = cal_component_finalize;
}

/* Object initialization function for the calendar component object */
static void
e_cal_component_init (ECalComponent *comp)
{
	comp->priv = e_cal_component_get_instance_private (comp);
	comp->priv->icalcomp = NULL;
}

/**
 * e_cal_component_new:
 *
 * Creates a new empty calendar component object.  Once created, you should set it from an
 * existing #icalcomponent structure by using e_cal_component_set_icalcomponent() or with a
 * new empty component type by using e_cal_component_set_new_vtype().
 *
 * Returns: (transfer full): A newly-created calendar component object.
 *
 * Since: 3.34
 **/
ECalComponent *
e_cal_component_new (void)
{
	return E_CAL_COMPONENT (g_object_new (E_TYPE_CAL_COMPONENT, NULL));
}

/**
 * e_cal_component_new_vtype:
 * @vtype: an #ECalComponentVType
 *
 * Creates a new #ECalComponent of type @vtype.
 *
 * Returns: (transfer full): A newly-created calendar component object with set @vtype.
 *
 * Since: 3.34
 **/
ECalComponent *
e_cal_component_new_vtype (ECalComponentVType vtype)
{
	ECalComponent *comp;

	comp = e_cal_component_new ();
	e_cal_component_set_new_vtype (comp, vtype);

	return comp;
}

/**
 * e_cal_component_new_from_string:
 * @calobj: A string representation of an iCalendar component.
 *
 * Creates a new calendar component object from the given iCalendar string.
 *
 * Returns: (transfer full) (nullable): A calendar component representing
 *    the given iCalendar string on success, %NULL if there was an error.
 *
 * Since: 3.34
 **/
ECalComponent *
e_cal_component_new_from_string (const gchar *calobj)
{
	ICalComponent *icalcomp;

	g_return_val_if_fail (calobj != NULL, NULL);

	icalcomp = i_cal_parser_parse_string (calobj);
	if (!icalcomp)
		return NULL;

	return e_cal_component_new_from_icalcomponent (icalcomp);
}

/**
 * e_cal_component_new_from_icalcomponent:
 * @icalcomp: (transfer full): An #ICalComponent to use
 *
 * Creates a new #ECalComponent which will has set @icalcomp as
 * an inner #ICalComponent. The newly created #ECalComponent takes
 * ownership of the @icalcomp, and if the call
 * to e_cal_component_set_icalcomponent() fails, then @icalcomp
 * is freed.
 *
 * Returns: (transfer full) (nullable): An #ECalComponent with @icalcomp
 *    assigned on success, %NULL if the @icalcomp cannot be assigned to
 *    #ECalComponent.
 *
 * Since: 3.34
 **/
ECalComponent *
e_cal_component_new_from_icalcomponent (ICalComponent *icalcomp)
{
	ECalComponent *comp;

	g_return_val_if_fail (icalcomp != NULL, NULL);

	comp = e_cal_component_new ();
	if (!e_cal_component_set_icalcomponent (comp, icalcomp)) {
		g_object_unref (icalcomp);
		g_object_unref (comp);

		return NULL;
	}

	return comp;
}

/**
 * e_cal_component_clone:
 * @comp: A calendar component object.
 *
 * Creates a new calendar component object by copying the information from
 * another one.
 *
 * Returns: (transfer full): A newly-created calendar component with the same
 * values as the original one.
 *
 * Since: 3.34
 **/
ECalComponent *
e_cal_component_clone (ECalComponent *comp)
{
	ECalComponent *new_comp;
	ICalComponent *new_icalcomp;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->need_sequence_inc == FALSE, NULL);

	new_comp = e_cal_component_new ();

	if (comp->priv->icalcomp) {
		new_icalcomp = i_cal_component_clone (comp->priv->icalcomp);
		if (!new_icalcomp || !e_cal_component_set_icalcomponent (new_comp, new_icalcomp)) {
			g_clear_object (&new_icalcomp);
			g_clear_object (&new_comp);
		}
	}

	return new_comp;
}

/* Ensures that the mandatory calendar component properties (uid, dtstamp) do
 * exist.  If they don't exist, it creates them automatically.
 */
static void
ensure_mandatory_properties (ECalComponent *comp)
{
	ICalProperty *prop;

	g_return_if_fail (comp->priv->icalcomp != NULL);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_UID_PROPERTY);
	if (prop) {
		g_object_unref (prop);
	} else {
		gchar *uid;

		uid = e_util_generate_uid ();
		i_cal_component_set_uid (comp->priv->icalcomp, uid);
		g_free (uid);
	}

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_DTSTAMP_PROPERTY);
	if (prop) {
		g_object_unref (prop);
	} else {
		ICalTime *tt;

		tt = i_cal_time_new_current_with_zone (i_cal_timezone_get_utc_timezone ());

		prop = i_cal_property_new_dtstamp (tt);
		i_cal_component_take_property (comp->priv->icalcomp, prop);

		g_object_unref (tt);
	}
}

static gboolean
ensure_alarm_uid_cb (ICalComponent *icalcomp,
		     ICalComponent *subcomp,
		     gpointer user_data)
{
	if (!e_cal_util_component_has_x_property (subcomp, E_CAL_EVOLUTION_ALARM_UID_PROPERTY)) {
		gchar *uid;

		uid = e_util_generate_uid ();
		e_cal_util_component_set_x_property (subcomp, E_CAL_EVOLUTION_ALARM_UID_PROPERTY, uid);
		g_free (uid);
	}

	return TRUE;
}

/**
 * e_cal_component_set_new_vtype:
 * @comp: A calendar component object.
 * @type: Type of calendar component to create.
 *
 * Clears any existing component data from a calendar component object and
 * creates a new #ICalComponent of the specified type for it.  The only property
 * that will be set in the new component will be its unique identifier.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_new_vtype (ECalComponent *comp,
                               ECalComponentVType type)
{
	ICalComponent *icalcomp;
	ICalComponentKind kind;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));

	free_icalcomponent (comp, TRUE);

	if (type == E_CAL_COMPONENT_NO_TYPE)
		return;

	/* Figure out the kind and create the icalcomponent */

	switch (type) {
	case E_CAL_COMPONENT_EVENT:
		kind = I_CAL_VEVENT_COMPONENT;
		break;

	case E_CAL_COMPONENT_TODO:
		kind = I_CAL_VTODO_COMPONENT;
		break;

	case E_CAL_COMPONENT_JOURNAL:
		kind = I_CAL_VJOURNAL_COMPONENT;
		break;

	case E_CAL_COMPONENT_FREEBUSY:
		kind = I_CAL_VFREEBUSY_COMPONENT;
		break;

	case E_CAL_COMPONENT_TIMEZONE:
		kind = I_CAL_VTIMEZONE_COMPONENT;
		break;

	default:
		g_warn_if_reached ();
		kind = I_CAL_NO_COMPONENT;
	}

	icalcomp = i_cal_component_new (kind);
	if (!icalcomp) {
		g_message ("e_cal_component_set_new_vtype(): Could not create the ICalComponent of kind %d!", kind);
		return;
	}

	/* Scan the component to build our mapping table */

	comp->priv->icalcomp = icalcomp;

	/* Add missing stuff */

	ensure_mandatory_properties (comp);
}

/**
 * e_cal_component_get_vtype:
 * @comp: A calendar component object.
 *
 * Queries the type of a calendar component object.
 *
 * Returns: The type of the component, as defined by RFC 2445.
 *
 * Since: 3.34
 **/
ECalComponentVType
e_cal_component_get_vtype (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), E_CAL_COMPONENT_NO_TYPE);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, E_CAL_COMPONENT_NO_TYPE);

	switch (i_cal_component_isa (comp->priv->icalcomp)) {
	case I_CAL_VEVENT_COMPONENT:
		return E_CAL_COMPONENT_EVENT;

	case I_CAL_VTODO_COMPONENT:
		return E_CAL_COMPONENT_TODO;

	case I_CAL_VJOURNAL_COMPONENT:
		return E_CAL_COMPONENT_JOURNAL;

	case I_CAL_VFREEBUSY_COMPONENT:
		return E_CAL_COMPONENT_FREEBUSY;

	case I_CAL_VTIMEZONE_COMPONENT:
		return E_CAL_COMPONENT_TIMEZONE;

	default:
		/* We should have been loaded with a supported type! */
		g_warn_if_reached ();
		return E_CAL_COMPONENT_NO_TYPE;
	}
}

/**
 * e_cal_component_set_icalcomponent:
 * @comp: A calendar component object.
 * @icalcomp: (transfer full) (nullable): An #ICalComponent.
 *
 * Sets the contents of a calendar component object from an #ICalComponent.
 * If the @comp already had an #ICalComponent set into it, it will
 * be freed automatically.
 *
 * Supported component types are VEVENT, VTODO, VJOURNAL, VFREEBUSY, and VTIMEZONE.
 *
 * Returns: %TRUE on success, %FALSE if @icalcomp is an unsupported component type.
 *
 * Since: 3.34
 **/
gboolean
e_cal_component_set_icalcomponent (ECalComponent *comp,
				   ICalComponent *icalcomp)
{
	ICalComponentKind kind;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);

	if (comp->priv->icalcomp == icalcomp)
		return TRUE;

	free_icalcomponent (comp, TRUE);

	if (!icalcomp) {
		comp->priv->icalcomp = NULL;
		return TRUE;
	}

	kind = i_cal_component_isa (icalcomp);

	if (!(kind == I_CAL_VEVENT_COMPONENT
	      || kind == I_CAL_VTODO_COMPONENT
	      || kind == I_CAL_VJOURNAL_COMPONENT
	      || kind == I_CAL_VFREEBUSY_COMPONENT
	      || kind == I_CAL_VTIMEZONE_COMPONENT))
		return FALSE;

	comp->priv->icalcomp = icalcomp;

	ensure_mandatory_properties (comp);

	foreach_subcomponent (icalcomp, I_CAL_VALARM_COMPONENT, ensure_alarm_uid_cb, NULL);

	return TRUE;
}

/**
 * e_cal_component_get_icalcomponent:
 * @comp: A calendar component object.
 *
 * Queries the #icalcomponent structure that a calendar component object is
 * wrapping.
 *
 * Returns: (transfer none) (nullable): An #ICalComponent structure, or %NULL
 *    if the @comp has no #ICalComponent set to it.
 *
 * Since: 3.34
 **/
ICalComponent *
e_cal_component_get_icalcomponent (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);

	return comp->priv->icalcomp;
}

/**
 * e_cal_component_strip_errors:
 * @comp: A calendar component object.
 *
 * Strips all error messages from the calendar component. Those error messages are
 * added to the iCalendar string representation whenever an invalid is used for
 * one of its fields.
 *
 * Since: 3.34
 **/
void
e_cal_component_strip_errors (ECalComponent *comp)
{
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));

	if (comp->priv->icalcomp)
		i_cal_component_strip_errors (comp->priv->icalcomp);
}

/**
 * e_cal_component_get_as_string:
 * @comp: A calendar component.
 *
 * Gets the iCalendar string representation of a calendar component.  You should
 * call e_cal_component_commit_sequence() before this function to ensure that the
 * component's sequence number is consistent with the state of the object.
 *
 * Returns: String representation of the calendar component according to
 * RFC 2445.
 *
 * Since: 3.34
 **/
gchar *
e_cal_component_get_as_string (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	/* Ensure that the user has committed the new SEQUENCE */
	g_return_val_if_fail (comp->priv->need_sequence_inc == FALSE, NULL);

	return i_cal_component_as_ical_string (comp->priv->icalcomp);
}

/* Ensures that an alarm subcomponent has the mandatory properties it needs. */
static gboolean
ensure_alarm_properties_cb (ICalComponent *icalcomp,
			    ICalComponent *subcomp,
			    gpointer user_data)
{
	ICalProperty *prop;
	ICalPropertyAction action;
	const gchar *summary;

	prop = i_cal_component_get_first_property (subcomp, I_CAL_ACTION_PROPERTY);
	if (!prop)
		return TRUE;

	action = i_cal_property_get_action (prop);

	g_object_unref (prop);

	switch (action) {
	case I_CAL_ACTION_DISPLAY:
		summary = i_cal_component_get_summary (icalcomp);

		/* Ensure we have a DESCRIPTION property */
		prop = i_cal_component_get_first_property (subcomp, I_CAL_DESCRIPTION_PROPERTY);
		if (prop) {
			if (summary && *summary) {
				ICalProperty *xprop;

				for (xprop = i_cal_component_get_first_property (subcomp, I_CAL_X_PROPERTY);
				     xprop;
				     g_object_unref (xprop), xprop = i_cal_component_get_next_property (subcomp, I_CAL_X_PROPERTY)) {
					const gchar *str;

					str = i_cal_property_get_x_name (xprop);
					if (!g_strcmp0 (str, "X-EVOLUTION-NEEDS-DESCRIPTION")) {
						i_cal_property_set_description (prop, summary);

						i_cal_component_remove_property (subcomp, xprop);
						g_object_unref (xprop);
						break;
					}
				}
			}

			g_object_unref (prop);
			break;
		}

		if (!summary || !*summary) {
			summary = _("Untitled appointment");

			/* add the X-EVOLUTION-NEEDS-DESCRIPTION property */
			prop = i_cal_property_new_x ("1");
			i_cal_property_set_x_name (prop, "X-EVOLUTION-NEEDS-DESCRIPTION");
			i_cal_component_take_property (subcomp, prop);
		}

		prop = i_cal_property_new_description (summary);
		i_cal_component_take_property (subcomp, prop);

		break;

	default:
		break;
	}

	return TRUE;
}

/**
 * e_cal_component_commit_sequence:
 * @comp: A calendar component object.
 *
 * Increments the sequence number property in a calendar component object if it
 * needs it.  This needs to be done when any of a number of properties listed in
 * RFC 2445 change values, such as the start and end dates of a component.
 *
 * This function must be called before calling e_cal_component_get_as_string() to
 * ensure that the component is fully consistent.
 *
 * Since: 3.34
 **/
void
e_cal_component_commit_sequence (ECalComponent *comp)
{
	ICalProperty *prop;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	foreach_subcomponent (comp->priv->icalcomp, I_CAL_VALARM_COMPONENT, ensure_alarm_properties_cb, comp);

	if (!comp->priv->need_sequence_inc)
		return;

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_SEQUENCE_PROPERTY);

	if (prop) {
		gint seq;

		seq = i_cal_property_get_sequence (prop);
		i_cal_property_set_sequence (prop, seq + 1);
		g_object_unref (prop);
	} else {
		/* The component had no SEQUENCE property, so assume that the
		 * default would have been zero.  Since it needed incrementing
		 * anyways, we use a value of 1 here.
		 */
		prop = i_cal_property_new_sequence (1);
		i_cal_component_take_property (comp->priv->icalcomp, prop);
	}

	comp->priv->need_sequence_inc = FALSE;
}

/**
 * e_cal_component_abort_sequence:
 * @comp: A calendar component object.
 *
 * Aborts the sequence change needed in the given calendar component,
 * which means it will not require a sequence commit (via
 * e_cal_component_commit_sequence()) even if the changes done require a
 * sequence increment.
 *
 * Since: 3.34
 **/
void
e_cal_component_abort_sequence (ECalComponent *comp)
{
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));

	comp->priv->need_sequence_inc = FALSE;
}

/**
 * e_cal_component_get_id:
 * @comp: A calendar component object.
 *
 * Get the ID of the component as an #ECalComponentId. The return value should
 * be freed with e_cal_component_id_free(), when no longer needed.
 *
 * Returns: (transfer full): the id of the component
 *
 * Since: 3.34
 **/
ECalComponentId *
e_cal_component_get_id (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	return e_cal_component_id_new_take (
		g_strdup (i_cal_component_get_uid (comp->priv->icalcomp)),
		e_cal_component_get_recurid_as_string (comp));
}

/**
 * e_cal_component_get_uid:
 * @comp: A calendar component object.
 *
 * Queries the unique identifier of a calendar component object.
 *
 * Returns: (transfer none): the UID string
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_get_uid (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	return i_cal_component_get_uid (comp->priv->icalcomp);
}

/**
 * e_cal_component_set_uid:
 * @comp: A calendar component object.
 * @uid: Unique identifier.
 *
 * Sets the unique identifier string of a calendar component object.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_uid (ECalComponent *comp,
                         const gchar *uid)
{
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (comp->priv->icalcomp != NULL);

	i_cal_component_set_uid (comp->priv->icalcomp, (gchar *) uid);
}

static gboolean
get_attachments_cb (ICalComponent *icalcomp,
		    ICalProperty *prop,
		    gpointer user_data)
{
	GSList **pattaches = user_data;
	ICalAttach *attach;

	g_return_val_if_fail (pattaches != NULL, FALSE);

	attach = i_cal_property_get_attach (prop);

	if (attach)
		*pattaches = g_slist_prepend (*pattaches, attach);

	return TRUE;
}

/**
 * e_cal_component_get_attachments:
 * @comp: A calendar component object
 *
 * Queries the attachment properties as #ICalAttach objects of the calendar
 * component object. Changes on these objects are directly affecting the component.
 * Free the returned #GSList with g_slist_free_full (slist, g_object_unref);,
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable) (element-type ICalAttach): a #GSList of
 *    attachments, as #ICalAttach objects
 *
 * Since: 3.34
 **/
GSList *
e_cal_component_get_attachments (ECalComponent *comp)
{
	GSList *attaches = NULL;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	foreach_property (comp->priv->icalcomp, I_CAL_ATTACH_PROPERTY, get_attachments_cb, &attaches);

	return g_slist_reverse (attaches);
}

/**
 * e_cal_component_set_attachments:
 * @comp: A calendar component object
 * @attachments: (nullable) (element-type ICalAttach): a #GSList of an #ICalAttach,
 *    or %NULL to remove any existing
 *
 * Sets the attachments of the calendar component object.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_attachments (ECalComponent *comp,
				 const GSList *attachments)
{
	GSList *link;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	remove_all_properties_of_kind (comp->priv->icalcomp, I_CAL_ATTACH_PROPERTY);

	for (link = (GSList *) attachments; link; link = g_slist_next (link)) {
		ICalAttach *attach = link->data;
		ICalProperty *prop;

		if (!attach)
			continue;

		prop = i_cal_property_new_attach (attach);

		if (prop)
			i_cal_component_take_property (comp->priv->icalcomp, prop);
	}
}

/**
 * e_cal_component_has_attachments:
 * @comp: A calendar component object.
 *
 * Queries the component to see if it has attachments.
 *
 * Returns: TRUE if there are attachments, FALSE otherwise.
 *
 * Since: 3.34
 **/
gboolean
e_cal_component_has_attachments (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, FALSE);

	return e_cal_util_component_has_property (comp->priv->icalcomp, I_CAL_ATTACH_PROPERTY);
}

/* Creates a comma-delimited string of categories */
static gchar *
stringify_categories (const GSList *categ_list)
{
	GString *str;
	GSList *link;

	str = g_string_new (NULL);

	for (link = (GSList *) categ_list; link; link = g_slist_next (link)) {
		const gchar *category = link->data;

		if (category && *category) {
			if (str->len)
				g_string_append_c (str, ',');
			g_string_append (str, category);
		}
	}

	return g_string_free (str, !str->len);
}

/**
 * e_cal_component_get_categories:
 * @comp: A calendar component object.
 *
 * Queries the categories of the given calendar component. The categories
 * are returned in the @categories argument, which, on success, will contain
 * a comma-separated list of all categories set in the component.
 * Free the returned string with g_free(), when no longer needed.
 *
 * Returns: (transfer full) (nullable): the categories as string, or %NULL
 *    if none are set
 *
 * Since: 3.34
 **/
gchar *
e_cal_component_get_categories (ECalComponent *comp)
{
	GSList *categories;
	gchar *str;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	categories = e_cal_component_get_categories_list (comp);
	if (!categories)
		return NULL;

	str = stringify_categories (categories);

	g_slist_free_full (categories, g_free);

	return str;
}

/**
 * e_cal_component_set_categories:
 * @comp: A calendar component object.
 * @categories: Comma-separated list of categories.
 *
 * Sets the list of categories for a calendar component.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_categories (ECalComponent *comp,
                                const gchar *categories)
{
	ICalProperty *prop;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	remove_all_properties_of_kind (comp->priv->icalcomp, I_CAL_CATEGORIES_PROPERTY);

	if (!categories || !*categories)
		return;

	prop = i_cal_property_new_categories (categories);
	i_cal_component_take_property (comp->priv->icalcomp, prop);
}

/**
 * e_cal_component_get_categories_list:
 * @comp: A calendar component object.
 *
 * Queries the list of categories of a calendar component object. Each element
 * in the returned categ_list is a string with the corresponding category.
 * Free the returned #GSList with g_slist_free_full (categories, g_free); , when
 * no longer needed.
 *
 * Returns: (transfer full) (element-type utf8) (nullable): the #GSList of strings, where each
 *    string is a category, or %NULL, when no category is set.
 *
 * Since: 3.34
 **/
GSList *
e_cal_component_get_categories_list (ECalComponent *comp)
{
	ICalProperty *prop;
	const gchar *categories;
	const gchar *p;
	const gchar *cat_start;
	GSList *categ_list = NULL;
	gchar *str;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	for (prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_CATEGORIES_PROPERTY);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (comp->priv->icalcomp, I_CAL_CATEGORIES_PROPERTY)) {
		categories = i_cal_property_get_categories (prop);

		if (!categories)
			continue;

		cat_start = categories;

		for (p = categories; *p; p++) {
			if (*p == ',') {
				str = g_strndup (cat_start, p - cat_start);
				categ_list = g_slist_prepend (categ_list, str);

				cat_start = p + 1;
			}
		}

		str = g_strndup (cat_start, p - cat_start);
		categ_list = g_slist_prepend (categ_list, str);
	}

	return g_slist_reverse (categ_list);
}

/**
 * e_cal_component_set_categories_list:
 * @comp: A calendar component object.
 * @categ_list: (element-type utf8): List of strings, one for each category.
 *
 * Sets the list of categories of a calendar component object.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_categories_list (ECalComponent *comp,
                                     const GSList *categ_list)
{
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	if (!categ_list) {
		e_cal_component_set_categories (comp, NULL);
	} else {
		gchar *categories_str;

		/* Create a single string of categories */
		categories_str = stringify_categories (categ_list);

		/* Set the categories */
		e_cal_component_set_categories (comp, categories_str);
		g_free (categories_str);
	}
}

/**
 * e_cal_component_get_classification:
 * @comp: A calendar component object.
 *
 * Queries the classification of a calendar component object.  If the
 * classification property is not set on this component, this function returns
 * #E_CAL_COMPONENT_CLASS_NONE.
 *
 * Retuurns: a classification of the @comp, as an #ECalComponentClassification
 *
 * Since: 3.34
 **/
ECalComponentClassification
e_cal_component_get_classification (ECalComponent *comp)
{
	ICalProperty *prop;
	ECalComponentClassification classif;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), E_CAL_COMPONENT_CLASS_UNKNOWN);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, E_CAL_COMPONENT_CLASS_UNKNOWN);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_CLASS_PROPERTY);

	if (!prop)
		return E_CAL_COMPONENT_CLASS_NONE;

	switch (i_cal_property_get_class (prop)) {
	case I_CAL_CLASS_PUBLIC:
		classif = E_CAL_COMPONENT_CLASS_PUBLIC;
		break;
	case I_CAL_CLASS_PRIVATE:
		classif = E_CAL_COMPONENT_CLASS_PRIVATE;
		break;
	case I_CAL_CLASS_CONFIDENTIAL:
		classif = E_CAL_COMPONENT_CLASS_CONFIDENTIAL;
		break;
	default:
		classif = E_CAL_COMPONENT_CLASS_UNKNOWN;
		break;
	}

	g_object_unref (prop);

	return classif;
}

/**
 * e_cal_component_set_classification:
 * @comp: A calendar component object.
 * @classif: Classification to use.
 *
 * Sets the classification property of a calendar component object.  To unset
 * the property, specify E_CAL_COMPONENT_CLASS_NONE for @classif.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_classification (ECalComponent *comp,
                                    ECalComponentClassification classif)
{
	ICalProperty_Class prop_class;
	ICalProperty *prop;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (classif != E_CAL_COMPONENT_CLASS_UNKNOWN);
	g_return_if_fail (comp->priv->icalcomp != NULL);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_CLASS_PROPERTY);

	if (classif == E_CAL_COMPONENT_CLASS_NONE) {
		if (prop) {
			i_cal_component_remove_property (comp->priv->icalcomp, prop);
			g_object_unref (prop);
		}

		return;
	}

	switch (classif) {
	case E_CAL_COMPONENT_CLASS_PUBLIC:
		prop_class = I_CAL_CLASS_PUBLIC;
		break;

	case E_CAL_COMPONENT_CLASS_PRIVATE:
		prop_class = I_CAL_CLASS_PRIVATE;
		break;

	case E_CAL_COMPONENT_CLASS_CONFIDENTIAL:
		prop_class = I_CAL_CLASS_CONFIDENTIAL;
		break;

	default:
		g_warn_if_reached ();
		prop_class = I_CAL_CLASS_NONE;
		break;
	}

	if (prop) {
		i_cal_property_set_class (prop, prop_class);
		g_object_unref (prop);
	} else {
		prop = i_cal_property_new_class (prop_class);
		i_cal_component_take_property (comp->priv->icalcomp, prop);
	}
}

/* Gets a text list value */
static GSList *
get_text_list (ICalComponent *icalcomp,
	       ICalPropertyKind prop_kind,
               const gchar *(* get_prop_func) (ICalProperty *prop))
{
	GSList *link, *props, *tl = NULL;

	if (!icalcomp)
		return NULL;

	props = gather_all_properties (icalcomp, prop_kind, FALSE);
	for (link = props; link; link = g_slist_next (link)) {
		ICalProperty *prop = link->data;
		ECalComponentText *text;

		if (!prop)
			continue;

		text = get_text_from_prop (prop, get_prop_func);
		if (!text)
			continue;

		tl = g_slist_prepend (tl, text);
	}

	g_slist_free_full (props, g_object_unref);

	/* No need to reverse it, the props are in reverse order
	   and processed in the reverse order, thus the result
	   is in the expected order. */
	return tl;
}

/* Sets a text list value */
static void
set_text_list (ICalComponent *icalcomp,
	       ICalPropertyKind prop_kind,
               ICalProperty * (* new_prop_func) (const gchar *value),
               const GSList *tl)
{
	GSList *link;

	/* Remove old texts */
	remove_all_properties_of_kind (icalcomp, prop_kind);

	/* Add in new texts */

	for (link = (GSList *) tl; link; link = g_slist_next (link)) {
		ECalComponentText *text;
		ICalProperty *prop;

		text = link->data;
		if (!text || !e_cal_component_text_get_value (text))
			continue;

		prop = new_prop_func ((gchar *) e_cal_component_text_get_value (text));

		set_text_altrep_on_prop (prop, text);

		i_cal_component_take_property (icalcomp, prop);
	}
}

/**
 * e_cal_component_get_comments:
 * @comp: A calendar component object.
 *
 * Queries the comments of a calendar component object.  The comment property can
 * appear several times inside a calendar component, and so a list of
 * #ECalComponentText is returned. Free the returned #GSList with
 * g_slist_free_full (slist, e_cal_component_text_free);, when no longer needed.
 *
 * Returns: (transfer full) (element-type ECalComponentText) (nullable): the comment properties
 *    and their parameters, as a list of #ECalComponentText structures; or %NULL, when
 *    the component doesn't contain any.
 *
 * Since: 3.34
 **/
GSList *
e_cal_component_get_comments (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	return get_text_list (comp->priv->icalcomp, I_CAL_COMMENT_PROPERTY, i_cal_property_get_comment);
}

/**
 * e_cal_component_set_comments:
 * @comp: A calendar component object.
 * @text_list: (element-type ECalComponentText): List of #ECalComponentText structures.
 *
 * Sets the comments of a calendar component object.  The comment property can
 * appear several times inside a calendar component, and so a list of
 * #ECalComponentText structures is used.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_comments (ECalComponent *comp,
			      const GSList *text_list)
{
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	set_text_list (comp->priv->icalcomp, I_CAL_COMMENT_PROPERTY, i_cal_property_new_comment, text_list);
}

/**
 * e_cal_component_get_contacts:
 * @comp: A calendar component object.
 *
 * Queries the contact of a calendar component object.  The contact property can
 * appear several times inside a calendar component, and so a list of
 * #ECalComponentText is returned. Free the returned #GSList with
 * g_slist_free_full (slist, e_cal_component_text_free);, when no longer needed.
 *
 * Returns: (transfer full) (element-type ECalComponentText): the contact properties and
 *    their parameters, as a #GSList of #ECalComponentText structures.
 *
 * Since: 3.34
 **/
GSList *
e_cal_component_get_contacts (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	return get_text_list (comp->priv->icalcomp, I_CAL_CONTACT_PROPERTY, i_cal_property_get_contact);
}

/**
 * e_cal_component_set_contacts:
 * @comp: A calendar component object.
 * @text_list: (element-type ECalComponentText): List of #ECalComponentText structures.
 *
 * Sets the contact of a calendar component object.  The contact property can
 * appear several times inside a calendar component, and so a list of
 * #ECalComponentText structures is used.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_contacts (ECalComponent *comp,
			      const GSList *text_list)
{
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	set_text_list (comp->priv->icalcomp, I_CAL_CONTACT_PROPERTY, i_cal_property_new_contact, text_list);
}

/* Gets a struct icaltimetype value */
static ICalTime *
get_icaltimetype (ICalComponent *icalcomp,
		  ICalPropertyKind prop_kind,
                  ICalTime * (* get_prop_func) (ICalProperty *prop))
{
	ICalProperty *prop;
	ICalTime *tt;

	prop = i_cal_component_get_first_property (icalcomp, prop_kind);
	if (!prop)
		return NULL;

	tt = get_prop_func (prop);

	g_object_unref (prop);

	return tt;
}

/* Sets a struct icaltimetype value */
static void
set_icaltimetype (ICalComponent *icalcomp,
                  ICalPropertyKind prop_kind,
                  ICalProperty *(* prop_new_func) (ICalTime *tt),
                  void (* prop_set_func) (ICalProperty *prop,
                                          ICalTime *tt),
                  const ICalTime *tt)
{
	ICalProperty *prop;

	prop = i_cal_component_get_first_property (icalcomp, prop_kind);

	if (!tt) {
		if (prop) {
			i_cal_component_remove_property (icalcomp, prop);
			g_clear_object (&prop);
		}

		return;
	}

	if (prop) {
		prop_set_func (prop, (ICalTime *) tt);
		g_object_unref (prop);
	} else {
		prop = prop_new_func ((ICalTime *) tt);
		i_cal_component_take_property (icalcomp, prop);
	}
}

/**
 * e_cal_component_get_completed:
 * @comp: A calendar component object.
 *
 * Queries the date at which a calendar compoment object was completed.
 * Free the returned non-NULL pointer with g_object_unref(), when
 * no longer needed.
 *
 * Returns: (transfer full) (nullable): the completion date, as an #ICalTime,
 * or %NULL, when none is set
 *
 * Since: 3.34
 **/
ICalTime *
e_cal_component_get_completed (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	return get_icaltimetype (comp->priv->icalcomp, I_CAL_COMPLETED_PROPERTY, i_cal_property_get_completed);
}

/**
 * e_cal_component_set_completed:
 * @comp: A calendar component object.
 * @tt: (nullable): Value for the completion date.
 *
 * Sets the date at which a calendar component object was completed.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_completed (ECalComponent *comp,
			       const ICalTime *tt)
{
	ICalTime *tmp_tt = NULL;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	if (tt && i_cal_time_is_date ((ICalTime *) tt)) {
		tmp_tt = i_cal_time_clone (tt);
		tt = tmp_tt;

		i_cal_time_set_is_date (tmp_tt, FALSE);
		i_cal_time_set_hour (tmp_tt, 0);
		i_cal_time_set_minute (tmp_tt, 0);
		i_cal_time_set_second (tmp_tt, 0);
		i_cal_time_set_timezone (tmp_tt, i_cal_timezone_get_utc_timezone ());
	}

	set_icaltimetype (comp->priv->icalcomp, I_CAL_COMPLETED_PROPERTY,
		i_cal_property_new_completed,
		i_cal_property_set_completed,
		tt);

	g_clear_object (&tmp_tt);
}

/**
 * e_cal_component_get_created:
 * @comp: A calendar component object.
 *
 * Queries the date in which a calendar component object was created in the
 * calendar store. Free the returned non-NULL pointer with g_object_unref(), when
 * no longer needed.
 *
 * Returns: (transfer full) (nullable): the creation date, as an #ICalTime, or
 * %NULL, when none is set
 *
 * Since: 3.34
 **/
ICalTime *
e_cal_component_get_created (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	return get_icaltimetype (comp->priv->icalcomp, I_CAL_CREATED_PROPERTY, i_cal_property_get_created);
}

/**
 * e_cal_component_set_created:
 * @comp: A calendar component object.
 * @tt: (nullable): Value for the creation date.
 *
 * Sets the date in which a calendar component object is created in the calendar
 * store.  This should only be used inside a calendar store application, i.e.
 * not by calendar user agents.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_created (ECalComponent *comp,
			     const ICalTime *tt)
{
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	set_icaltimetype (comp->priv->icalcomp, I_CAL_CREATED_PROPERTY,
		i_cal_property_new_created,
		i_cal_property_set_created,
		tt);
}

/**
 * e_cal_component_get_descriptions:
 * @comp: A calendar component object.
 *
 * Queries the description of a calendar component object.  Journal components
 * may have more than one description, and as such this function returns a list
 * of #ECalComponentText structures.  All other types of components can have at
 * most one description. Free the returned #GSList with
 * g_slist_free_full (slist, e_cal_component_text_free);, when no longer needed.
 *
 * Returns: (transfer full) (element-type ECalComponentText) (nullable): the description
 *    properties and their parameters, as a #GSList of #ECalComponentText structures.
 *
 * Since: 3.34
 **/
GSList *
e_cal_component_get_descriptions (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	return get_text_list (comp->priv->icalcomp, I_CAL_DESCRIPTION_PROPERTY, i_cal_property_get_description);
}

/**
 * e_cal_component_set_descriptions:
 * @comp: A calendar component object.
 * @text_list: (element-type ECalComponentText): List of #ECalComponentText structures.
 *
 * Sets the description of a calendar component object.  Journal components may
 * have more than one description, and as such this function takes in a list of
 * #ECalComponentText structures.  All other types of components can have
 * at most one description.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_descriptions (ECalComponent *comp,
				  const GSList *text_list)
{
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	set_text_list (comp->priv->icalcomp, I_CAL_DESCRIPTION_PROPERTY, i_cal_property_new_description, text_list);
}

/* Gets a date/time and timezone pair */
static ECalComponentDateTime *
get_datetime (ICalComponent *icalcomp,
	      ICalPropertyKind prop_kind,
              ICalTime * (* get_prop_func) (ICalProperty *prop),
	      ICalProperty **out_prop)
{
	ICalProperty *prop;
	ICalParameter *param;
	ICalTime *value = NULL;
	gchar *tzid;

	if (out_prop)
		*out_prop = NULL;

	prop = i_cal_component_get_first_property (icalcomp, prop_kind);
	if (prop)
		value = get_prop_func (prop);

	if (!value) {
		g_clear_object (&prop);
		return NULL;
	}

	param = i_cal_property_get_first_parameter (prop, I_CAL_TZID_PARAMETER);
	/* If the ICalTime has is_utc set, we set "UTC" as the TZID.
	 * This makes the timezone code simpler. */
	if (param)
		tzid = g_strdup (i_cal_parameter_get_tzid (param));
	else if (i_cal_time_is_utc (value))
		tzid = g_strdup ("UTC");
	else
		tzid = NULL;

	g_clear_object (&param);

	if (out_prop)
		*out_prop = prop;
	else
		g_clear_object (&prop);

	return e_cal_component_datetime_new_take (value, tzid);
}

/* Sets a date/time and timezone pair */
static void
set_datetime (ICalComponent *icalcomp,
	      ICalPropertyKind prop_kind,
	      ICalProperty *(* prop_new_func) (ICalTime *tt),
              void (* prop_set_func) (ICalProperty *prop,
				      ICalTime *tt),
	      const ECalComponentDateTime *dt,
	      ICalProperty **out_prop)
{
	ICalProperty *prop;
	ICalParameter *param;
	ICalTime *tt;
	const gchar *tzid;

	if (out_prop)
		*out_prop = NULL;

	prop = i_cal_component_get_first_property (icalcomp, prop_kind);

	/* If we are setting the property to NULL (i.e. removing it), then
	 * we remove it if it exists. */
	if (!dt) {
		if (prop) {
			i_cal_component_remove_property (icalcomp, prop);
			g_clear_object (&prop);
		}

		return;
	}

	tt = e_cal_component_datetime_get_value (dt);

	g_return_if_fail (tt != NULL);

	tzid = e_cal_component_datetime_get_tzid (dt);

	/* If the TZID is set to "UTC", we set the is_utc flag. */
	if (!g_strcmp0 (tzid, "UTC"))
		i_cal_time_set_timezone (tt, i_cal_timezone_get_utc_timezone ());
	else if (i_cal_time_is_utc (tt))
		i_cal_time_set_timezone (tt, NULL);

	if (prop) {
		/* make sure no VALUE property is left if not needed */
		i_cal_property_remove_parameter_by_kind (prop, I_CAL_VALUE_PARAMETER);

		prop_set_func (prop, tt);
	} else {
		prop = prop_new_func (tt);
		i_cal_component_add_property (icalcomp, prop);
	}

	param = i_cal_property_get_first_parameter (prop, I_CAL_TZID_PARAMETER);

	/* If the TZID is set to "UTC", we don't want to save the TZID. */
	if (tzid && g_strcmp0 (tzid, "UTC") != 0) {
		if (param) {
			i_cal_parameter_set_tzid (param, (gchar *) tzid);
		} else {
			param = i_cal_parameter_new_tzid ((gchar *) tzid);
			i_cal_property_add_parameter (prop, param);
		}
	} else if (param) {
		i_cal_property_remove_parameter_by_kind (prop, I_CAL_TZID_PARAMETER);
	}

	g_clear_object (&param);

	if (out_prop)
		*out_prop = prop;
	else
		g_clear_object (&prop);
}

/* This tries to get the DTSTART + DURATION for a VEVENT or VTODO. In a
 * VEVENT this is used for the DTEND if no DTEND exists, In a VTOTO it is
 * used for the DUE date if DUE doesn't exist. */
static ECalComponentDateTime *
e_cal_component_get_start_plus_duration (ECalComponent *comp)
{
	ICalDuration *duration;
	ICalProperty *prop;
	ICalTime *tt;
	ECalComponentDateTime *dt;
	guint dur_days, dur_hours, dur_minutes, dur_seconds;

	/* libical can calculate it from DTSTART/DTEND, which is not needed here */
	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_DURATION_PROPERTY);
	if (!prop)
		return NULL;

	g_clear_object (&prop);

	duration = i_cal_component_get_duration (comp->priv->icalcomp);
	if (!duration || i_cal_duration_is_null_duration (duration) || i_cal_duration_is_bad_duration (duration)) {
		g_clear_object (&duration);
		return NULL;
	}

	/* Get the DTSTART time. */
	dt = get_datetime (comp->priv->icalcomp, I_CAL_DTSTART_PROPERTY, i_cal_property_get_dtstart, NULL);
	if (!dt) {
		g_object_unref (duration);
		return NULL;
	}

	/* The DURATION shouldn't be negative, but just return DTSTART if it
	 * is, i.e. assume it is 0. */
	if (i_cal_duration_is_neg (duration)) {
		g_object_unref (duration);
		return dt;
	}

	/* If DTSTART is a DATE value, then we need to check if the DURATION
	 * includes any hours, minutes or seconds. If it does, we need to
	 * make the DTEND/DUE a DATE-TIME value. */
	dur_days = i_cal_duration_get_days (duration) + (7 * i_cal_duration_get_weeks (duration));
	dur_hours = i_cal_duration_get_hours (duration);
	dur_minutes = i_cal_duration_get_minutes (duration);
	dur_seconds = i_cal_duration_get_seconds (duration);

	tt = e_cal_component_datetime_get_value (dt);
	if (i_cal_time_is_date (tt) && (
	    dur_hours != 0 || dur_minutes != 0 || dur_seconds != 0)) {
		i_cal_time_set_is_date (tt, FALSE);
	}

	/* Add on the DURATION. */
	i_cal_time_adjust (tt, dur_days, dur_hours, dur_minutes, dur_seconds);

	g_object_unref (duration);

	return dt;
}

/**
 * e_cal_component_get_dtend:
 * @comp: A calendar component object.
 *
 * Queries the date/time end of a calendar component object. In case there's no DTEND,
 * but only DTSTART and DURATION, then the end is computed from the later two.
 * Free the returned #ECalComponentDateTime with e_cal_component_datetime_free(),
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable): the date/time end, as an #ECalComponentDateTime
 *
 * Since: 3.34
 **/
ECalComponentDateTime *
e_cal_component_get_dtend (ECalComponent *comp)
{
	ECalComponentDateTime *dt;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	if (e_cal_component_get_vtype (comp) != E_CAL_COMPONENT_EVENT &&
	    e_cal_component_get_vtype (comp) != E_CAL_COMPONENT_FREEBUSY)
		return NULL;

	dt = get_datetime (comp->priv->icalcomp, I_CAL_DTEND_PROPERTY, i_cal_property_get_dtend, NULL);

	/* If we don't have a DTEND property, then we try to get DTSTART + DURATION. */
	if (!dt)
		dt = e_cal_component_get_start_plus_duration (comp);

	return dt;
}

/**
 * e_cal_component_set_dtend:
 * @comp: A calendar component object.
 * @dt: (nullable): End date/time, or %NULL, to remove the property.
 *
 * Sets the date/time end property of a calendar component object.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_dtend (ECalComponent *comp,
                           const ECalComponentDateTime *dt)
{
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	g_warn_if_fail (e_cal_component_get_vtype (comp) == E_CAL_COMPONENT_EVENT ||
			e_cal_component_get_vtype (comp) == E_CAL_COMPONENT_FREEBUSY);

	set_datetime (comp->priv->icalcomp, I_CAL_DTEND_PROPERTY,
		i_cal_property_new_dtend,
		i_cal_property_set_dtend,
		dt,
		NULL);

	/* Make sure we remove any existing DURATION property, as it can't be
	 * used with a DTEND. If DTEND is set to NULL, i.e. removed, we also
	 * want to remove any DURATION. */
	remove_all_properties_of_kind (comp->priv->icalcomp, I_CAL_DURATION_PROPERTY);

	comp->priv->need_sequence_inc = TRUE;
}

/**
 * e_cal_component_get_dtstamp:
 * @comp: A calendar component object.
 *
 * Queries the date/timestamp property of a calendar component object, which is
 * the last time at which the object was modified by a calendar user agent.
 *
 * Free a non-NULL returned object with g_object_unref(),
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable): A value for the date/timestamp, or %NULL, when none found.
 *
 * Since: 3.34
 **/
ICalTime *
e_cal_component_get_dtstamp (ECalComponent *comp)
{
	ICalProperty *prop;
	ICalTime *tt;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_DTSTAMP_PROPERTY);

	/* This MUST exist, since we ensured that it did */
	g_return_val_if_fail (prop != NULL, NULL);

	tt = i_cal_property_get_dtstamp (prop);

	g_clear_object (&prop);

	return tt;
}

/**
 * e_cal_component_set_dtstamp:
 * @comp: A calendar component object.
 * @tt: Date/timestamp value.
 *
 * Sets the date/timestamp of a calendar component object.  This should be
 * called whenever a calendar user agent makes a change to a component's
 * properties.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_dtstamp (ECalComponent *comp,
			     const ICalTime *tt)
{
	ICalProperty *prop;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (I_CAL_IS_TIME ((ICalTime *) tt));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_DTSTAMP_PROPERTY);

	/* This MUST exist, since we ensured that it did */
	g_return_if_fail (prop != NULL);

	i_cal_property_set_dtstamp (prop, (ICalTime *) tt);

	g_clear_object (&prop);
}

/**
 * e_cal_component_get_dtstart:
 * @comp: A calendar component object.
 *
 * Queries the date/time start of a calendar component object.
 * Free the returned #ECalComponentDateTime with e_cal_component_datetime_free(),
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable): the date/time start, as an #ECalComponentDateTime
 *
 * Since: 3.34
 **/
ECalComponentDateTime *
e_cal_component_get_dtstart (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	return get_datetime (comp->priv->icalcomp, I_CAL_DTSTART_PROPERTY, i_cal_property_get_dtstart, NULL);
}

/**
 * e_cal_component_set_dtstart:
 * @comp: A calendar component object.
 * @dt: (nullable): Start date/time, or %NULL, to remove the property.
 *
 * Sets the date/time start property of a calendar component object.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_dtstart (ECalComponent *comp,
			     const ECalComponentDateTime *dt)
{
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	set_datetime (comp->priv->icalcomp, I_CAL_DTSTART_PROPERTY,
		i_cal_property_new_dtstart,
		i_cal_property_set_dtstart,
		dt,
		NULL);

	comp->priv->need_sequence_inc = TRUE;
}

/**
 * e_cal_component_get_due:
 * @comp: A calendar component object.
 *
 * Queries the due date/time of a calendar component object. In case there's no DUE,
 * but only DTSTART and DURATION, then the due is computed from the later two.
 * Free the returned #ECalComponentDateTime with e_cal_component_datetime_free(),
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable): the due date/time, as an #ECalComponentDateTime
 *
 * Since: 3.34
 **/
ECalComponentDateTime *
e_cal_component_get_due (ECalComponent *comp)
{
	ECalComponentDateTime *dt;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	if (e_cal_component_get_vtype (comp) != E_CAL_COMPONENT_TODO)
		return NULL;

	dt = get_datetime (comp->priv->icalcomp, I_CAL_DUE_PROPERTY, i_cal_property_get_due, NULL);

	/* If we don't have a DUE property, then we try to get DTSTART + DURATION. */
	if (!dt)
		dt = e_cal_component_get_start_plus_duration (comp);

	return dt;
}

/**
 * e_cal_component_set_due:
 * @comp: A calendar component object.
 * @dt: (nullable): End date/time, or %NULL, to remove the property.
 *
 * Sets the due date/time property of a calendar component object.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_due (ECalComponent *comp,
			 const ECalComponentDateTime *dt)
{
	ICalProperty *prop;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	g_warn_if_fail (e_cal_component_get_vtype (comp) == E_CAL_COMPONENT_TODO);

	set_datetime (comp->priv->icalcomp, I_CAL_DUE_PROPERTY,
		i_cal_property_new_due,
		i_cal_property_set_due,
		dt,
		NULL);

	/* Make sure we remove any existing DURATION property, as it can't be
	 * used with a DTEND. If DTEND is set to NULL, i.e. removed, we also
	 * want to remove any DURATION. */
	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_DURATION_PROPERTY);
	if (prop) {
		i_cal_component_remove_property (comp->priv->icalcomp, prop);
		g_clear_object (&prop);
	}

	comp->priv->need_sequence_inc = TRUE;
}

/* Builds a list of ECalComponentPeriod structures based on a list of icalproperties */
static GSList * /* ECalComponentPeriod * */
get_period_list (ICalComponent *icalcomp,
		 ICalPropertyKind prop_kind,
		 ICalDatetimeperiod * (* get_prop_func) (ICalProperty *prop))
{
	GSList *props, *link, *periods = NULL;

	props = gather_all_properties (icalcomp, prop_kind, FALSE);

	for (link = props; link; link = g_slist_next (link)) {
		ICalProperty *prop = link->data;
		ICalParameter *param;
		ICalPeriod *icalperiod;
		ICalDatetimeperiod *pt;
		ICalDuration *duration = NULL;
		ICalTime *start = NULL, *end = NULL;
		ECalComponentPeriod *period;
		ECalComponentPeriodKind period_kind;

		if (!prop)
			continue;

		/* Get start and end/duration */
		pt = get_prop_func (prop);
		if (!pt)
			continue;

		icalperiod = i_cal_datetimeperiod_get_period (pt);

		/* Get value parameter */
		param = i_cal_property_get_first_parameter (prop, I_CAL_VALUE_PARAMETER);

		if (param) {
			ICalParameterValue value_type;

			value_type = i_cal_parameter_get_value (param);

			if (value_type == I_CAL_VALUE_DATE || value_type == I_CAL_VALUE_DATETIME) {
				period_kind = E_CAL_COMPONENT_PERIOD_DATETIME;
			} else if (value_type == I_CAL_VALUE_PERIOD) {
				duration = i_cal_period_get_duration (icalperiod);

				if (!duration ||
				    i_cal_duration_is_null_duration (duration) ||
				    i_cal_duration_is_bad_duration (duration))
					period_kind = E_CAL_COMPONENT_PERIOD_DATETIME;
				else
					period_kind = E_CAL_COMPONENT_PERIOD_DURATION;

				g_clear_object (&duration);
			} else {
				g_message ("get_period_list(): Unknown value for period %d; using DATETIME", value_type);
				period_kind = E_CAL_COMPONENT_PERIOD_DATETIME;
			}
		} else {
			period_kind = E_CAL_COMPONENT_PERIOD_DATETIME;
		}

		start = i_cal_period_get_start (icalperiod);

		if (period_kind == E_CAL_COMPONENT_PERIOD_DATETIME) {
			if (!start || i_cal_time_is_null_time (start)) {
				g_clear_object (&start);
				start = i_cal_datetimeperiod_get_time (pt);
			} else {
				end = i_cal_period_get_end (icalperiod);
			}
		} else /* if (period_kind == E_CAL_COMPONENT_PERIOD_DURATION) */ {
			duration = i_cal_period_get_duration (icalperiod);
		}

		period = period_kind == E_CAL_COMPONENT_PERIOD_DATETIME ?
			e_cal_component_period_new_datetime (start, end) :
			e_cal_component_period_new_duration (start, duration);

		if (period)
			periods = g_slist_prepend (periods, period);

		g_clear_object (&param);
		g_clear_object (&icalperiod);
		g_clear_object (&pt);
		g_clear_object (&duration);
		g_clear_object (&start);
		g_clear_object (&end);
	}

	g_slist_free_full (props, g_object_unref);

	/* No need to reverse it, the props are in reverse order
	   and processed in the reverse order, thus the result
	   is in the expected order. */
	return periods;
}

/* Sets a period list value */
static void
set_period_list (ICalComponent *icalcomp,
		 ICalPropertyKind prop_kind,
		 ICalProperty *(* new_prop_func) (ICalDatetimeperiod *period),
		 const GSList *periods_list)
{
	GSList *link;

	/* Remove old periods */

	remove_all_properties_of_kind (icalcomp, prop_kind);

	/* Add in new periods */

	for (link = (GSList *) periods_list; link; link = g_slist_next (link)) {
		const ECalComponentPeriod *period = link->data;
		ICalDatetimeperiod *ic_datetimeperiod;
		ICalPeriod *ic_period;
		ICalProperty *prop;
		ICalParameter *param;
		ICalParameterValue value_type = I_CAL_VALUE_PERIOD;
		ICalTime *end;

		if (!period)
			continue;

		ic_period = i_cal_period_new_null_period ();
		ic_datetimeperiod = i_cal_datetimeperiod_new ();

		i_cal_period_set_start (ic_period, e_cal_component_period_get_start (period));

		switch (e_cal_component_period_get_kind (period)) {
		case E_CAL_COMPONENT_PERIOD_DATETIME:
			end = e_cal_component_period_get_end (period);
			if (!end || i_cal_time_is_null_time (end)) {
				i_cal_datetimeperiod_set_time (ic_datetimeperiod, e_cal_component_period_get_start (period));
				if (i_cal_time_is_date (e_cal_component_period_get_start (period))) {
					value_type = I_CAL_VALUE_DATE;
				} else {
					value_type = I_CAL_VALUE_DATETIME;
				}
			} else {
				i_cal_period_set_end (ic_period, e_cal_component_period_get_end (period));
			}
			break;
		case E_CAL_COMPONENT_PERIOD_DURATION:
			i_cal_period_set_duration (ic_period, e_cal_component_period_get_duration (period));
			break;
		}

		i_cal_datetimeperiod_set_period (ic_datetimeperiod, ic_period);

		prop = new_prop_func (ic_datetimeperiod);

		param = i_cal_parameter_new_value (value_type);
		i_cal_property_take_parameter (prop, param);

		i_cal_component_take_property (icalcomp, prop);

		g_object_unref (ic_datetimeperiod);
		g_object_unref (ic_period);
	}
}

static gboolean
extract_exdate_properties_cb (ICalComponent *icalcomp,
			      ICalProperty *prop,
			      gpointer user_data)
{
	GSList **pexdates = user_data;
	ICalTime *tt;

	g_return_val_if_fail (pexdates != NULL, FALSE);

	tt = i_cal_property_get_exdate (prop);
	if (tt) {
		ICalParameter *param;
		gchar *tzid;

		param = i_cal_property_get_first_parameter (prop, I_CAL_TZID_PARAMETER);
		if (param)
			tzid = g_strdup (i_cal_parameter_get_tzid (param));
		else
			tzid = NULL;

		if (tzid && !*tzid) {
			g_free (tzid);
			tzid = NULL;
		}

		*pexdates = g_slist_prepend (*pexdates, e_cal_component_datetime_new_take (tt, tzid));

		g_clear_object (&param);
	}

	return TRUE;
}

/**
 * e_cal_component_get_exdates:
 * @comp: A calendar component object.
 *
 * Queries the list of exception date properties in a calendar component object.
 * Free the returned #GSList with g_slist_free_full (exdates, e_cal_component_datetime_free);,
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable) (element-type ECalComponentDateTime):
 *    the list of exception dates, as a #GSList of #ECalComponentDateTime
 *
 * Since: 3.34
 **/
GSList *
e_cal_component_get_exdates (ECalComponent *comp)
{
	GSList *exdates = NULL;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	foreach_property (comp->priv->icalcomp, I_CAL_EXDATE_PROPERTY,
		extract_exdate_properties_cb, &exdates);

	return g_slist_reverse (exdates);
}

/**
 * e_cal_component_set_exdates:
 * @comp: A calendar component object.
 * @exdate_list: (nullable) (element-type ECalComponentDateTime): List of #ECalComponentDateTime structures.
 *
 * Sets the list of exception dates in a calendar component object.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_exdates (ECalComponent *comp,
			     const GSList *exdate_list)
{
	GSList *link;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	/* Remove old exception dates */
	remove_all_properties_of_kind (comp->priv->icalcomp, I_CAL_EXDATE_PROPERTY);

	/* Add in new exception dates */

	for (link = (GSList *) exdate_list; link; link = g_slist_next (link)) {
		const ECalComponentDateTime *dt = link->data;
		ICalProperty *prop;
		ICalTime *tt;
		const gchar *tzid;

		if (!dt)
			continue;

		tt = e_cal_component_datetime_get_value (dt);
		if (!tt)
			continue;

		tzid = e_cal_component_datetime_get_tzid (dt);

		prop = i_cal_property_new_exdate (tt);

		if (tzid && *tzid) {
			ICalParameter *param;

			param = i_cal_parameter_new_tzid ((gchar *) tzid);
			i_cal_property_take_parameter (prop, param);
		}

		i_cal_component_take_property (comp->priv->icalcomp, prop);
	}

	comp->priv->need_sequence_inc = TRUE;
}

/**
 * e_cal_component_has_exdates:
 * @comp: A calendar component object.
 *
 * Queries whether a calendar component object has any exception dates defined
 * for it.
 *
 * Returns: TRUE if the component has exception dates, FALSE otherwise.
 *
 * Since: 3.34
 **/
gboolean
e_cal_component_has_exdates (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, FALSE);

	return e_cal_util_component_has_property (comp->priv->icalcomp, I_CAL_EXDATE_PROPERTY);
}

/* Gets a list of recurrence rules */
static GSList *
get_recur_list (ICalComponent *icalcomp,
		ICalPropertyKind prop_kind,
                ICalRecurrence * (* get_prop_func) (ICalProperty *prop))
{
	GSList *props, *link, *recurs = NULL;

	props = gather_all_properties (icalcomp, prop_kind, FALSE);

	for (link = props; link; link = g_slist_next (link)) {
		ICalProperty *prop = link->data;
		ICalRecurrence *rt;

		rt = get_prop_func (prop);
		if (rt)
			recurs = g_slist_prepend (recurs, rt);
	}

	g_slist_free_full (props, g_object_unref);

	/* No need to reverse it, the props are in reverse order
	   and processed in the reverse order, thus the result
	   is in the expected order. */
	return recurs;
}

/* Sets a list of recurrence rules */
static void
set_recur_list (ICalComponent *icalcomp,
		ICalPropertyKind prop_kind,
		ICalProperty * (* new_prop_func) (ICalRecurrence *recur),
		const GSList *rl) /* ICalRecurrence * */
{
	GSList *link;

	/* Remove old recurrences */
	remove_all_properties_of_kind (icalcomp, prop_kind);

	/* Add in new recurrences */

	for (link = (GSList *) rl; link; link = g_slist_next (link)) {
		ICalProperty *prop;
		ICalRecurrence *recur = link->data;

		if (recur) {
			prop = (* new_prop_func) (recur);
			i_cal_component_take_property (icalcomp, prop);
		}
	}
}

/**
 * e_cal_component_get_exrules:
 * @comp: A calendar component object.
 *
 * Queries the list of exception rule properties of a calendar component
 * object. Free the returned list with g_slist_free_full (slist, g_object_unref);,
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable) (element-type ICalRecurrence): a #GSList
 *    of exception rules as #ICalRecurrence structures, or %NULL, when none exist.
 *
 * Since: 3.34
 **/
GSList *
e_cal_component_get_exrules (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	return get_recur_list (comp->priv->icalcomp, I_CAL_EXRULE_PROPERTY, i_cal_property_get_exrule);
}

/**
 * e_cal_component_get_exrule_properties:
 * @comp: A calendar component object.
 *
 * Queries the list of exception rule properties of a calendar component object.
 * Free the list with g_slist_free_full (slist, g_object_unref);, when
 * no longer needed.
 *
 * Returns: (transfer full) (nullable) (element-type ICalProperty): a list of exception
 *    rule properties
 *
 * Since: 3.34
 **/
GSList *
e_cal_component_get_exrule_properties (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	return gather_all_properties (comp->priv->icalcomp, I_CAL_EXRULE_PROPERTY, TRUE);
}

/**
 * e_cal_component_set_exrules:
 * @comp: A calendar component object.
 * @recur_list: (nullable) (element-type ICalRecurrence): a #GSList
 *    of #ICalRecurrence structures, or %NULL.
 *
 * Sets the list of exception rules in a calendar component object.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_exrules (ECalComponent *comp,
			     const GSList *recur_list)
{
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	set_recur_list (comp->priv->icalcomp, I_CAL_EXRULE_PROPERTY, i_cal_property_new_exrule, recur_list);

	comp->priv->need_sequence_inc = TRUE;
}

/**
 * e_cal_component_has_exrules:
 * @comp: A calendar component object.
 *
 * Queries whether a calendar component object has any exception rules defined
 * for it.
 *
 * Returns: TRUE if the component has exception rules, FALSE otherwise.
 *
 * Since: 3.34
 **/
gboolean
e_cal_component_has_exrules (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, FALSE);

	return e_cal_util_component_has_property (comp->priv->icalcomp, I_CAL_EXRULE_PROPERTY);
}

/**
 * e_cal_component_has_exceptions:
 * @comp: A calendar component object
 *
 * Queries whether a calendar component object has any exception dates
 * or exception rules.
 *
 * Returns: TRUE if the component has exceptions, FALSE otherwise.
 *
 * Since: 3.34
 **/
gboolean
e_cal_component_has_exceptions (ECalComponent *comp)
{
	return e_cal_component_has_exdates (comp) || e_cal_component_has_exrules (comp);
}

/**
 * e_cal_component_get_geo:
 * @comp: A calendar component object.
 *
 * Gets the geographic position property of a calendar component object.
 * Free the returned non-NULL object with g_object_unref(), when
 * no longer needed.
 *
 * Returns: (transfer full) (nullable): the geographic position as #ICalGeo,
 *    or %NULL, when none set.
 *
 * Since: 3.34
 **/
ICalGeo *
e_cal_component_get_geo (ECalComponent *comp)
{
	ICalProperty *prop;
	ICalGeo *geo;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_GEO_PROPERTY);
	if (!prop)
		return NULL;

	geo = i_cal_property_get_geo (prop);

	g_object_unref (prop);

	return geo;
}

/**
 * e_cal_component_set_geo:
 * @comp: A calendar component object.
 * @geo: (nullable): Value for the geographic position property, or %NULL to unset.
 *
 * Sets the geographic position property on a calendar component object.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_geo (ECalComponent *comp,
			 const ICalGeo *geo)
{
	ICalProperty *prop;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_GEO_PROPERTY);

	if (!geo) {
		if (prop) {
			i_cal_component_remove_property (comp->priv->icalcomp, prop);
			g_clear_object (&prop);
		}

		return;
	}

	if (prop) {
		i_cal_property_set_geo (prop, (ICalGeo *) geo);
	} else {
		prop = i_cal_property_new_geo ((ICalGeo *) geo);
		i_cal_component_add_property (comp->priv->icalcomp, prop);
	}

	g_clear_object (&prop);
}

/**
 * e_cal_component_get_last_modified:
 * @comp: A calendar component object.
 *
 * Queries the time at which a calendar component object was last modified in
 * the calendar store. Free the returned non-NULL pointer with g_object_unref(),
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable): the last modified time, as an
 * #ICalTime, or %NULL, when none is set
 *
 * Since: 3.34
 **/
ICalTime *
e_cal_component_get_last_modified (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	return get_icaltimetype (comp->priv->icalcomp, I_CAL_LASTMODIFIED_PROPERTY, i_cal_property_get_lastmodified);
}

/**
 * e_cal_component_set_last_modified:
 * @comp: A calendar component object.
 * @tt: (nullable): Value for the last time modified.
 *
 * Sets the time at which a calendar component object was last stored in the
 * calendar store.  This should not be called by plain calendar user agents.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_last_modified (ECalComponent *comp,
				   const ICalTime *tt)
{
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	set_icaltimetype (comp->priv->icalcomp, I_CAL_LASTMODIFIED_PROPERTY,
		i_cal_property_new_lastmodified,
		i_cal_property_set_lastmodified,
		tt);
}

/**
 * e_cal_component_get_organizer:
 * @comp:  A calendar component object
 *
 * Queries the organizer property of a calendar component object.
 * Free the returned structure with e_cal_component_organizer_free(),
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable): an #ECalComponentOrganizer structure
 *    destribing the organizer, or %NULL, when none exists.
 *
 * Since: 3.34
 **/
ECalComponentOrganizer *
e_cal_component_get_organizer (ECalComponent *comp)
{
	ECalComponentOrganizer *organizer;
	ICalProperty *prop;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_ORGANIZER_PROPERTY);
	if (!prop)
		return NULL;

	organizer = e_cal_component_organizer_new_from_property (prop);

	g_object_unref (prop);

	return organizer;
}

/**
 * e_cal_component_set_organizer:
 * @comp:  A calendar component object.
 * @organizer: (nullable): Value for the organizer property, as an #ECalComponentOrganizer
 *
 * Sets the organizer of a calendar component object
 *
 * Since: 3.34
 **/
void
e_cal_component_set_organizer (ECalComponent *comp,
			       const ECalComponentOrganizer *organizer)
{
	ICalProperty *prop;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_ORGANIZER_PROPERTY);

	if (!organizer) {
		if (prop) {
			i_cal_component_remove_property (comp->priv->icalcomp, prop);
			g_clear_object (&prop);
		}

		return;
	}

	if (!prop) {
		prop = i_cal_property_new (I_CAL_ORGANIZER_PROPERTY);
		i_cal_component_add_property (comp->priv->icalcomp, prop);
	}

	e_cal_component_organizer_fill_property (organizer, prop);

	g_clear_object (&prop);
}

/**
 * e_cal_component_has_organizer:
 * @comp: A calendar component object.
 *
 * Check whether a calendar component object has an organizer or not.
 *
 * Returns: TRUE if there is an organizer, FALSE otherwise.
 *
 * Since: 3.34
 **/
gboolean
e_cal_component_has_organizer (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);

	return e_cal_util_component_has_property (comp->priv->icalcomp, I_CAL_ORGANIZER_PROPERTY);
}

/**
 * e_cal_component_get_percent_complete:
 * @comp: A calendar component object.
 *
 * Queries the percent-complete property of a calendar component object.
 *
 * Returns: the percent-complete property value, or -1 if not found
 *
 * Since: 3.34
 **/
gint
e_cal_component_get_percent_complete (ECalComponent *comp)
{
	ICalProperty *prop;
	gint percent;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), -1);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, -1);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_PERCENTCOMPLETE_PROPERTY);
	if (!prop)
		return -1;

	percent = i_cal_property_get_percentcomplete (prop);

	g_object_unref (prop);

	return percent;
}

/**
 * e_cal_component_set_percent_complete:
 * @comp: an #ECalComponent
 * @percent: a percent to set, or -1 to remove the property
 *
 * Sets percent complete. The @percent can be between 0 and 100, inclusive.
 * A special value -1 can be used to remove the percent complete property.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_percent_complete (ECalComponent *comp,
				      gint percent)
{
	ICalProperty *prop;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);
	g_return_if_fail (percent >= -1 && percent <= 100);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_PERCENTCOMPLETE_PROPERTY);

	if (percent == -1) {
		if (prop) {
			i_cal_component_remove_property (comp->priv->icalcomp, prop);
			g_clear_object (&prop);
		}

		return;
	}

	if (prop) {
		i_cal_property_set_percentcomplete (prop, percent);
		g_clear_object (&prop);
	} else {
		prop = i_cal_property_new_percentcomplete (percent);
		i_cal_component_take_property (comp->priv->icalcomp, prop);
	}
}

/**
 * e_cal_component_get_priority:
 * @comp: A calendar component object.
 *
 * Queries the priority property of a calendar component object.
 *
 * Returns: the priority property value, or -1, if not found
 *
 * Since: 3.34
 **/
gint
e_cal_component_get_priority (ECalComponent *comp)
{
	ICalProperty *prop;
	gint priority;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), -1);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, -1);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_PRIORITY_PROPERTY);
	if (!prop)
		return -1;

	priority = i_cal_property_get_priority (prop);

	g_object_unref (prop);

	return priority;
}

/**
 * e_cal_component_set_priority:
 * @comp: A calendar component object.
 * @priority: Value for the priority property.
 *
 * Sets the priority property of a calendar component object.
 * The @priority can be between 0 and 9, inclusive.
 * A special value -1 can be used to remove the priority property.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_priority (ECalComponent *comp,
			      gint priority)
{
	ICalProperty *prop;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);
	g_return_if_fail (priority >= -1 && priority <= 9);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_PRIORITY_PROPERTY);

	if (priority == -1) {
		if (prop) {
			i_cal_component_remove_property (comp->priv->icalcomp, prop);
			g_clear_object (&prop);
		}

		return;
	}

	if (prop) {
		i_cal_property_set_priority (prop, priority);
		g_clear_object (&prop);
	} else {
		prop = i_cal_property_new_priority (priority);
		i_cal_component_take_property (comp->priv->icalcomp, prop);
	}
}

/**
 * e_cal_component_get_recurid:
 * @comp: A calendar component object.
 *
 * Queries the recurrence id property of a calendar component object.
 * Free the returned #ECalComponentRange with e_cal_component_range_free(),
 * whe no longer needed.
 *
 * Returns: (transfer full) (nullable): the recurrence id property, as an #ECalComponentRange
 *
 * Since: 3.34
 **/
ECalComponentRange *
e_cal_component_get_recurid (ECalComponent *comp)
{
	ECalComponentDateTime *dt;
	ECalComponentRangeKind range_kind;
	ICalProperty *prop = NULL;
	ICalParameter *param;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	dt = get_datetime (comp->priv->icalcomp, I_CAL_RECURRENCEID_PROPERTY, i_cal_property_get_recurrenceid, &prop);

	if (!dt) {
		g_clear_object (&prop);
		return NULL;
	}

	range_kind = E_CAL_COMPONENT_RANGE_SINGLE;
	param = i_cal_property_get_first_parameter (prop, I_CAL_RANGE_PARAMETER);

	/* RFC 5545 says it can use only THIS_AND_FUTURE here */
	if (param && i_cal_parameter_get_range (param) == I_CAL_RANGE_THISANDFUTURE)
		range_kind = E_CAL_COMPONENT_RANGE_THISFUTURE;

	g_clear_object (&param);
	g_clear_object (&prop);

	return e_cal_component_range_new_take (range_kind, dt);
}

/**
 * e_cal_component_get_recurid_as_string:
 * @comp: A calendar component object.
 *
 * Gets the recurrence ID property as a string.
 *
 * Returns: the recurrence ID as a string.
 *
 * Since: 3.34
 **/
gchar *
e_cal_component_get_recurid_as_string (ECalComponent *comp)
{
	return e_cal_util_component_get_recurid_as_string (comp->priv->icalcomp);
}

/**
 * e_cal_component_set_recurid:
 * @comp: A calendar component object.
 * @recur_id: (nullable): Value for the recurrence id property, or %NULL, to remove the property.
 *
 * Sets the recurrence id property of a calendar component object.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_recurid (ECalComponent *comp,
			     const ECalComponentRange *recur_id)
{
	ECalComponentDateTime *dt;
	ICalProperty *prop = NULL;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	dt = recur_id ? e_cal_component_range_get_datetime (recur_id) : NULL;

	set_datetime (comp->priv->icalcomp, I_CAL_RECURRENCEID_PROPERTY,
		i_cal_property_new_recurrenceid,
		i_cal_property_set_recurrenceid,
		dt,
		&prop);

	if (prop) {
		ICalParameter *param;

		param = i_cal_property_get_first_parameter (prop, I_CAL_RANGE_PARAMETER);

		/* RFC 5545 says it can use only THIS_AND_FUTURE here */
		if (e_cal_component_range_get_kind (recur_id) == E_CAL_COMPONENT_RANGE_THISFUTURE) {
			if (param) {
				i_cal_parameter_set_range (param, I_CAL_RANGE_THISANDFUTURE);
			} else {
				param = i_cal_parameter_new_range (I_CAL_RANGE_THISANDFUTURE);
				i_cal_property_add_parameter (prop, param);
			}
		} else if (param) {
			i_cal_property_remove_parameter_by_ref (prop, param);
		}

		g_clear_object (&param);
		g_clear_object (&prop);
	}
}

/**
 * e_cal_component_get_rdates:
 * @comp: A calendar component object.
 *
 * Queries the list of recurrence date properties in a calendar component
 * object. Free the returned #GSList with g_slist_free_full (slist, e_cal_component_period_free);,
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable) (element-type ECalComponentPeriod): the list
 *    of recurrence dates, as a #GSList of #ECalComponentPeriod structures.
 *
 * Since: 3.34
 **/
GSList *
e_cal_component_get_rdates (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	return get_period_list (comp->priv->icalcomp, I_CAL_RDATE_PROPERTY, i_cal_property_get_rdate);
}

/**
 * e_cal_component_set_rdates:
 * @comp: A calendar component object.
 * @rdate_list: (nullable) (element-type ECalComponentPeriod): List of
 *    #ECalComponentPeriod structures, or %NULL to set none
 *
 * Sets the list of recurrence dates in a calendar component object.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_rdates (ECalComponent *comp,
			    const GSList *rdate_list)
{
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	set_period_list (comp->priv->icalcomp, I_CAL_RDATE_PROPERTY, i_cal_property_new_rdate, rdate_list);

	comp->priv->need_sequence_inc = TRUE;
}

/**
 * e_cal_component_has_rdates:
 * @comp: A calendar component object.
 *
 * Queries whether a calendar component object has any recurrence dates defined
 * for it.
 *
 * Returns: TRUE if the component has recurrence dates, FALSE otherwise.
 *
 * Since: 3.34
 **/
gboolean
e_cal_component_has_rdates (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, FALSE);

	return e_cal_util_component_has_property (comp->priv->icalcomp, I_CAL_RDATE_PROPERTY);
}

/**
 * e_cal_component_get_rrules:
 * @comp: A calendar component object.
 *
 * Queries the list of recurrence rule properties of a calendar component
 * object. Free the returned list with g_slist_free_full (slist, g_object_unref);,
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable) (element-type ICalRecurrence): a #GSList
 *    of recurrence rules as #ICalRecurrence structures, or %NULL, when none exist.
 *
 * Since: 3.34
 **/
GSList *
e_cal_component_get_rrules (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	return get_recur_list (comp->priv->icalcomp, I_CAL_RRULE_PROPERTY, i_cal_property_get_rrule);
}

/**
 * e_cal_component_get_rrule_properties:
 * @comp: A calendar component object.
 *
 * Queries a list of recurrence rule properties of a calendar component object.
 * Free the list with g_slist_free_full (slist, g_object_unref);, when
 * no longer needed.
 *
 * Returns: (transfer full) (nullable) (element-type ICalProperty): a list of recurrence
 *    rule properties
 *
 * Since: 3.34
 **/
GSList *
e_cal_component_get_rrule_properties (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	return gather_all_properties (comp->priv->icalcomp, I_CAL_RRULE_PROPERTY, TRUE);
}

/**
 * e_cal_component_set_rrules:
 * @comp: A calendar component object.
 * @recur_list: (nullable) (element-type ICalRecurrence): List of #ICalRecurrence structures, or %NULL.
 *
 * Sets the list of recurrence rules in a calendar component object.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_rrules (ECalComponent *comp,
			    const GSList *recur_list)
{
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	set_recur_list (comp->priv->icalcomp, I_CAL_RRULE_PROPERTY, i_cal_property_new_rrule, recur_list);

	comp->priv->need_sequence_inc = TRUE;
}

/**
 * e_cal_component_has_rrules:
 * @comp: A calendar component object.
 *
 * Queries whether a calendar component object has any recurrence rules defined
 * for it.
 *
 * Returns: TRUE if the component has recurrence rules, FALSE otherwise.
 *
 * Since: 3.34
 **/
gboolean
e_cal_component_has_rrules (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, FALSE);

	return e_cal_util_component_has_property (comp->priv->icalcomp, I_CAL_RRULE_PROPERTY);
}

/**
 * e_cal_component_has_recurrences:
 * @comp: A calendar component object
 *
 * Queries whether a calendar component object has any recurrence dates or
 * recurrence rules.
 *
 * Returns: TRUE if the component has recurrences, FALSE otherwise.
 *
 * Since: 3.34
 **/
gboolean
e_cal_component_has_recurrences (ECalComponent *comp)
{
	return e_cal_component_has_rdates (comp) || e_cal_component_has_rrules (comp);
}

/* Counts the elements in the by_xxx fields of an ICalRecurrence;
   it also frees the 'field' array*/
static gint
count_by_xxx_and_free (GArray *field) /* gshort */
{
	gint ii;

	if (!field)
		return 0;

	for (ii = 0; ii < field->len; ii++) {
		if (g_array_index (field, gshort, ii) == I_CAL_RECURRENCE_ARRAY_MAX)
			break;
	}

	g_array_unref (field);

	return ii;
}

/**
 * e_cal_component_has_simple_recurrence:
 * @comp: A calendar component object.
 *
 * Checks whether the given calendar component object has simple recurrence
 * rules or more complicated ones.
 *
 * Returns: TRUE if it has a simple recurrence rule, FALSE otherwise.
 *
 * Since: 3.34
 **/
gboolean
e_cal_component_has_simple_recurrence (ECalComponent *comp)
{
	GSList *rrule_list;
	ICalRecurrence *rt;
	gint n_by_second, n_by_minute, n_by_hour;
	gint n_by_day, n_by_month_day, n_by_year_day;
	gint n_by_week_no, n_by_month, n_by_set_pos;
	gint len, i;
	gboolean simple = FALSE;

	if (!e_cal_component_has_recurrences (comp))
		return TRUE;

	rrule_list = e_cal_component_get_rrules (comp);
	len = g_slist_length (rrule_list);
	if (len > 1 || !rrule_list ||
	    e_cal_component_has_rdates (comp) ||
	    e_cal_component_has_exrules (comp))
		goto cleanup;

	/* Down to one rule, so test that one */
	rt = rrule_list->data;

	/* Any funky frequency? */
	if (i_cal_recurrence_get_freq (rt) == I_CAL_SECONDLY_RECURRENCE ||
	    i_cal_recurrence_get_freq (rt) == I_CAL_MINUTELY_RECURRENCE ||
	    i_cal_recurrence_get_freq (rt) == I_CAL_HOURLY_RECURRENCE)
		goto cleanup;

	/* Any funky BY_* */
#define N_HAS_BY(field) (count_by_xxx_and_free (field))

	n_by_second = N_HAS_BY (i_cal_recurrence_get_by_second_array (rt));
	n_by_minute = N_HAS_BY (i_cal_recurrence_get_by_minute_array (rt));
	n_by_hour = N_HAS_BY (i_cal_recurrence_get_by_hour_array (rt));
	n_by_day = N_HAS_BY (i_cal_recurrence_get_by_day_array (rt));
	n_by_month_day = N_HAS_BY (i_cal_recurrence_get_by_month_day_array (rt));
	n_by_year_day = N_HAS_BY (i_cal_recurrence_get_by_year_day_array (rt));
	n_by_week_no = N_HAS_BY (i_cal_recurrence_get_by_week_no_array (rt));
	n_by_month = N_HAS_BY (i_cal_recurrence_get_by_month_array (rt));
	n_by_set_pos = N_HAS_BY (i_cal_recurrence_get_by_set_pos_array (rt));

	if (n_by_second != 0
	    || n_by_minute != 0
	    || n_by_hour != 0)
		goto cleanup;

	switch (i_cal_recurrence_get_freq (rt)) {
	case I_CAL_DAILY_RECURRENCE:
		if (n_by_day != 0
		    || n_by_month_day != 0
		    || n_by_year_day != 0
		    || n_by_week_no != 0
		    || n_by_month != 0
		    || n_by_set_pos != 0)
			goto cleanup;

		simple = TRUE;
		break;

	case I_CAL_WEEKLY_RECURRENCE:
		if (n_by_month_day != 0
		    || n_by_year_day != 0
		    || n_by_week_no != 0
		    || n_by_month != 0
		    || n_by_set_pos != 0)
			goto cleanup;

		for (i = 0; i < 8; i++) {
			gint pos;
			gshort byday = i_cal_recurrence_get_by_day (rt, i);

			if (byday == I_CAL_RECURRENCE_ARRAY_MAX)
				break;

			pos = i_cal_recurrence_day_position (byday);

			if (pos != 0)
				goto cleanup;
		}

		simple = TRUE;
		break;

	case I_CAL_MONTHLY_RECURRENCE:
		if (n_by_year_day != 0
		    || n_by_week_no != 0
		    || n_by_month != 0
		    || n_by_set_pos > 1)
			goto cleanup;

		if (n_by_month_day == 1) {
			gint nth;

			if (n_by_set_pos != 0)
				goto cleanup;

			nth = i_cal_recurrence_get_by_month_day (rt, 0);
			if (nth < 1 && nth != -1)
				goto cleanup;

		} else if (n_by_day == 1) {
			ICalRecurrenceWeekday weekday;
			gint pos;

			/* Outlook 2000 uses BYDAY=TU;BYSETPOS=2, and will not
			 * accept BYDAY=2TU. So we now use the same as Outlook
			 * by default. */

			weekday = i_cal_recurrence_day_day_of_week (i_cal_recurrence_get_by_day (rt, 0));
			pos = i_cal_recurrence_day_position (i_cal_recurrence_get_by_day (rt, 0));

			if (pos == 0) {
				if (n_by_set_pos != 1)
					goto cleanup;
				pos = i_cal_recurrence_get_by_set_pos (rt, 0);
			}

			if (pos < 0)
				goto cleanup;

			switch (weekday) {
			case I_CAL_MONDAY_WEEKDAY:
			case I_CAL_TUESDAY_WEEKDAY:
			case I_CAL_WEDNESDAY_WEEKDAY:
			case I_CAL_THURSDAY_WEEKDAY:
			case I_CAL_FRIDAY_WEEKDAY:
			case I_CAL_SATURDAY_WEEKDAY:
			case I_CAL_SUNDAY_WEEKDAY:
				break;

			default:
				goto cleanup;
			}
		} else {
			goto cleanup;
		}

		simple = TRUE;
		break;

	case I_CAL_YEARLY_RECURRENCE:
		if (n_by_day != 0
		    || n_by_month_day != 0
		    || n_by_year_day != 0
		    || n_by_week_no != 0
		    || n_by_month != 0
		    || n_by_set_pos != 0)
			goto cleanup;

		simple = TRUE;
		break;

	default:
		goto cleanup;
	}

 cleanup:
	g_slist_free_full (rrule_list, g_object_unref);

	return simple;
}

/**
 * e_cal_component_is_instance:
 * @comp: A calendar component object.
 *
 * Checks whether a calendar component object is an instance of a recurring
 * event.
 *
 * Returns: TRUE if it is an instance, FALSE if not.
 *
 * Since: 3.34
 **/
gboolean
e_cal_component_is_instance (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);

	return e_cal_util_component_has_property (comp->priv->icalcomp, I_CAL_RECURRENCEID_PROPERTY);
}

/**
 * e_cal_component_get_sequence:
 * @comp: A calendar component object.
 *
 * Queries the sequence number of a calendar component object.
 *
 * Returns: the sequence number, or -1 if not found
 *
 * Since: 3.34
 **/
gint
e_cal_component_get_sequence (ECalComponent *comp)
{
	ICalProperty *prop;
	gint sequence;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), -1);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, -1);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_SEQUENCE_PROPERTY);
	if (!prop)
		return -1;

	sequence = i_cal_property_get_sequence (prop);

	g_object_unref (prop);

	return sequence;
}

/**
 * e_cal_component_set_sequence:
 * @comp: A calendar component object.
 * @sequence: a sequence number to set, or -1 to remove the property
 *
 * Sets the sequence number of a calendar component object.
 * A special value -1 can be used to remove the sequence number property.
 *
 * Normally this function should not be called, since the sequence number
 * is incremented automatically at the proper times.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_sequence (ECalComponent *comp,
			      gint sequence)
{
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	comp->priv->need_sequence_inc = FALSE;

	if (sequence <= -1) {
		ICalProperty *prop;

		prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_SEQUENCE_PROPERTY);
		if (prop) {
			i_cal_component_remove_property (comp->priv->icalcomp, prop);
			g_object_unref (prop);
		}

		return;
	}

	i_cal_component_set_sequence (comp->priv->icalcomp, sequence);
}

/**
 * e_cal_component_get_status:
 * @comp: A calendar component object.
 *
 * Queries the status property of a calendar component object.
 *
 * Returns: the status value; or %I_CAL_STATUS_NONE, if the component
 *   has no status property
 *
 * Since: 3.34
 **/
ICalPropertyStatus
e_cal_component_get_status (ECalComponent *comp)
{
	ICalProperty *prop;
	ICalPropertyStatus status;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), I_CAL_STATUS_NONE);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, I_CAL_STATUS_NONE);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_STATUS_PROPERTY);

	if (!prop)
		return I_CAL_STATUS_NONE;

	status = i_cal_property_get_status (prop);

	g_object_unref (prop);

	return status;
}

/**
 * e_cal_component_set_status:
 * @comp: A calendar component object.
 * @status: Status value, as an #ICalPropertyStatus. Use %I_CAL_STATUS_NONE, to unset the property
 *
 * Sets the status property of a calendar component object.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_status (ECalComponent *comp,
			    ICalPropertyStatus status)
{
	ICalProperty *prop;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	comp->priv->need_sequence_inc = TRUE;

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_STATUS_PROPERTY);

	if (status == I_CAL_STATUS_NONE) {
		if (prop) {
			i_cal_component_remove_property (comp->priv->icalcomp, prop);
			g_object_unref (prop);
		}

		return;
	}

	if (prop) {
		i_cal_property_set_status (prop, status);
		g_object_unref (prop);
	} else {
		prop = i_cal_property_new_status (status);
		i_cal_component_take_property (comp->priv->icalcomp, prop);
	}
}

/**
 * e_cal_component_get_summary:
 * @comp: A calendar component object.
 *
 * Queries the summary of a calendar component object.
 * Free the returned pointer withe_cal_component_text_free(),
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable): the summary, as an #ECalComponentText,
 *    or %NULL, when none is set
 *
 * Since: 3.34
 **/
ECalComponentText *
e_cal_component_get_summary (ECalComponent *comp)
{
	ECalComponentText *text;
	ICalProperty *prop;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_SUMMARY_PROPERTY);
	if (!prop)
		return NULL;

	text = get_text_from_prop (prop, i_cal_property_get_summary);

	g_object_unref (prop);

	return text;
}

typedef struct {
	gchar *old_summary;
	const gchar *new_summary;
} SetAlarmDescriptionData;

static gboolean
set_alarm_description_cb (ICalComponent *icalcomp,
			  ICalComponent *subcomp,
			  gpointer user_data)
{
	ICalProperty *icalprop, *desc_prop;
	SetAlarmDescriptionData *sadd = user_data;
	gboolean changed = FALSE;
	const gchar *old_summary = NULL;

	g_return_val_if_fail (sadd != NULL, FALSE);

	/* set the new description on the alarm */
	desc_prop = i_cal_component_get_first_property (subcomp, I_CAL_DESCRIPTION_PROPERTY);
	if (desc_prop) {
		old_summary = i_cal_property_get_description (desc_prop);
	} else {
		desc_prop = i_cal_property_new_description (sadd->new_summary);
	}

	/* remove the X-EVOLUTION-NEEDS_DESCRIPTION property */
	icalprop = i_cal_component_get_first_property (subcomp, I_CAL_X_PROPERTY);
	while (icalprop) {
		const gchar *x_name;

		x_name = i_cal_property_get_x_name (icalprop);
		if (!g_strcmp0 (x_name, "X-EVOLUTION-NEEDS-DESCRIPTION")) {
			i_cal_component_remove_property (subcomp, icalprop);
			g_object_unref (icalprop);

			i_cal_property_set_description (desc_prop, sadd->new_summary);
			changed = TRUE;
			break;
		}

		g_object_unref (icalprop);
		icalprop = i_cal_component_get_next_property (subcomp, I_CAL_X_PROPERTY);
	}

	if (!changed) {
		if (!g_strcmp0 (old_summary ? old_summary : "", sadd->old_summary ? sadd->old_summary : "")) {
			i_cal_property_set_description (desc_prop, sadd->new_summary);
		}
	}

	g_object_unref (desc_prop);

	return TRUE;
}

/**
 * e_cal_component_set_summary:
 * @comp: A calendar component object.
 * @summary: Summary property and its parameters.
 *
 * Sets the summary of a calendar component object.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_summary (ECalComponent *comp,
			     const ECalComponentText *summary)
{
	ICalProperty *prop;
	SetAlarmDescriptionData sadd;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_SUMMARY_PROPERTY);

	if (!summary) {
		if (prop) {
			i_cal_component_remove_property (comp->priv->icalcomp, prop);
			g_object_unref (prop);
		}

		return;
	}

	if (!e_cal_component_text_get_value (summary)) {
		g_clear_object (&prop);
		return;
	}

	if (prop) {
		/* Make a copy, to avoid use-after-free */
		sadd.old_summary = g_strdup (i_cal_property_get_summary (prop));
		i_cal_property_set_summary (prop, (gchar *) e_cal_component_text_get_value (summary));
		set_text_altrep_on_prop (prop, summary);
	} else {
		sadd.old_summary = NULL;
		prop = i_cal_property_new_summary ((gchar *) e_cal_component_text_get_value (summary));
		set_text_altrep_on_prop (prop, summary);
		i_cal_component_take_property (comp->priv->icalcomp, prop);
		prop = NULL;
	}

	g_clear_object (&prop);

	/* look for alarms that need a description */
	sadd.new_summary = e_cal_component_text_get_value (summary);

	foreach_subcomponent (comp->priv->icalcomp, I_CAL_VALARM_COMPONENT, set_alarm_description_cb, &sadd);

	g_free (sadd.old_summary);
}

/**
 * e_cal_component_get_transparency:
 * @comp: A calendar component object.
 *
 * Queries the time transparency of a calendar component object.
 *
 * Returns: the time transparency, as an #ECalComponentTransparency;
 *    value #E_CAL_COMPONENT_TRANSP_NONE is returned when none is set
 *
 * Since: 3.34
 **/
ECalComponentTransparency
e_cal_component_get_transparency (ECalComponent *comp)
{
	ECalComponentTransparency transp;
	ICalProperty *prop;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), E_CAL_COMPONENT_TRANSP_NONE);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, E_CAL_COMPONENT_TRANSP_NONE);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_TRANSP_PROPERTY);

	if (!prop)
		return E_CAL_COMPONENT_TRANSP_NONE;

	switch (i_cal_property_get_transp (prop)) {
	case I_CAL_TRANSP_TRANSPARENT:
	case I_CAL_TRANSP_TRANSPARENTNOCONFLICT:
		transp = E_CAL_COMPONENT_TRANSP_TRANSPARENT;
		break;

	case I_CAL_TRANSP_OPAQUE:
	case I_CAL_TRANSP_OPAQUENOCONFLICT:
		transp = E_CAL_COMPONENT_TRANSP_OPAQUE;
		break;

	default:
		transp = E_CAL_COMPONENT_TRANSP_UNKNOWN;
		break;
	}

	g_object_unref (prop);

	return transp;
}

/**
 * e_cal_component_set_transparency:
 * @comp: A calendar component object.
 * @transp: Time transparency value.
 *
 * Sets the time transparency of a calendar component object.
 * Use %E_CAL_COMPONENT_TRANSP_NONE to unset the property.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_transparency (ECalComponent *comp,
                                  ECalComponentTransparency transp)
{
	ICalProperty *prop;
	ICalPropertyTransp ical_transp;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (transp != E_CAL_COMPONENT_TRANSP_UNKNOWN);
	g_return_if_fail (comp->priv->icalcomp != NULL);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_TRANSP_PROPERTY);

	if (transp == E_CAL_COMPONENT_TRANSP_NONE) {
		if (prop) {
			i_cal_component_remove_property (comp->priv->icalcomp, prop);
			g_object_unref (prop);
		}

		return;
	}

	switch (transp) {
	case E_CAL_COMPONENT_TRANSP_TRANSPARENT:
		ical_transp = I_CAL_TRANSP_TRANSPARENT;
		break;

	case E_CAL_COMPONENT_TRANSP_OPAQUE:
		ical_transp = I_CAL_TRANSP_OPAQUE;
		break;

	default:
		g_warn_if_reached ();
		ical_transp = I_CAL_TRANSP_NONE;
		break;
	}

	if (prop) {
		i_cal_property_set_transp (prop, ical_transp);
		g_object_unref (prop);
	} else {
		prop = i_cal_property_new_transp (ical_transp);
		i_cal_component_take_property (comp->priv->icalcomp, prop);
	}
}

/**
 * e_cal_component_get_url:
 * @comp: A calendar component object.
 *
 * Queries the uniform resource locator property of a calendar component object.
 * Free the returned URL with g_free(), when no longer needed.
 *
 * Returns: (transfer full) (nullable): the URL, or %NULL, when none is set
 *
 * Since: 3.34
 **/
gchar *
e_cal_component_get_url (ECalComponent *comp)
{
	ICalProperty *prop;
	gchar *url;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_URL_PROPERTY);
	if (!prop)
		return NULL;

	url = g_strdup (i_cal_property_get_url (prop));

	g_object_unref (prop);

	return url;
}

/**
 * e_cal_component_set_url:
 * @comp: A calendar component object.
 * @url: (nullable): URL value.
 *
 * Sets the uniform resource locator property of a calendar component object.
 * A %NULL or an empty string removes the property.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_url (ECalComponent *comp,
                         const gchar *url)
{
	ICalProperty *prop;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_URL_PROPERTY);

	if (!url || !(*url)) {
		if (prop) {
			i_cal_component_remove_property (comp->priv->icalcomp, prop);
			g_object_unref (prop);
		}

		return;
	}

	if (prop) {
		i_cal_property_set_url (prop, (gchar *) url);
		g_object_unref (prop);
	} else {
		prop = i_cal_property_new_url ((gchar *) url);
		i_cal_component_take_property (comp->priv->icalcomp, prop);
	}
}

static gboolean
get_attendee_list_cb (ICalComponent *icalcomp,
		      ICalProperty *prop,
		      gpointer user_data)
{
	GSList **pattendees = user_data;
	ECalComponentAttendee *attendee;

	g_return_val_if_fail (pattendees != NULL, FALSE);

	attendee = e_cal_component_attendee_new_from_property (prop);

	if (attendee)
		*pattendees = g_slist_prepend (*pattendees, attendee);

	return TRUE;
}

/**
 * e_cal_component_get_attendees:
 * @comp: A calendar component object.
 *
 * Queries the attendee properties of the calendar component object.
 * Free the returned #GSList with g_slist_free_full (slist, e_cal_component_attendee_free);,
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable) (element-type ECalComponentAttendee):
 *    the attendees, as a #GSList of an #ECalComponentAttendee, or %NULL,
 *    when none are set
 *
 * Since: 3.34
 **/
GSList *
e_cal_component_get_attendees (ECalComponent *comp)
{
	GSList *attendees = NULL;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	foreach_property (comp->priv->icalcomp, I_CAL_ATTENDEE_PROPERTY, get_attendee_list_cb, &attendees);

	return g_slist_reverse (attendees);
}

/**
 * e_cal_component_set_attendees:
 * @comp: A calendar component object.
 * @attendee_list: (nullable) (element-type ECalComponentAttendee): Values for attendee
 *    properties, or %NULL to unset
 *
 * Sets the attendees of a calendar component object
 *
 * Since: 3.34
 **/
void
e_cal_component_set_attendees (ECalComponent *comp,
			       const GSList *attendee_list)
{
	GSList *link;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	remove_all_properties_of_kind (comp->priv->icalcomp, I_CAL_ATTENDEE_PROPERTY);

	for (link = (GSList *) attendee_list; link; link = g_slist_next (link)) {
		const ECalComponentAttendee *attendee = link->data;
		ICalProperty *prop;

		if (!attendee)
			continue;

		prop = e_cal_component_attendee_get_as_property (attendee);
		if (!prop)
			continue;

		i_cal_component_take_property (comp->priv->icalcomp, prop);
	}
}

/**
 * e_cal_component_has_attendees:
 * @comp: A calendar component object.
 *
 * Queries a calendar component object for the existence of attendees.
 *
 * Returns: TRUE if there are attendees, FALSE if not.
 *
 * Since: 3.34
 */
gboolean
e_cal_component_has_attendees (ECalComponent *comp)
{
	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);

	return e_cal_util_component_has_property (comp->priv->icalcomp, I_CAL_ATTENDEE_PROPERTY);
}

/**
 * e_cal_component_get_location:
 * @comp: A calendar component object
 *
 * Queries the location property of a calendar component object.
 *
 * Returns: (transfer full) (nullable): the locatio, or %NULL, if none is set
 *
 * Since: 3.34
 **/
gchar *
e_cal_component_get_location (ECalComponent *comp)
{
	ICalProperty *prop;
	gchar *location;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_LOCATION_PROPERTY);

	if (!prop)
		return NULL;

	location = g_strdup (i_cal_property_get_location (prop));

	g_object_unref (prop);

	return location;
}

/**
 * e_cal_component_set_location:
 * @comp: A calendar component object.
 * @location: (nullable): Location value. Use %NULL or empty string, to unset the property.
 *
 * Sets the location property of a calendar component object.
 *
 * Since: 3.34
 **/
void
e_cal_component_set_location (ECalComponent *comp,
                              const gchar *location)
{
	ICalProperty *prop;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	prop = i_cal_component_get_first_property (comp->priv->icalcomp, I_CAL_LOCATION_PROPERTY);

	if (!location || !*location) {
		if (prop) {
			i_cal_component_remove_property (comp->priv->icalcomp, prop);
			g_object_unref (prop);
		}

		return;
	}

	if (prop) {
		i_cal_property_set_location (prop, (gchar *) location);
		g_object_unref (prop);
	} else {
		prop = i_cal_property_new_location ((gchar *) location);
		i_cal_component_take_property (comp->priv->icalcomp, prop);
	}
}

/**
 * e_cal_component_has_alarms:
 * @comp: A calendar component object.
 *
 * Checks whether the component has any alarms.
 *
 * Returns: TRUE if the component has any alarms.
 *
 * Since: 3.34
 **/
gboolean
e_cal_component_has_alarms (ECalComponent *comp)
{
	ICalComponent *subcomp;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), FALSE);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, FALSE);

	subcomp = i_cal_component_get_first_component (comp->priv->icalcomp, I_CAL_VALARM_COMPONENT);
	if (subcomp) {
		g_object_unref (subcomp);
		return TRUE;
	}

	return FALSE;
}

static gchar *
dup_alarm_uid_from_component (ICalComponent *valarm)
{
	ICalProperty *prop;
	gchar *auid = NULL;

	g_return_val_if_fail (valarm != NULL, NULL);

	for (prop = i_cal_component_get_first_property (valarm, I_CAL_X_PROPERTY);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (valarm, I_CAL_X_PROPERTY)) {
		const gchar *xname;

		xname = i_cal_property_get_x_name (prop);

		if (g_strcmp0 (xname, E_CAL_EVOLUTION_ALARM_UID_PROPERTY) == 0) {
			const gchar *xvalue;

			xvalue = i_cal_property_get_x (prop);
			if (xvalue) {
				auid = g_strdup (xvalue);
				g_object_unref (prop);
				break;
			}
		}
	}

	return auid;
}

/**
 * e_cal_component_add_alarm:
 * @comp: A calendar component.
 * @alarm: (transfer none): an alarm, as an #ECalComponentAlarm
 *
 * Adds an alarm subcomponent to a calendar component.  You should have created
 * the @alarm by using e_cal_component_alarm_new(); it is invalid to use an
 * #ECalComponentAlarm structure that came from e_cal_component_get_alarm().  After
 * adding the alarm, the @alarm structure is no longer valid because the
 * internal structures may change and you should get rid of it by using
 * e_cal_component_alarm_free().
 *
 * Since: 3.34
 **/
void
e_cal_component_add_alarm (ECalComponent *comp,
			   ECalComponentAlarm *alarm)
{
	GSList *existing_uids, *link;
	ICalComponent *valarm;
	const gchar *auid;

	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (alarm != NULL);
	g_return_if_fail (comp->priv->icalcomp);

	auid = e_cal_component_alarm_get_uid (alarm);

	existing_uids = e_cal_component_get_alarm_uids (comp);

	for (link = existing_uids; link; link = g_slist_next (link)) {
		const gchar *existing_auid = link->data;

		if (g_strcmp0 (auid, existing_auid) == 0)
			break;
	}

	g_slist_free_full (existing_uids, g_free);

	/* Not NULL, when found an alarm with the same UID */
	if (link) {
		/* This generates new UID for the alarm */
		e_cal_component_alarm_set_uid (alarm, NULL);
	}

	valarm = e_cal_component_alarm_get_as_component (alarm);
	if (valarm)
		i_cal_component_take_component (comp->priv->icalcomp, valarm);
}

static gboolean
remove_alarm_cb (ICalComponent *icalcomp,
		 ICalComponent *subcomp,
		 gpointer user_data)
{
	const gchar *auid = user_data;
	gchar *existing;

	g_return_val_if_fail (auid != NULL, FALSE);

	existing = dup_alarm_uid_from_component (subcomp);
	if (g_strcmp0 (existing, auid) == 0) {
		g_free (existing);
		i_cal_component_remove_component (icalcomp, subcomp);
		return FALSE;
	}

	g_free (existing);

	return TRUE;
}

/**
 * e_cal_component_remove_alarm:
 * @comp: A calendar component.
 * @auid: UID of the alarm to remove.
 *
 * Removes an alarm subcomponent from a calendar component.  If the alarm that
 * corresponds to the specified @auid had been fetched with
 * e_cal_component_get_alarm(), then those alarm structures will be invalid; you
 * should get rid of them with e_cal_component_alarm_free() before using this
 * function.
 *
 * Since: 3.34
 **/
void
e_cal_component_remove_alarm (ECalComponent *comp,
                              const gchar *auid)
{
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (auid != NULL);
	g_return_if_fail (comp->priv->icalcomp != NULL);

	foreach_subcomponent (comp->priv->icalcomp, I_CAL_VALARM_COMPONENT, remove_alarm_cb, (gpointer) auid);
}

static gboolean
remove_all_alarms_cb (ICalComponent *icalcomp,
		      ICalComponent *subcomp,
		      gpointer user_data)
{
	i_cal_component_remove_component (icalcomp, subcomp);

	return TRUE;
}

/**
 * e_cal_component_remove_all_alarms:
 * @comp: A calendar component
 *
 * Remove all alarms from the calendar component
 *
 * Since: 3.34
 **/
void
e_cal_component_remove_all_alarms (ECalComponent *comp)
{
	g_return_if_fail (E_IS_CAL_COMPONENT (comp));
	g_return_if_fail (comp->priv->icalcomp != NULL);

	foreach_subcomponent (comp->priv->icalcomp, I_CAL_VALARM_COMPONENT, remove_all_alarms_cb, NULL);
}

static gboolean
get_alarm_uids_cb (ICalComponent *icalcomp,
		   ICalComponent *subcomp,
		   gpointer user_data)
{
	GSList **puids = user_data;
	gchar *auid;

	g_return_val_if_fail (puids != NULL, FALSE);

	auid = dup_alarm_uid_from_component (subcomp);
	if (auid)
		*puids = g_slist_prepend (*puids, auid);

	return TRUE;
}

/**
 * e_cal_component_get_alarm_uids:
 * @comp: A calendar component.
 *
 * Builds a list of the unique identifiers of the alarm subcomponents inside a
 * calendar component. Free the returned #GSList with
 * g_slist_free_full (slist, g_free);, when no longer needed.
 *
 * Returns: (transfer full) (nullable) (element-type utf8): a #GSList of unique
 *    identifiers for alarms.
 *
 * Since: 3.34
 **/
GSList *
e_cal_component_get_alarm_uids (ECalComponent *comp)
{
	GSList *uids = NULL;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	foreach_subcomponent (comp->priv->icalcomp, I_CAL_VALARM_COMPONENT, get_alarm_uids_cb, &uids);

	return g_slist_reverse (uids);
}

struct GetAlarmData {
	const gchar *auid;
	ICalComponent *valarm;
};

static gboolean
get_alarm_cb (ICalComponent *icalcomp,
	      ICalComponent *subcomp,
	      gpointer user_data)
{
	struct GetAlarmData *gad = user_data;
	gchar *auid;

	g_return_val_if_fail (gad != NULL, FALSE);

	auid = dup_alarm_uid_from_component (subcomp);
	if (g_strcmp0 (auid, gad->auid) == 0) {
		gad->valarm = g_object_ref (subcomp);
	}

	g_free (auid);

	return !gad->valarm;
}

/**
 * e_cal_component_get_alarm:
 * @comp: A calendar component.
 * @auid: Unique identifier for the sought alarm subcomponent.
 *
 * Queries a particular alarm subcomponent of a calendar component.
 * Free the returned pointer with e_cal_component_alarm_free(),
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable): the alarm subcomponent that corresponds
 *    to the specified @auid, or %NULL if no alarm exists with that UID
 *
 * Since: 3.34
 **/
ECalComponentAlarm *
e_cal_component_get_alarm (ECalComponent *comp,
                           const gchar *auid)
{
	ECalComponentAlarm *alarm = NULL;
	struct GetAlarmData gad;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);
	g_return_val_if_fail (auid != NULL, NULL);

	gad.auid = auid;
	gad.valarm = NULL;

	foreach_subcomponent (comp->priv->icalcomp, I_CAL_VALARM_COMPONENT, get_alarm_cb, &gad);

	if (gad.valarm) {
		alarm = e_cal_component_alarm_new_from_component (gad.valarm);
		g_object_unref (gad.valarm);
	}

	return alarm;
}

static gboolean
get_all_alarms_cb (ICalComponent *icalcomp,
		   ICalComponent *subcomp,
		   gpointer user_data)
{
	GSList **palarms = user_data;
	ECalComponentAlarm *alarm;

	g_return_val_if_fail (palarms != NULL, FALSE);

	alarm = e_cal_component_alarm_new_from_component (subcomp);
	if (alarm)
		*palarms = g_slist_prepend (*palarms, alarm);

	return TRUE;
}

/**
 * e_cal_component_get_all_alarms:
 * @comp: A calendar component.
 *
 * Queries all alarm subcomponents of a calendar component.
 * Free the returned #GSList with g_slist_free_full (slist, e_cal_component_alarm_free);,
 * when no longer needed.
 *
 * Returns: (transfer full) (nullable) (element-type ECalComponentAlarm): the alarm subcomponents
 *    as a #GSList of #ECalComponentAlarm, or %NULL, if no alarm exists
 *
 * Since: 3.34
 **/
GSList *
e_cal_component_get_all_alarms (ECalComponent *comp)
{
	GSList *alarms = NULL;

	g_return_val_if_fail (E_IS_CAL_COMPONENT (comp), NULL);
	g_return_val_if_fail (comp->priv->icalcomp != NULL, NULL);

	foreach_subcomponent (comp->priv->icalcomp, I_CAL_VALARM_COMPONENT, get_all_alarms_cb, &alarms);

	return g_slist_reverse (alarms);
}
