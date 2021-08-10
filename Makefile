build: neutron

neutron: main.c util_mccdaq.c
	gcc -g -Wall -O2 $^ -lm -lpthread -lcurses -o $@

clean:
	rm -f neutron

