all: imgkap kapfit

imgkap: imgkap.c
	gcc imgkap.c -O3 -s -lm -lfreeimage -o imgkap

kapfit:
	gcc kapfit.c -O0 -s -lm -o kapfit

install: imgkap kapfit
	cp imgkap /usr/local/bin
	cp kapfit /usr/local/bin

clean:
	rm imgkap
	if [ -f natl.kap ]; then \
	rm natl.kap; \
	fi
	rm kapfit

test: imgkap
	./imgkap examples/PWAE10.gif 45 -95 "139;250" 25 -45 "1532;938" natl.kap
	if [ -f natl.kap ]; then \
	echo "imgkap seems working correctly."; \
	else \
	echo "imgkap seems broken."; \
	fi
