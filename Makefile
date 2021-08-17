build: neutron

neutron: main.c util_mccdaq.c mccdaq_cb.c utils.c
	gcc -g -Wall -O2 -I. $^ -lm -lpthread -lcurses -lmccusb -lhidapi-libusb -lusb-1.0 -o $@
	@#sudo chown root:root $@
	@#sudo chmod 4777 $@

clean:
	rm -f neutron

clobber:
	rm -f neutron neutron.log neutron*.dat

