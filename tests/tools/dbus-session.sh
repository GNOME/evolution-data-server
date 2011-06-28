#
# Helper functions to start your own D-Bus session.
#
# Refactored from with-session-bush.sh (from telepathy-glib).
#
# The canonical location of this program is the telepathy-glib tools/
# directory, please synchronize any changes with that copy.
#
# Copyright (C) 2007-2008,2011 Collabora Ltd. <http://www.collabora.co.uk/>
#
# Copying and distribution of this file, with or without modification,
# are permitted in any medium without royalty provided the copyright
# notice and this notice are preserved.


dbus_daemon_args="--print-address=5 --print-pid=6 --fork"
dbus_verbose=0
dbus_me=with-session-bus
dbus_sleep=0
dbus_with_session=""
dbus_config_file=""

# Params:
# verbose: 0 for off and 1 for on
#
dbus_init () {
    exec 5> $dbus_me-$$.address
    exec 6> $dbus_me-$$.pid
    dbus_verbose=$1
}

dbus_usage ()
{
  echo "usage: $me [options] -- program [program_options]" >&2
  echo "Requires write access to the current directory." >&2
  echo "" >&2
  echo "If \$WITH_SESSION_BUS_FORK_DBUS_MONITOR is set, fork dbus-monitor" >&2
  echo "with the arguments in \$WITH_SESSION_BUS_FORK_DBUS_MONITOR_OPT." >&2
  echo "The output of dbus-monitor is saved in $me-<pid>.dbus-monitor-logs" >&2
  exit 2
}

dbus_parse_args () {
    while test "z$1" != "z--"; do
	case "$1" in
	--sleep=*)
            sleep="$1"
	    dbus_sleep="${sleep#--sleep=}"
	    shift
	    ;;
	--session)
	    dbus_with_session="--session"
	    shift
	    ;;
	--config-file=*)
            # FIXME: assumes config file doesn't contain any special characters
	    dbus_config_file="$1"
	    shift
	    ;;
	 *)
	    dbus_usage
	    ;;
	esac
    done
}

dbus_start () {
    local args="$dbus_daemon_args $dbus_with_session $dbus_config_file "

    if [ $dbus_verbose -gt 0 ] ; then
	echo -n "dbus args $args "
    fi

    dbus-daemon $args

    {
	if [ $dbus_verbose -gt 0 ] ; then
	    echo -n "Temporary bus daemon is "; cat $dbus_me-$$.address;
	fi
    } >&2

    {
	if [ $dbus_verbose -gt 0 ] ; then
	    echo -n "Temporary bus daemon PID is "; head -n1 $dbus_me-$$.pid;
	fi
    } >&2

    DBUS_SESSION_BUS_ADDRESS="`cat $dbus_me-$$.address`"
    export DBUS_SESSION_BUS_ADDRESS

    if [ -n "$WITH_SESSION_BUS_FORK_DBUS_MONITOR" ] ; then
	if [ $dbus_verbose -gt 0 ] ; then
	    echo -n "Forking dbus-monitor " \
		"$WITH_SESSION_BUS_FORK_DBUS_MONITOR_OPT" >&2
	fi
	dbus-monitor $WITH_SESSION_BUS_FORK_DBUS_MONITOR_OPT \
            > $dbus_me-$$.dbus-monitor-logs 2>&1 &
    fi
}

#
# This should be called for INT, HUP and TERM signals
#
dbus_stop () {
    pid=`head -n1 $dbus_me-$$.pid`
    if test -n "$pid" ; then
	if [ $dbus_verbose -gt 0 ] ; then
	    echo "Killing temporary bus daemon: $pid" >&2
	fi
	kill -INT "$pid"
    fi
    rm -f $dbus_me-$$.address
    rm -f $dbus_me-$$.pid
}
