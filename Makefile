CC = gcc
CFLAGS = -g -D_REENTRANT
LIBS = -lm

all: myftp myftpserve

myftp: myftp.c myftp.h
    $(CC) $(CFLAGS) -o myftp myftp.c $(LIBS)

myftpserve: myftpserve.c myftp.h
    $(CC) $(CFLAGS) -o myftpserve myftpserve.c $(LIBS)

clean:
    rm -f myftp myftpserve
