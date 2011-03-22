#! /bin/sh
# Usage : Argument 1 specifies the no. of libecal client instances would be
# executed 

if [ -z $XDG_DATA_HOME ]
then
	XDG_DATA_HOME=$HOME/.local
fi

i=0
if [ ! -d tmp ] 
then
	mkdir tmp
fi
while [ $i -ne $1 ] 
do
i=$(($i+1))
if [ ! -d $XDG_DATA_HOME/calendar/local/OnThisComputer/TestCal$i ] 
then
	mkdir $XDG_DATA_HOME/calendar/local/OnThisComputer/TestCal$i
fi
cp -f testdata.ics $XDG_DATA_HOME/calendar/local/OnThisComputer/TestCal$i/calendar.ics
./test-ecal $XDG_DATA_HOME/calendar/local/OnThisComputer/TestCal $i | tee -i "tmp/$i.out" & 
#./test-ecal $XDG_DATA_HOME/calendar/local/OnThisComputer/TestCal $i > "tmp/$i.out" & 
done
dateTest=`date`
echo "End of testing at: $dateTest"
exit 0
