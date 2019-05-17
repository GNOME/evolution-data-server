/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2019 Red Hat, Inc. (www.redhat.com)
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

#include "evolution-data-server-config.h"

/**
 * SECTION:e-cal-component-attendee
 * @short_description: An ECalComponentAttendee structure
 * @include: libecal/libecal.h
 *
 * Contains functions to work with the #ECalComponentAttendee structure.
 **/

#include "e-cal-component-parameter-bag.h"

#include "e-cal-component-attendee.h"

G_DEFINE_BOXED_TYPE (ECalComponentAttendee, e_cal_component_attendee, e_cal_component_attendee_copy, e_cal_component_attendee_free)

struct _ECalComponentAttendee {
	gchar *value;

	gchar *member;
	ICalParameterCutype cutype;
	ICalParameterRole role;
	ICalParameterPartstat partstat;
	gboolean rsvp;

	gchar *delegatedfrom;
	gchar *delegatedto;
	gchar *sentby;
	gchar *cn;
	gchar *language;

	ECalComponentParameterBag *parameter_bag;
};

/**
 * e_cal_component_attendee_new:
 *
 * Creates a new empty #ECalComponentAttendee structure. Free it
 * with e_cal_component_attendee_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentAttendee
 *
 * Since: 3.34
 **/
ECalComponentAttendee *
e_cal_component_attendee_new (void)
{
	ECalComponentAttendee *attendee;

	attendee = g_new0 (ECalComponentAttendee, 1);
	attendee->cutype = I_CAL_CUTYPE_NONE;
	attendee->role = I_CAL_ROLE_REQPARTICIPANT;
	attendee->partstat = I_CAL_PARTSTAT_NEEDSACTION;
	attendee->parameter_bag = e_cal_component_parameter_bag_new ();

	return attendee;
}

/**
 * e_cal_component_attendee_new_full:
 * @value: (nullable): usually a "mailto:email" of the attendee
 * @member: (nullable): member parameter
 * @cutype: type of the attendee, an #ICalParameterCutype
 * @role: role of the attendee, an #ICalParameterRole
 * @partstat: current status of the attendee, an #ICalParameterPartstat
 * @rsvp: whether requires RSVP
 * @delegatedfrom: (nullable): delegated from
 * @delegatedto: (nullable): delegated to
 * @sentby: (nullable): sent by
 * @cn: (nullable): common name
 * @language: (nullable): language
 *
 * Creates a new #ECalComponentAttendee structure, with all members filled
 * with given values from the parameters. The %NULL and empty strings are
 * treated as unset the value. Free the structure
 * with e_cal_component_attendee_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated #ECalComponentAttendee
 *
 * Since: 3.34
 **/
ECalComponentAttendee *
e_cal_component_attendee_new_full (const gchar *value,
				   const gchar *member,
				   ICalParameterCutype cutype,
				   ICalParameterRole role,
				   ICalParameterPartstat partstat,
				   gboolean rsvp,
				   const gchar *delegatedfrom,
				   const gchar *delegatedto,
				   const gchar *sentby,
				   const gchar *cn,
				   const gchar *language)
{
	ECalComponentAttendee *attendee;

	attendee = e_cal_component_attendee_new ();
	attendee->value = value && *value ? g_strdup (value) : NULL;
	attendee->member = member && *member ? g_strdup (member) : NULL;
	attendee->cutype = cutype;
	attendee->role = role;
	attendee->partstat = partstat;
	attendee->rsvp = rsvp;
	attendee->delegatedfrom = delegatedfrom && *delegatedfrom ? g_strdup (delegatedfrom) : NULL;
	attendee->delegatedto = delegatedto && *delegatedto ? g_strdup (delegatedto) : NULL;
	attendee->sentby = sentby && *sentby ? g_strdup (sentby) : NULL;
	attendee->cn = cn && *cn ? g_strdup (cn) : NULL;
	attendee->language = language && *language ? g_strdup (language) : NULL;

	return attendee;
}

