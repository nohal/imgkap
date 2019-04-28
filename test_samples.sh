#!/bin/bash
 
#set -x 

function GenerateKap()
{
	[ -e out.kap ] && rm out.kap

	echo "./imgkap $PNGFILE $KAPHEADERFILE out.kap -t testchart $OPTIONS"
	./imgkap $PNGFILE $KAPHEADERFILE out.kap -t testchart $OPTIONS

	if [ -f out.kap ]; then 
	  echo "imgkap seems working correctly." 
	else 
	  echo "imgkap seems broken."
	fi
}


PNGFILE="./examples/osm/L16-27816-42904-16-8_16.png" 
KAPHEADERFILE="./examples/osm/L16-27816-42904-16-8_16.png.header.kap" 
OPTIONS="-c"
TC="1"
GenerateKap

PNGFILE="./examples/osm/L16-29296-46016-8-8_16.png" 
KAPHEADERFILE="./examples/osm/L16-29296-46016-8-8_16.png.header.kap" 
OPTIONS="-c"
TC="2"
GenerateKap

PNGFILE="./examples/osm/L16-19056-38040-8-8_16.png" 
KAPHEADERFILE="./examples/osm/L16-19056-38040-8-8_16.png.header.kap" 
OPTIONS="-c"
TC="3"
GenerateKap
