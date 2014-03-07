# build executable 
all: LinzerSchnitteMidibeta0.5.c hw_params.c
	gcc -lm -lasound -o LinzerSchnitteMidi LinzerSchnitteMidibeta0.5.c
	gcc -lasound -o hw_params hw_params.c	

clean:
	$(RM) LinzerSchnitteMidi
	$(RM) hw_params