/**
 * e_cal_component_attendee_new_from_property:
 * @property: an #ICalProperty of kind %I_CAL_ATTENDEE_PROPERTY
 *
 * Creates a new #ECalComponentAttendee, filled with values from @property,
 * which should be of kind %I_CAL_ATTENDEE_PROPERTY. The function returns
 * %NULL when it is not of the expected kind. Free the structure
 * with e_cal_component_attendee_free(), when no longer needed.
 *
 * Returns: (transfer full) (nullable): a newly allocated #ECalComponentAttendee
 *
 * Since: 3.34
 **/
ECalComponentAttendee *
e_cal_component_attendee_new_from_property (const ICalProperty *property)
{
	ECalComponentAttendee *attendee;

	g_return_val_if_fail (I_CAL_IS_PROPERTY (property), NULL);

	if (i_cal_property_isa ((ICalProperty *) property) != I_CAL_ATTENDEE_PROPERTY)
		return NULL;

	attendee = e_cal_component_attendee_new ();

	e_cal_component_attendee_set_from_property (attendee, property);

	return attendee;
}

/**
 * e_cal_component_attendee_copy:
 * @attendee: (not nullable): an #ECalComponentAttendee
 *
 * Returns a newly allocated copy of @attendee, which should be freed with
 * e_cal_component_attendee_free(), when no longer needed.
 *
 * Returns: (transfer full): a newly allocated copy of @attendee
 *
 * Since: 3.34
 **/
ECalComponentAttendee *
e_cal_component_attendee_copy (const ECalComponentAttendee *attendee)
{
	ECalComponentAttendee *copy;

	g_return_val_if_fail (attendee != NULL, NULL);

	copy = e_cal_component_attendee_new_full (attendee->value,
		attendee->member,
		attendee->cutype,
		attendee->role,
		attendee->partstat,
		attendee->rsvp,
		attendee->delegatedfrom,
		attendee->delegatedto,
		attendee->sentby,
		attendee->cn,
		attendee->language);

	e_cal_component_parameter_bag_assign (copy->parameter_bag, attendee->parameter_bag);

	return copy;
}

/**
 * e_cal_component_attendee_free: (skip)
 * @attendee: (type ECalComponentAttendee) (nullable): an #ECalComponentAttendee to free
 *
 * Free @attendee, previously created by e_cal_component_attendee_new(),
 * e_cal_component_attendee_new_full(), e_cal_component_attendee_new_from_property()
 * or e_cal_component_attendee_copy(). The function does nothing, if @attendee
 * is %NULL.
 *
 * Since: 3.34
 **/
void
e_cal_component_attendee_free (gpointer attendee)
{
	ECalComponentAttendee *att = attendee;

	if (att) {
		e_cal_component_parameter_bag_free (att->parameter_bag);
		g_free (att->value);
		g_free (att->member);
		g_free (att->delegatedfrom);
		g_free (att->delegatedto);
		g_free (att->sentby);
		g_free (att->cn);
		g_free (att->language);
		g_free (att);
	}
}

static gboolean
e_cal_component_attendee_filter_params_cb (ICalParameter *param,
					   gpointer user_data)
{
	ICalParameterKind kind;

	kind = i_cal_parameter_isa (param);

	return kind != I_CAL_MEMBER_PARAMETER &&
	       kind != I_CAL_CUTYPE_PARAMETER &&
	       kind != I_CAL_ROLE_PARAMETER &&
	       kind != I_CAL_PARTSTAT_PARAMETER &&
	       kind != I_CAL_RSVP_PARAMETER &&
	       kind != I_CAL_DELEGATEDFROM_PARAMETER &&
	       kind != I_CAL_DELEGATEDTO_PARAMETER &&
	       kind != I_CAL_SENTBY_PARAMETER &&
	       kind != I_CAL_CN_PARAMETER &&
	       kind != I_CAL_LANGUAGE_PARAMETER;
}

