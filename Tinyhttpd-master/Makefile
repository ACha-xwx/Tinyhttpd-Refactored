all: httpd client
LIBS = -lpthread #-lsocket
httpd: httpd.c sds.c
	gcc -g -W -Wall $(LIBS) -o $@ httpd.c sds.c

client: simpleclient.c
	gcc -W -Wall -o $@ $<
clean:
	rm httpd client
