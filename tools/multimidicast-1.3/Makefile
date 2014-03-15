# $Header: /code/multimidicast/Makefile,v 1.2 2007/10/22 17:57:03 doj Exp $

CXXFLAGS+=-Wall -O2
LIBS+=-lasound

multimidicast:	multimidicast.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	$(RM) multimidicast *.o *~
