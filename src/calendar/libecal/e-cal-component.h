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

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_CAL_COMPONENT_H
#define E_CAL_COMPONENT_H

#include <time.h>
#include <glib-object.h>
#include <libical-glib/libical-glib.h>

#include <libecal/e-cal-enums.h>
#include <libecal/e-cal-component-alarm.h>
#include <libecal/e-cal-component-alarm-instance.h>
#include <libecal/e-cal-component-alarm-repeat.h>
#include <libecal/e-cal-component-alarm-trigger.h>
#include <libecal/e-cal-component-alarms.h>
#include <libecal/e-cal-component-attendee.h>
#include <libecal/e-cal-component-datetime.h>
#include <libecal/e-cal-component-id.h>
#include <libecal/e-cal-component-organizer.h>
#include <libecal/e-cal-component-period.h>
#include <libecal/e-cal-component-range.h>
#include <libecal/e-cal-component-text.h>

/* Standard GObject macros */
#define E_TYPE_CAL_COMPONENT \
	(e_cal_component_get_type ())
#define E_CAL_COMPONENT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CAL_COMPONENT, ECalComponent))
#define E_CAL_COMPONENT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CAL_COMPONENT, ECalComponentClass))
#define E_IS_CAL_COMPONENT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CAL_COMPONENT))
#define E_IS_CAL_COMPONENT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CAL_COMPONENT))
#define E_CAL_COMPONENT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CAL_COMPONENT, ECalComponentClass))

G_BEGIN_DECLS

typedef struct _ECalComponent ECalComponent;
typedef struct _ECalComponentClass ECalComponentClass;
typedef struct _ECalComponentPrivate ECalComponentPrivate;

struct _ECalComponent {
	/*< private >*/
	GObject parent;
	ECalComponentPrivate *priv;
};

struct _ECalComponentClass {
	/*< private >*/
	GObjectClass parent_class;
};

GType		e_cal_component_get_type	(void);

ECalComponent *	e_cal_component_new		(void);
ECalComponent *	e_cal_component_new_from_string	(const gchar *calobj);
ECalComponent *	e_cal_component_new_from_icalcomponent
						(ICalComponent *icalcomp);

ECalComponent *	e_cal_component_clone		(ECalComponent *comp);

void		e_cal_component_set_new_vtype	(ECalComponent *comp,
						 ECalComponentVType type);

icalcomponent *	e_cal_component_get_icalcomponent
						(ECalComponent *comp);
gboolean	e_cal_component_set_icalcomponent
						(ECalComponent *comp,
						 icalcomponent *icalcomp);
void		e_cal_component_rescan		(ECalComponent *comp);
void		e_cal_component_strip_errors	(ECalComponent *comp);

ECalComponentVType
		e_cal_component_get_vtype	(ECalComponent *comp);

gchar *		e_cal_component_get_as_string	(ECalComponent *comp);

void		e_cal_component_commit_sequence	(ECalComponent *comp);
void		e_cal_component_abort_sequence	(ECalComponent *comp);

void		e_cal_component_get_uid		(ECalComponent *comp,
						 const gchar **uid);
void		e_cal_component_set_uid		(ECalComponent *comp,
						 const gchar *uid);

ECalComponentId *
		e_cal_component_get_id		(ECalComponent *comp);

void		e_cal_component_get_categories	(ECalComponent *comp,
						 const gchar **categories);
void		e_cal_component_set_categories	(ECalComponent *comp,
						 const gchar *categories);
void		e_cal_component_get_categories_list
						(ECalComponent *comp,
						 GSList **categ_list);
void		e_cal_component_set_categories_list
						(ECalComponent *comp,
						 GSList *categ_list);

void		e_cal_component_get_classification
						(ECalComponent *comp,
						 ECalComponentClassification *classif);
void		e_cal_component_set_classification
						(ECalComponent *comp,
						 ECalComponentClassification classif);

void		e_cal_component_get_comment_list
						(ECalComponent *comp,
						 GSList **text_list);
void		e_cal_component_set_comment_list
						(ECalComponent *comp,
						 GSList *text_list);

void		e_cal_component_get_completed	(ECalComponent *comp,
						 struct icaltimetype **t);
