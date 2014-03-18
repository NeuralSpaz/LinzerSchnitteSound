# build executable
all: LinzerSchnitteMidibeta0.5.c hw_params.c LinzerSchnitteMidibeta0.6.c
	gcc LinzerSchnitteMidibeta0.5.c -o LinzerSchnitteMidi -lm -lasound
	gcc LinzerSchnitteMidibeta0.6.c -o LinzerSchnitteMidiTesting -lm -lasound -lcurses 
	gcc LinzerSchnitteMidibeta0.7.c -o LinzerSchnitteMidi0.7 -lm -lasound -lcurses
	gcc LinzerSchnitteMidibeta0.7.c -o LSMidi -lm -lasound -lcurses
	gcc hw_params.c -o hw_params -lasound	

clean:
	$(RM) LinzerSchnitteMidi
	$(RM) LinzerSchnitteMidiTesting
	$(RM) hw_params
	$(RM) LinzerSchnitteMidi0.7
	$(RM) LSMidi
	

