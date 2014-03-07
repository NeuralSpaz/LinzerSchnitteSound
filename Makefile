# build executable 
all: LinzerSchnitteMidibeta0.5.c
	gcc -lm -lasound -o LinzerSchnitteMidi LinzerSchnitteMidibeta0.5.c

clean:
	$(RM) LinzerSchnitteMidi
