all: imgkap

imgkap:
	gcc imgkap.c -O3 -s -lm -lfreeimage -o imgkap

install: imgkap
	cp imgkap /usr/local/bin

clean:
	rm imgkap
	if [ -f natl.kap ]; then \
	rm natl.kap; \
	fi

test: imgkap
	./imgkap -w examples/image.png "52°20'" "3°40'" "1871;146" "51°40'" "1°10'" "50;934" -r "500;96-1918;96-1918;968-142;968-142;541" natl.kap
	if [ -f natl.kap ]; then \
	echo "imgkap seems working correctly."; \
	else \
	echo "imgkap seems broken."; \
	fi
