all: imgkap

imgkap:
	gcc imgkap.c -O3 -s -lm -lfreeimage -o imgkap

install: imgkap
	cp imgkap /usr/local/bin

clean:
	rm imgkap
