.PHONY: all

all: ttun

ttun: ttun.c
	gcc -o $@ -levent_core $^

clean:
	rm -f ttun