void		e_cal_component_set_completed	(ECalComponent *comp,
						 struct icaltimetype *t);

void		e_cal_component_get_contact_list
						(ECalComponent *comp,
						 GSList **text_list);
void		e_cal_component_set_contact_list
						(ECalComponent *comp,
						 GSList *text_list);

void		e_cal_component_get_created	(ECalComponent *comp,
						 struct icaltimetype **t);
void		e_cal_component_set_created	(ECalComponent *comp,
						 struct icaltimetype *t);

void		e_cal_component_get_description_list
						(ECalComponent *comp,
						 GSList **text_list);
void		e_cal_component_set_description_list
						(ECalComponent *comp,
						 GSList *text_list);

void		e_cal_component_get_dtend	(ECalComponent *comp,
						 ECalComponentDateTime *dt);
void		e_cal_component_set_dtend	(ECalComponent *comp,
						 ECalComponentDateTime *dt);

void		e_cal_component_get_dtstamp	(ECalComponent *comp,
						 struct icaltimetype *t);
void		e_cal_component_set_dtstamp	(ECalComponent *comp,
						 struct icaltimetype *t);

void		e_cal_component_get_dtstart	(ECalComponent *comp,
						 ECalComponentDateTime *dt);
void		e_cal_component_set_dtstart	(ECalComponent *comp,
						 ECalComponentDateTime *dt);

void		e_cal_component_get_due		(ECalComponent *comp,
						 ECalComponentDateTime *dt);
void		e_cal_component_set_due		(ECalComponent *comp,
						 ECalComponentDateTime *dt);

void		e_cal_component_get_exdate_list	(ECalComponent *comp,
						 GSList **exdate_list);
void		e_cal_component_set_exdate_list	(ECalComponent *comp,
						 GSList *exdate_list);
gboolean	e_cal_component_has_exdates	(ECalComponent *comp);

void		e_cal_component_get_exrule_list	(ECalComponent *comp,
						 GSList **recur_list);
void		e_cal_component_get_exrule_property_list
						(ECalComponent *comp,
						 GSList **recur_list);
void		e_cal_component_set_exrule_list	(ECalComponent *comp,
						 GSList *recur_list);
gboolean	e_cal_component_has_exrules	(ECalComponent *comp);

gboolean	e_cal_component_has_exceptions	(ECalComponent *comp);

void		e_cal_component_get_geo		(ECalComponent *comp,
						 struct icalgeotype **geo);
void		e_cal_component_set_geo		(ECalComponent *comp,
						 struct icalgeotype *geo);

void		e_cal_component_get_last_modified
						(ECalComponent *comp,
						 struct icaltimetype **t);
void		e_cal_component_set_last_modified
						(ECalComponent *comp,
						 struct icaltimetype *t);

void		e_cal_component_get_organizer	(ECalComponent *comp,
						 ECalComponentOrganizer *organizer);
void		e_cal_component_set_organizer	(ECalComponent *comp,
						 ECalComponentOrganizer *organizer);
gboolean	e_cal_component_has_organizer	(ECalComponent *comp);

gint		e_cal_component_get_percent_as_int
						(ECalComponent *comp);
void		e_cal_component_set_percent_as_int
						(ECalComponent *comp,
						 gint percent);

void		e_cal_component_get_percent	(ECalComponent *comp,
						 gint **percent);
void		e_cal_component_set_percent	(ECalComponent *comp,
						 gint *percent);

void		e_cal_component_get_priority	(ECalComponent *comp,
						 gint **priority);
void		e_cal_component_set_priority	(ECalComponent *comp,
						 gint *priority);

void		e_cal_component_get_recurid	(ECalComponent *comp,
						 ECalComponentRange *recur_id);
gchar *		e_cal_component_get_recurid_as_string
						(ECalComponent *comp);
void		e_cal_component_set_recurid	(ECalComponent *comp,
						 ECalComponentRange *recur_id);

void		e_cal_component_get_rdate_list	(ECalComponent *comp,
						 GSList **period_list);
void		e_cal_component_set_rdate_list	(ECalComponent *comp,
						 GSList *period_list);
