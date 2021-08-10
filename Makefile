build: neutron

neutron: main.c util_mccdaq.c mccdaq_cb.c
	gcc -g -Wall -O2 -I. $^ -lm -lpthread -lcurses -lmccusb -lhidapi-libusb -lusb-1.0 -o $@

clean:
	rm -f neutron

