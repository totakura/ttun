.PHONY: all

all: ttun

ttun: ttun.c
	gcc -o $@ -g -O0 -levent_core $^

clean:
	rm -f ttun
