build: simple

simple: simple.c util_mccdaq.c mccdaq_cb.c utils.c
	gcc -g -Wall -O2 -I. $^ -lm -lpthread -lcurses -lmccusb -lhidapi-libusb -lusb-1.0 -o $@

clean:
	rm -f simple

clobber:
	rm -f simple simple.log