/**
 * e_cal_component_attendee_set_from_property:
 * @attendee: an #ECalComponentAttendee
 * @property: an #ICalProperty
 *
 * Fill the @attendee structure with the information from
 * the @property, which should be of %I_CAL_ATTENDEE_PROPERTY kind.
 *
 * Since: 3.34
 **/
void
e_cal_component_attendee_set_from_property (ECalComponentAttendee *attendee,
					    const ICalProperty *property)
{
	ICalProperty *prop = (ICalProperty *) property;
	ICalParameter *param;

	g_return_if_fail (attendee != NULL);
	g_return_if_fail (I_CAL_IS_PROPERTY (property));
	g_return_if_fail (i_cal_property_isa (prop) == I_CAL_ATTENDEE_PROPERTY);

	e_cal_component_attendee_set_value (attendee, i_cal_property_get_attendee (prop));

	param = i_cal_property_get_first_parameter (prop, I_CAL_MEMBER_PARAMETER);
	e_cal_component_attendee_set_member (attendee, param ? i_cal_parameter_get_member (param) : NULL);
	g_clear_object (&param);

	param = i_cal_property_get_first_parameter (prop, I_CAL_CUTYPE_PARAMETER);
	e_cal_component_attendee_set_cutype (attendee, param ? i_cal_parameter_get_cutype (param) : I_CAL_CUTYPE_NONE);
	g_clear_object (&param);

	param = i_cal_property_get_first_parameter (prop, I_CAL_ROLE_PARAMETER);
	e_cal_component_attendee_set_role (attendee, param ? i_cal_parameter_get_role (param) : I_CAL_ROLE_REQPARTICIPANT);
	g_clear_object (&param);

	param = i_cal_property_get_first_parameter (prop, I_CAL_PARTSTAT_PARAMETER);
	e_cal_component_attendee_set_partstat (attendee, param ? i_cal_parameter_get_partstat (param) : I_CAL_PARTSTAT_NEEDSACTION);
	g_clear_object (&param);

	param = i_cal_property_get_first_parameter (prop, I_CAL_RSVP_PARAMETER);
	e_cal_component_attendee_set_rsvp (attendee, param && i_cal_parameter_get_rsvp (param) == I_CAL_RSVP_TRUE);
	g_clear_object (&param);

	param = i_cal_property_get_first_parameter (prop, I_CAL_DELEGATEDFROM_PARAMETER);
	e_cal_component_attendee_set_delegatedfrom (attendee, param ? i_cal_parameter_get_delegatedfrom (param) : NULL);
	g_clear_object (&param);

	param = i_cal_property_get_first_parameter (prop, I_CAL_DELEGATEDTO_PARAMETER);
	e_cal_component_attendee_set_delegatedto (attendee, param ? i_cal_parameter_get_delegatedto (param) : NULL);
	g_clear_object (&param);

	param = i_cal_property_get_first_parameter (prop, I_CAL_SENTBY_PARAMETER);
	e_cal_component_attendee_set_sentby (attendee, param ? i_cal_parameter_get_sentby (param) : NULL);
	g_clear_object (&param);

	param = i_cal_property_get_first_parameter (prop, I_CAL_CN_PARAMETER);
	e_cal_component_attendee_set_cn (attendee, param ? i_cal_parameter_get_cn (param) : NULL);
	g_clear_object (&param);

	param = i_cal_property_get_first_parameter (prop, I_CAL_LANGUAGE_PARAMETER);
	e_cal_component_attendee_set_language (attendee, param ? i_cal_parameter_get_language (param) : NULL);
	g_clear_object (&param);

	e_cal_component_parameter_bag_set_from_property (attendee->parameter_bag, prop, e_cal_component_attendee_filter_params_cb, NULL);
}

/**
 * e_cal_component_attendee_get_as_property:
 * @attendee: an #ECalComponentAttendee
 *
 * Converts information stored in @attendee into an #ICalProperty
 * of %I_CAL_ATTENDEE_PROPERTY kind. The caller is responsible to free
 * the returned object with g_object_unref(), when no longer needed.
 *
 * Returns: (transfer full): a newly created #ICalProperty, containing
 *    information from the @attendee.
 *
 * Since: 3.34
 **/
