# build executable
CC = gcc
CXX = g++
CFLAGS = -Wall -Werror
LIBS+= -lasound -lm

all: hw_params LSmidi5 LSmidi6 LSmidi7 multimidicast.o
	$(CXX) -o multimidicast multimidicast.o -lasound

LSmidi5:
	$(CC) $(CFLAGS) -o LSmidi5 LinzerSchnitteMidibeta0.5.c $(LIBS)

LSmidi6:
	$(CC) $(CFLAGS) -o LSmidi6 LinzerSchnitteMidibeta0.6.c $(LIBS) -lcurses 

LSmidi7:
	$(CC) $(CFLAGS) -o LSmidi7 LinzerSchnitteMidibeta0.7.c $(LIBS)

hw_params: hw_params.c
	$(CC) $(CFLAGS) -c -o hw_params hw_params.c $(LIBS)

multimidicast.o:
	$(CXX) -Wall -O2 -c -o multimidicast.o multimidicast.cpp


clean:
	$(RM) *.o
	$(RM) LSmidi5
	$(RM) LSmidi6
	$(RM) LSmidi7
	$(RM) hw_params
	$(RM) multimidicast
	