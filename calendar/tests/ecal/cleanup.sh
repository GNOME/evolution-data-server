#! /bin/sh

if [ -z $XDG_DATA_HOME ]
then
	XDG_DATA_HOME=$HOME/.local
fi

rm -rf $XDG_DATA_HOME/calendar/OnThisComputer/Test*
rm -f tmp/*.out

