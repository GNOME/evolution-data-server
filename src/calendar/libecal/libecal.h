/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef LIBECAL_H
#define LIBECAL_H

#define __LIBECAL_H_INSIDE__

#define LIBICAL_GLIB_UNSTABLE_API 1

#include <libical-glib/libical-glib.h>
#include <libedataserver/libedataserver.h>

#include <libecal/e-cal-check-timezones.h>
#include <libecal/e-cal-client-view.h>
#include <libecal/e-cal-client.h>
#include <libecal/e-cal-component.h>
#include <libecal/e-cal-component-alarm.h>
#include <libecal/e-cal-component-alarm-instance.h>
#include <libecal/e-cal-component-alarm-repeat.h>
#include <libecal/e-cal-component-alarm-trigger.h>
#include <libecal/e-cal-component-alarms.h>
#include <libecal/e-cal-component-attendee.h>
#include <libecal/e-cal-component-bag.h>
#include <libecal/e-cal-component-datetime.h>
#include <libecal/e-cal-component-id.h>
#include <libecal/e-cal-component-organizer.h>
#include <libecal/e-cal-component-parameter-bag.h>
#include <libecal/e-cal-component-period.h>
#include <libecal/e-cal-component-property-bag.h>
#include <libecal/e-cal-component-range.h>
#include <libecal/e-cal-component-text.h>
#include <libecal/e-cal-enums.h>
#include <libecal/e-cal-enumtypes.h>
#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-system-timezone.h>
#include <libecal/e-cal-time-util.h>
#include <libecal/e-cal-util.h>
#include <libecal/e-reminder-watcher.h>
#include <libecal/e-timezone-cache.h>

#undef LIBICAL_GLIB_UNSTABLE_API

#undef __LIBECAL_H_INSIDE__

#endif /* LIBECAL_H */
