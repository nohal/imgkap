## imgkap 1.13
Tool to manipuate raster nautical charts in KAP format

[![Build Status](https://travis-ci.org/nohal/imgkap.svg?branch=master)](https://travis-ci.org/nohal/imgkap)

This repository is a Github clone of the original code created by M'dJ in 2011 and maintained by Pavel Kalian

### Usage
```
imgkap [option] [inputfile] [lat0 lon0 [x0;y0] lat1 lon1 [x1;y1] | headerfile] [outputfile]

Usage of imgkap Version 1.13 by M'dJ + H.N

Convert kap to img :
  >imgkap mykap.kap myimg.png
    -convert mykap into myimg.png
  >imgkap mykap.kap mheader.kap myimg.png
    -convert mykap into header myheader (only text file) and myimg.png

Convert img to kap : 
  >imgkap myimg.png myheaderkap.kap myresult.kap
    -convert myimg.png into myresult.kap using myheader.kap for kap informations
  >imgkap mykap.png lat0 lon0 lat1 lon1 myresult.kap
    -convert myimg.png into myresult.kap using WGS84 positioning
  >imgkap mykap.png lat0 lon0 x0;y0 lat1 lon1 x1;y1 myresult.kap
    -convert myimg.png into myresult.kap
  >imgkap -s 'LOWEST LOW WATER' myimg.png lat0 lon0 lat1 lon2 -f
    -convert myimg.png into myimg.kap using WGS84 positioning and options

Convert kml to kap : 
  >imgkap mykml.kml
    -convert GroundOverlay mykml file into kap file using name and dir of image
  >imgkap mykml.kml mykap.kap
    -convert GroundOverlay mykml into mykap file

WGS84 positioning :
	lat0 lon0 is a left,top point
	lat1 lon1 is a right,bottom point
	lat to be between -85 and +85 degree
	lon to be between -180 and +180 degree
	    different formats are accepted : -1.22  1°10'20.123N  -1d22.123 ...
	x;y can be used if lat lon is not a left, top and right, bottom point
	    lat0 lon0 x0;y0 must be in the left, upper third of the image
	    lat1 lon1 x1;y1 must be in the right, lower third of the image
Options :
	-w  : no image size extension to WGS84 because image is already WGS84
	-r x0f;y0f-x1f;y1f  "2 pixel points -> 4 * PLY"
	    : define a rectangle area in the image visible from the .kap
	-r x0f;y0f-x1f;y1f-x2f;y2f-x3f;y3f... "3 to 10 pixel points -> PLY"
	    : define a up to 10 edges polygon visible from the .kap
	-n  : Force compatibility all KAP software, max 127 colors
	-f  : fix units to FATHOMS
	-s name : fix sounding datum
	-t title : change name of map
	-p color : color of map
	   color (Kap to image) : ALL|RGB|DAY|DSK|NGT|NGR|GRY|PRC|PRG
	     ALL generate multipage image, use only with GIF or TIF
	   color (image or Kap to Kap) :  NONE|KAP|MAP|IMG
	     NONE use colors in image file, default
	     KAP only width KAP or header file, use RGB tag in KAP file
	     MAP generate DSK and NGB colors for map scan
	       < 64 colors: Black -> Gray, White -> Black
	     IMG generate DSK and NGB colors for image (photo, satellite...)
```