ICalProperty *
e_cal_component_attendee_get_as_property (const ECalComponentAttendee *attendee)
{
	ICalProperty *prop;

	g_return_val_if_fail (attendee != NULL, NULL);

	prop = i_cal_property_new (I_CAL_ATTENDEE_PROPERTY);
	g_return_val_if_fail (prop != NULL, NULL);

	e_cal_component_attendee_fill_property (attendee, prop);

	return prop;
}

/**
 * e_cal_component_attendee_fill_property:
 * @attendee: an #ECalComponentAttendee
 * @property: (inout) (not nullable): an #ICalProperty
 *
 * Fill @property with information from @attendee. The @property
 * should be of kind %I_CAL_ATTENDEE_PROPERTY.
 *
 * Since: 3.34
 **/
void
e_cal_component_attendee_fill_property (const ECalComponentAttendee *attendee,
					ICalProperty *property)
{
	ICalParameter *param;

	g_return_if_fail (attendee != NULL);
	g_return_if_fail (I_CAL_IS_PROPERTY (property));
	g_return_if_fail (i_cal_property_isa (property) == I_CAL_ATTENDEE_PROPERTY);

	i_cal_property_set_attendee (property, attendee->value ? attendee->value : "mailto:");

	#define fill_param(_param, _val, _filled) \
		param = i_cal_property_get_first_parameter (property, _param); \
		if (_filled) { \
			if (!param) { \
				param = i_cal_parameter_new (_param); \
				i_cal_property_add_parameter (property, param); \
			} \
			i_cal_parameter_set_ ## _val (param, attendee-> _val); \
			g_clear_object (&param); \
		} else if (param) { \
			i_cal_property_remove_parameter_by_kind (property, _param); \
			g_clear_object (&param); \
		}

	fill_param (I_CAL_MEMBER_PARAMETER, member, attendee->member && *attendee->member);
	fill_param (I_CAL_CUTYPE_PARAMETER, cutype, attendee->cutype != I_CAL_CUTYPE_NONE);
	fill_param (I_CAL_ROLE_PARAMETER, role, attendee->role != I_CAL_ROLE_NONE);
	fill_param (I_CAL_PARTSTAT_PARAMETER, partstat, attendee->partstat != I_CAL_PARTSTAT_NONE);

	param = i_cal_property_get_first_parameter (property, I_CAL_RSVP_PARAMETER);
	if (param) {
		i_cal_parameter_set_rsvp (param, attendee->rsvp ? I_CAL_RSVP_TRUE : I_CAL_RSVP_FALSE);
		g_clear_object (&param);
	} else {
		param = i_cal_parameter_new (I_CAL_RSVP_PARAMETER);
		i_cal_parameter_set_rsvp (param, attendee->rsvp ? I_CAL_RSVP_TRUE : I_CAL_RSVP_FALSE);
		i_cal_property_take_parameter (property, param);
	}

	fill_param (I_CAL_DELEGATEDFROM_PARAMETER, delegatedfrom, attendee->delegatedfrom && *attendee->delegatedfrom);
	fill_param (I_CAL_DELEGATEDTO_PARAMETER, delegatedto, attendee->delegatedto && *attendee->delegatedto);
	fill_param (I_CAL_SENTBY_PARAMETER, sentby, attendee->sentby && *attendee->sentby);
	fill_param (I_CAL_CN_PARAMETER, cn, attendee->cn && *attendee->cn);
	fill_param (I_CAL_LANGUAGE_PARAMETER, language, attendee->language && *attendee->language);

	#undef fill_param

	e_cal_component_parameter_bag_fill_property (attendee->parameter_bag, property);
}