gboolean	e_cal_component_has_rdates	(ECalComponent *comp);

void		e_cal_component_get_rrule_list	(ECalComponent *comp,
						 GSList **recur_list);
void		e_cal_component_get_rrule_property_list
						(ECalComponent *comp,
						 GSList **recur_list);
void		e_cal_component_set_rrule_list	(ECalComponent *comp,
						 GSList *recur_list);
gboolean	e_cal_component_has_rrules	(ECalComponent *comp);

gboolean	e_cal_component_has_recurrences	(ECalComponent *comp);
gboolean	e_cal_component_has_simple_recurrence
						(ECalComponent *comp);
gboolean	e_cal_component_is_instance	(ECalComponent *comp);

void		e_cal_component_get_sequence	(ECalComponent *comp,
						 gint **sequence);
void		e_cal_component_set_sequence	(ECalComponent *comp,
						 gint *sequence);

void		e_cal_component_get_status	(ECalComponent *comp,
						 icalproperty_status *status);
void		e_cal_component_set_status	(ECalComponent *comp,
						 icalproperty_status status);

void		e_cal_component_get_summary	(ECalComponent *comp,
						 ECalComponentText *summary);
void		e_cal_component_set_summary	(ECalComponent *comp,
						 ECalComponentText *summary);

void		e_cal_component_get_transparency
						(ECalComponent *comp,
						 ECalComponentTransparency *transp);
void		e_cal_component_set_transparency
						(ECalComponent *comp,
						 ECalComponentTransparency transp);

void		e_cal_component_get_url		(ECalComponent *comp,
						 const gchar **url);
void		e_cal_component_set_url		(ECalComponent *comp,
						 const gchar *url);

void		e_cal_component_get_attendee_list
						(ECalComponent *comp,
						 GSList **attendee_list);
void		e_cal_component_set_attendee_list
						(ECalComponent *comp,
						 GSList *attendee_list);
gboolean	e_cal_component_has_attendees	(ECalComponent *comp);

void		e_cal_component_get_location	(ECalComponent *comp,
						 const gchar **location);
void		e_cal_component_set_location	(ECalComponent *comp,
						 const gchar *location);

/* Attachment handling */
void		e_cal_component_get_attachment_list
						(ECalComponent *comp,
						 GSList **attachment_list);
void		e_cal_component_set_attachment_list
						(ECalComponent *comp,
						 GSList *attachment_list);
gboolean	e_cal_component_has_attachments	(ECalComponent *comp);
gint		e_cal_component_get_num_attachments
						(ECalComponent *comp);

gboolean	e_cal_component_event_dates_match
						(ECalComponent *comp1,
						 ECalComponent *comp2);

/* Functions to free returned values */

void		e_cal_component_free_categories_list
						(GSList *categ_list);
void		e_cal_component_free_range	(ECalComponentRange *range);
void		e_cal_component_free_exdate_list
						(GSList *exdate_list);
void		e_cal_component_free_geo	(struct icalgeotype *geo);
void		e_cal_component_free_icaltimetype
						(struct icaltimetype *t);
void		e_cal_component_free_percent	(gint *percent);
void		e_cal_component_free_priority	(gint *priority);
void		e_cal_component_free_period_list
						(GSList *period_list);
void		e_cal_component_free_recur_list	(GSList *recur_list);
void		e_cal_component_free_sequence	(gint *sequence);
void		e_cal_component_free_text_list	(GSList *text_list);
void		e_cal_component_free_attendee_list
						(GSList *attendee_list);

gboolean	e_cal_component_has_alarms	(ECalComponent *comp);
void		e_cal_component_add_alarm	(ECalComponent *comp,
						 ECalComponentAlarm *alarm);
void		e_cal_component_remove_alarm	(ECalComponent *comp,
						 const gchar *auid);
void		e_cal_component_remove_all_alarms
						(ECalComponent *comp);

GList *		e_cal_component_get_alarm_uids	(ECalComponent *comp);
ECalComponentAlarm *
		e_cal_component_get_alarm	(ECalComponent *comp,
						 const gchar *auid);

G_END_DECLS

#endif /* E_CAL_COMPONENT_H */
