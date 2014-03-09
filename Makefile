# build executable 
all: LinzerSchnitteMidibeta0.5.c hw_params.c LinzerSchnitteMidibeta0.6.c
	gcc -lm -lasound -o LinzerSchnitteMidi LinzerSchnitteMidibeta0.5.c 
	gcc -lm -lasound -lcurses -o LinzerSchnitteMidiTesting LinzerSchnitteMidibeta0.6.c
	gcc -lm -lasound -lcurses -o LinzerSchnitteMidi0.7 LinzerSchnitteMidibeta0.7.c 
	gcc -lasound -o hw_params hw_params.c	

clean:
	$(RM) LinzerSchnitteMidi
	$(RM) LinzerSchnitteMidiTesting
	$(RM) hw_params
	$(RM) LinzerSchnitteMidi0.7