/**
 * e_cal_component_attendee_get_value:
 * @attendee: an #ECalComponentAttendee
 *
 * Returns: (nullable): the @attendee URI, usually of "mailto:email" form
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_attendee_get_value (const ECalComponentAttendee *attendee)
{
	g_return_val_if_fail (attendee != NULL, NULL);

	return attendee->value;
}

/**
 * e_cal_component_attendee_set_value:
 * @attendee: an #ECalComponentAttendee
 * @value: (nullable): the value to set
 *
 * Set the @attendee URI, usually of "mailto:email" form. The %NULL
 * and empty strings are treated as unset the value.
 *
 * Since: 3.34
 **/
void
e_cal_component_attendee_set_value (ECalComponentAttendee *attendee,
				    const gchar *value)
{
	g_return_if_fail (attendee != NULL);

	if (value && !*value)
		value = NULL;

	if (g_strcmp0 (attendee->value, value) != 0) {
		g_free (attendee->value);
		attendee->value = g_strdup (value);
	}
}

/**
 * e_cal_component_attendee_get_member:
 * @attendee: an #ECalComponentAttendee
 *
 * Returns: (nullable): the @attendee member property
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_attendee_get_member (const ECalComponentAttendee *attendee)
{
	g_return_val_if_fail (attendee != NULL, NULL);

	return attendee->member;
}

/**
 * e_cal_component_attendee_set_member:
 * @attendee: an #ECalComponentAttendee
 * @member: (nullable): the value to set
 *
 * Set the @attendee member parameter. The %NULL
 * and empty strings are treated as unset the value.
 *
 * Since: 3.34
 **/
void
e_cal_component_attendee_set_member (ECalComponentAttendee *attendee,
				     const gchar *member)
{
	g_return_if_fail (attendee != NULL);

	if (member && !*member)
		member = NULL;

	if (g_strcmp0 (attendee->member, member) != 0) {
		g_free (attendee->member);
		attendee->member = g_strdup (member);
	}
}

/**
 * e_cal_component_attendee_get_cutype:
 * @attendee: an #ECalComponentAttendee
 *
 * Returns: the @attendee type, as an #ICalParameterCutype
 *
 * Since: 3.34
 **/
ICalParameterCutype
e_cal_component_attendee_get_cutype (const ECalComponentAttendee *attendee)
{
	g_return_val_if_fail (attendee != NULL, I_CAL_CUTYPE_NONE);

	return attendee->cutype;
}

/**
 * e_cal_component_attendee_set_cutype:
 * @attendee: an #ECalComponentAttendee
 * @cutype: the value to set, as an #ICalParameterCutype
 *
 * Set the @attendee type, as an #ICalParameterCutype.
 *
 * Since: 3.34
 **/
void
e_cal_component_attendee_set_cutype (ECalComponentAttendee *attendee,
				     ICalParameterCutype cutype)
{
	g_return_if_fail (attendee != NULL);

	if (attendee->cutype != cutype) {
		attendee->cutype = cutype;
	}
}

/**
 * e_cal_component_attendee_get_role:
 * @attendee: an #ECalComponentAttendee
 *
 * Returns: the @attendee role, as an #ICalParameterRole
 *
 * Since: 3.34
 **/
ICalParameterRole
e_cal_component_attendee_get_role (const ECalComponentAttendee *attendee)
{
	g_return_val_if_fail (attendee != NULL, I_CAL_ROLE_NONE);

	return attendee->role;
}

/**
 * e_cal_component_attendee_set_role:
 * @attendee: an #ECalComponentAttendee
 * @role: the value to set, as an #ICalParameterRole
 *
 * Set the @attendee role, as an #ICalParameterRole.
 *
 * Since: 3.34
 **/
void
e_cal_component_attendee_set_role (ECalComponentAttendee *attendee,
				   ICalParameterRole role)
{
	g_return_if_fail (attendee != NULL);

	if (attendee->role != role) {
		attendee->role = role;
	}
}

/**
 * e_cal_component_attendee_get_partstat:
 * @attendee: an #ECalComponentAttendee
 *
 * Returns: the @attendee status, as an #ICalParameterPartstat
 *
 * Since: 3.34
 **/
