#!/bin/sh
# with-session-bus-eds.sh - run a program with a temporary D-Bus session daemon
#
# interesting bits have been move into dbus to permit reusability
#
# Copyright (C) 2007-2008 Collabora Ltd. <http://www.collabora.co.uk/>
#
# Copying and distribution of this file, with or without modification,
# are permitted in any medium without royalty provided the copyright
# notice and this notice are preserved.


cur_dir=`dirname $0`

. $cur_dir"/dbus-session.sh"
. $cur_dir"/eds.sh"

dbus_parse_args $@
while test "z$1" != "z--"; do
    shift
done
shift
if test "z$1" = "z"; then dbus_usage; fi

cleanup ()
{
    eds_stop
    dbus_stop
}

trap cleanup INT HUP TERM

eds_init_settings
eds_start
dbus_init 0

dbus_start

e=0
"$@" || e=$?

trap - INT HUP TERM
cleanup

exit $e
