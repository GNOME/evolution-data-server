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
ECalComponent *	e_cal_component_new_vtype	(ECalComponentVType vtype);
ECalComponent *	e_cal_component_new_from_string	(const gchar *calobj);
ECalComponent *	e_cal_component_new_from_icalcomponent
						(ICalComponent *icalcomp);

ECalComponent *	e_cal_component_clone		(ECalComponent *comp);

void		e_cal_component_set_new_vtype	(ECalComponent *comp,
						 ECalComponentVType type);
ECalComponentVType
		e_cal_component_get_vtype	(ECalComponent *comp);

ICalComponent *	e_cal_component_get_icalcomponent
						(ECalComponent *comp);
gboolean	e_cal_component_set_icalcomponent
						(ECalComponent *comp,
						 ICalComponent *icalcomp);
void		e_cal_component_strip_errors	(ECalComponent *comp);

gchar *		e_cal_component_get_as_string	(ECalComponent *comp);

void		e_cal_component_commit_sequence	(ECalComponent *comp);
void		e_cal_component_abort_sequence	(ECalComponent *comp);

const gchar *	e_cal_component_get_uid		(ECalComponent *comp);
void		e_cal_component_set_uid		(ECalComponent *comp,
						 const gchar *uid);

ECalComponentId *
		e_cal_component_get_id		(ECalComponent *comp);

gchar *		e_cal_component_get_categories	(ECalComponent *comp);
void		e_cal_component_set_categories	(ECalComponent *comp,
						 const gchar *categories);
GSList *	e_cal_component_get_categories_list /* gchar * */
						(ECalComponent *comp);
void		e_cal_component_set_categories_list
						(ECalComponent *comp,
						 const GSList *categ_list); /* gchar * */

ECalComponentClassification
		e_cal_component_get_classification
						(ECalComponent *comp);
void		e_cal_component_set_classification
						(ECalComponent *comp,
						 ECalComponentClassification classif);

GSList *	e_cal_component_get_comments	(ECalComponent *comp); /* ECalComponentText * */
void		e_cal_component_set_comments	(ECalComponent *comp,
						 const GSList *text_list); /* ECalComponentText * */

ICalTime *	e_cal_component_get_completed	(ECalComponent *comp);
void		e_cal_component_set_completed	(ECalComponent *comp,
						 const ICalTime *tt);

GSList *	e_cal_component_get_contacts	(ECalComponent *comp); /* ECalComponentText * */
void		e_cal_component_set_contacts	(ECalComponent *comp,
						 const GSList *text_list); /* ECalComponentText * */

ICalTime *	e_cal_component_get_created	(ECalComponent *comp);
void		e_cal_component_set_created	(ECalComponent *comp,
						 const ICalTime *tt);

GSList *	e_cal_component_get_descriptions(ECalComponent *comp);  /* ECalComponentText * */
void		e_cal_component_set_descriptions(ECalComponent *comp,
						 const GSList *text_list); /* ECalComponentText * */

ECalComponentDateTime *
		e_cal_component_get_dtend	(ECalComponent *comp);
void		e_cal_component_set_dtend	(ECalComponent *comp,
						 const ECalComponentDateTime *dt);

ICalTime *	e_cal_component_get_dtstamp	(ECalComponent *comp);
void		e_cal_component_set_dtstamp	(ECalComponent *comp,
						 const ICalTime *tt);

ECalComponentDateTime *
		e_cal_component_get_dtstart	(ECalComponent *comp);
void		e_cal_component_set_dtstart	(ECalComponent *comp,
						 const ECalComponentDateTime *dt);

ECalComponentDateTime *
		e_cal_component_get_due		(ECalComponent *comp);
void		e_cal_component_set_due		(ECalComponent *comp,
						 const ECalComponentDateTime *dt);

GSList *	e_cal_component_get_exdates	(ECalComponent *comp); /* ECalComponentDateTime * */
void		e_cal_component_set_exdates	(ECalComponent *comp,
						 const GSList *exdate_list); /* ECalComponentDateTime * */
gboolean	e_cal_component_has_exdates	(ECalComponent *comp);

GSList *	e_cal_component_get_exrules	(ECalComponent *comp); /* ICalRecurrence * */
GSList *	e_cal_component_get_exrule_properties /* ICalProperty * */
						(ECalComponent *comp);
void		e_cal_component_set_exrules	(ECalComponent *comp,
						 const GSList *recur_list); /* ICalRecurrence * */
gboolean	e_cal_component_has_exrules	(ECalComponent *comp);

gboolean	e_cal_component_has_exceptions	(ECalComponent *comp);