ICalParameterPartstat
e_cal_component_attendee_get_partstat (const ECalComponentAttendee *attendee)
{
	g_return_val_if_fail (attendee != NULL, I_CAL_PARTSTAT_NONE);

	return attendee->partstat;
}

/**
 * e_cal_component_attendee_set_partstat:
 * @attendee: an #ECalComponentAttendee
 * @partstat: the value to set, as an #ICalParameterPartstat
 *
 * Set the @attendee status, as an #ICalParameterPartstat.
 *
 * Since: 3.34
 **/
void
e_cal_component_attendee_set_partstat (ECalComponentAttendee *attendee,
				       ICalParameterPartstat partstat)
{
	g_return_if_fail (attendee != NULL);

	if (attendee->partstat != partstat) {
		attendee->partstat = partstat;
	}
}

/**
 * e_cal_component_attendee_get_rsvp:
 * @attendee: an #ECalComponentAttendee
 *
 * Returns: whether the @attendee requires RSVP
 *
 * Since: 3.34
 **/
gboolean
e_cal_component_attendee_get_rsvp (const ECalComponentAttendee *attendee)
{
	g_return_val_if_fail (attendee != NULL, FALSE);

	return attendee->rsvp;
}

/**
 * e_cal_component_attendee_set_rsvp:
 * @attendee: an #ECalComponentAttendee
 * @rsvp: the value to set
 *
 * Set the @attendee RSVP.
 *
 * Since: 3.34
 **/
void
e_cal_component_attendee_set_rsvp (ECalComponentAttendee *attendee,
				   gboolean rsvp)
{
	g_return_if_fail (attendee != NULL);

	if ((attendee->rsvp ? 1 : 0) != (rsvp ? 1 : 0)) {
		attendee->rsvp = rsvp;
	}
}

/**
 * e_cal_component_attendee_get_delegatedfrom:
 * @attendee: an #ECalComponentAttendee
 *
 * Returns: (nullable): the @attendee delegatedfrom parameter
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_attendee_get_delegatedfrom (const ECalComponentAttendee *attendee)
{
	g_return_val_if_fail (attendee != NULL, NULL);

	return attendee->delegatedfrom;
}

/**
 * e_cal_component_attendee_set_delegatedfrom:
 * @attendee: an #ECalComponentAttendee
 * @delegatedfrom: (nullable): the value to set
 *
 * Set the @attendee delegatedfrom parameter. The %NULL
 * and empty strings are treated as unset the value.
 *
 * Since: 3.34
 **/
void
e_cal_component_attendee_set_delegatedfrom (ECalComponentAttendee *attendee,
					    const gchar *delegatedfrom)
{
	g_return_if_fail (attendee != NULL);

	if (delegatedfrom && !*delegatedfrom)
		delegatedfrom = NULL;

	if (g_strcmp0 (attendee->delegatedfrom, delegatedfrom) != 0) {
		g_free (attendee->delegatedfrom);
		attendee->delegatedfrom = g_strdup (delegatedfrom);
	}
}

/**
 * e_cal_component_attendee_get_delegatedto:
 * @attendee: an #ECalComponentAttendee
 *
 * Returns: (nullable): the @attendee delegatedto parameter
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_attendee_get_delegatedto (const ECalComponentAttendee *attendee)
{
	g_return_val_if_fail (attendee != NULL, NULL);

	return attendee->delegatedto;
}

/**
 * e_cal_component_attendee_set_delegatedto:
 * @attendee: an #ECalComponentAttendee
 * @delegatedto: (nullable): the value to set
 *
 * Set the @attendee delegatedto parameter. The %NULL
 * and empty strings are treated as unset the value.
 *
 * Since: 3.34
 **/
