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
	./imgkap examples/PWAE10.gif 45 -95 "139;250" 25 -45 "1532;938" natl.kap
	if [ -f natl.kap ]; then \
	echo "imgkap seems working correctly."; \
	else \
	echo "imgkap seems broken."; \
	fi
