CC= gcc
INCLUDES= 
CFLAGS= -O2
LIBS= -lmpg123 -lao

all: audio test

audio:  audio.c
	$(CC) -shared $(CFLAGS) $(INCLUDES) -o libaudio.so audio.c $(LIBS)

test: test.cpp
	cc test.cpp -o test -L. -laudio

clean:
	rm -f libaudio.so test