ICalGeo *	e_cal_component_get_geo		(ECalComponent *comp);
void		e_cal_component_set_geo		(ECalComponent *comp,
						 const ICalGeo *geo);

ICalTime *	e_cal_component_get_last_modified
						(ECalComponent *comp);
void		e_cal_component_set_last_modified
						(ECalComponent *comp,
						 const ICalTime *tt);

ECalComponentOrganizer *
		e_cal_component_get_organizer	(ECalComponent *comp);
void		e_cal_component_set_organizer	(ECalComponent *comp,
						 const ECalComponentOrganizer *organizer);
gboolean	e_cal_component_has_organizer	(ECalComponent *comp);

gint		e_cal_component_get_percent_complete
						(ECalComponent *comp);
void		e_cal_component_set_percent_complete
						(ECalComponent *comp,
						 gint percent);

gint		e_cal_component_get_priority	(ECalComponent *comp);
void		e_cal_component_set_priority	(ECalComponent *comp,
						 gint priority);

ECalComponentRange *
		e_cal_component_get_recurid	(ECalComponent *comp);
gchar *		e_cal_component_get_recurid_as_string
						(ECalComponent *comp);
void		e_cal_component_set_recurid	(ECalComponent *comp,
						 const ECalComponentRange *recur_id);

GSList *	e_cal_component_get_rdates	(ECalComponent *comp); /* ECalComponentPeriod * */
void		e_cal_component_set_rdates	(ECalComponent *comp,
						 const GSList *rdate_list); /* ECalComponentPeriod * */
gboolean	e_cal_component_has_rdates	(ECalComponent *comp);

GSList *	e_cal_component_get_rrules	(ECalComponent *comp); /* ICalRecurrence * */
GSList *	e_cal_component_get_rrule_properties /* ICalProperty * */
						(ECalComponent *comp);
void		e_cal_component_set_rrules	(ECalComponent *comp,
						 const GSList *recur_list); /* ICalRecurrence * */
gboolean	e_cal_component_has_rrules	(ECalComponent *comp);

gboolean	e_cal_component_has_recurrences	(ECalComponent *comp);
gboolean	e_cal_component_has_simple_recurrence
						(ECalComponent *comp);
gboolean	e_cal_component_is_instance	(ECalComponent *comp);

gint		e_cal_component_get_sequence	(ECalComponent *comp);
void		e_cal_component_set_sequence	(ECalComponent *comp,
						 gint sequence);

ICalPropertyStatus
		e_cal_component_get_status	(ECalComponent *comp);
void		e_cal_component_set_status	(ECalComponent *comp,
						 ICalPropertyStatus status);

ECalComponentText *
		e_cal_component_get_summary	(ECalComponent *comp);
void		e_cal_component_set_summary	(ECalComponent *comp,
						 const ECalComponentText *summary);

ECalComponentTransparency
		e_cal_component_get_transparency(ECalComponent *comp);
void		e_cal_component_set_transparency(ECalComponent *comp,
						 ECalComponentTransparency transp);

gchar *		e_cal_component_get_url		(ECalComponent *comp);
void		e_cal_component_set_url		(ECalComponent *comp,
						 const gchar *url);

GSList *	e_cal_component_get_attendees	(ECalComponent *comp);  /* ECalComponentAttendee * */
void		e_cal_component_set_attendees	(ECalComponent *comp,
						 const GSList *attendee_list); /* ECalComponentAttendee * */
gboolean	e_cal_component_has_attendees	(ECalComponent *comp);

gchar *		e_cal_component_get_location	(ECalComponent *comp);
void		e_cal_component_set_location	(ECalComponent *comp,
						 const gchar *location);

/* Attachment handling */
GSList *	e_cal_component_get_attachments	(ECalComponent *comp); /* ICalAttach * */
void		e_cal_component_set_attachments	(ECalComponent *comp,
						 const GSList *attachments); /* ICalAttach * */
gboolean	e_cal_component_has_attachments	(ECalComponent *comp);

/* Alarms */
gboolean	e_cal_component_has_alarms	(ECalComponent *comp);
void		e_cal_component_add_alarm	(ECalComponent *comp,
						 ECalComponentAlarm *alarm);
void		e_cal_component_remove_alarm	(ECalComponent *comp,
						 const gchar *auid);
void		e_cal_component_remove_all_alarms
						(ECalComponent *comp);

GSList *	e_cal_component_get_alarm_uids	(ECalComponent *comp); /* gchar * */
ECalComponentAlarm *
		e_cal_component_get_alarm	(ECalComponent *comp,
						 const gchar *auid);
GSList *	e_cal_component_get_all_alarms	(ECalComponent *comp); /* ECalComponentAlarm * */

G_END_DECLS

#endif /* E_CAL_COMPONENT_H */
