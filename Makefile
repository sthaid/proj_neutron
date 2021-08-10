build: neutron

neutron: main.c
	gcc -g -Wall -O2 $^ -lm -lpthread -lcurses -o $@

clean:
	rm -f neutron