void
e_cal_component_attendee_set_delegatedto (ECalComponentAttendee *attendee,
					  const gchar *delegatedto)
{
	g_return_if_fail (attendee != NULL);

	if (delegatedto && !*delegatedto)
		delegatedto = NULL;

	if (g_strcmp0 (attendee->delegatedto, delegatedto) != 0) {
		g_free (attendee->delegatedto);
		attendee->delegatedto = g_strdup (delegatedto);
	}
}

/**
 * e_cal_component_attendee_get_sentby:
 * @attendee: an #ECalComponentAttendee
 *
 * Returns: (nullable): the @attendee sentby parameter
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_attendee_get_sentby (const ECalComponentAttendee *attendee)
{
	g_return_val_if_fail (attendee != NULL, NULL);

	return attendee->sentby;
}

/**
 * e_cal_component_attendee_set_sentby:
 * @attendee: an #ECalComponentAttendee
 * @sentby: (nullable): the value to set
 *
 * Set the @attendee sentby parameter. The %NULL
 * and empty strings are treated as unset the value.
 *
 * Since: 3.34
 **/
void
e_cal_component_attendee_set_sentby (ECalComponentAttendee *attendee,
				     const gchar *sentby)
{
	g_return_if_fail (attendee != NULL);

	if (sentby && !*sentby)
		sentby = NULL;

	if (g_strcmp0 (attendee->sentby, sentby) != 0) {
		g_free (attendee->sentby);
		attendee->sentby = g_strdup (sentby);
	}
}

/**
 * e_cal_component_attendee_get_cn:
 * @attendee: an #ECalComponentAttendee
 *
 * Returns: (nullable): the @attendee common name (cn) parameter
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_attendee_get_cn (const ECalComponentAttendee *attendee)
{
	g_return_val_if_fail (attendee != NULL, NULL);

	return attendee->cn;
}

/**
 * e_cal_component_attendee_set_cn:
 * @attendee: an #ECalComponentAttendee
 * @cn: (nullable): the value to set
 *
 * Set the @attendee common name (cn) parameter. The %NULL
 * and empty strings are treated as unset the value.
 *
 * Since: 3.34
 **/
void
e_cal_component_attendee_set_cn (ECalComponentAttendee *attendee,
				 const gchar *cn)
{
	g_return_if_fail (attendee != NULL);

	if (cn && !*cn)
		cn = NULL;

	if (g_strcmp0 (attendee->cn, cn) != 0) {
		g_free (attendee->cn);
		attendee->cn = g_strdup (cn);
	}
}

/**
 * e_cal_component_attendee_get_language:
 * @attendee: an #ECalComponentAttendee
 *
 * Returns: (nullable): the @attendee language parameter
 *
 * Since: 3.34
 **/
const gchar *
e_cal_component_attendee_get_language (const ECalComponentAttendee *attendee)
{
	g_return_val_if_fail (attendee != NULL, NULL);

	return attendee->language;
}

/**
 * e_cal_component_attendee_set_language:
 * @attendee: an #ECalComponentAttendee
 * @language: (nullable): the value to set
 *
 * Set the @attendee language parameter. The %NULL
 * and empty strings are treated as unset the value.
 *
 * Since: 3.34
 **/
void
e_cal_component_attendee_set_language (ECalComponentAttendee *attendee,
				       const gchar *language)
{
	g_return_if_fail (attendee != NULL);

	if (language && !*language)
		language = NULL;

	if (g_strcmp0 (attendee->language, language) != 0) {
		g_free (attendee->language);
		attendee->language = g_strdup (language);
	}
}

/**
 * e_cal_component_attendee_get_parameter_bag:
 * @attendee: an #ECalComponentAttendee
 *
 * Returns: (transfer none): an #ECalComponentParameterBag with additional
 *    parameters stored with the attendee property, other than those accessible
 *    with the other functions of the @attendee.
 *
 * Since: 3.34
 **/
ECalComponentParameterBag *
e_cal_component_attendee_get_parameter_bag (const ECalComponentAttendee *attendee)
{
	g_return_val_if_fail (attendee != NULL, NULL);

	return attendee->parameter_bag;
}
