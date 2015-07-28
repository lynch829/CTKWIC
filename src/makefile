CC=gcc

ODIR=obj

IDIR_RECON=../include/
IDIR_READER=../lib/reader/
INCLUDES=-I$(IDIR_RECON) -I$(IDIR_READER)

LIBS=$(IDIR_READER)read_raw_file.o -lfftw3 -lm 

CFLAGS=$(INCLUDES) -L$(IDIR_READER) -g -std=c99

_OBJ=main.o setup.o interp.o rebin_cpu.o
OBJ=$(patsubst %,$(ODIR)/%,$(_OBJ));

all: ../test

../test: $(OBJ)
	(cd obj && $(CC) -L$(IDIR_READER) -o ../../test ../$(IDIR_READER)read_raw_file.o *.o -lfftw3 -lm)

obj/main.o: main.c $(IDIR_RECON)recon_structs.h $(IDIR_RECON)setup.h $(IDIR_RECON)rebin_cpu.h
	$(CC) $(CFLAGS) -c -o $@ $<

obj/setup.o: setup.c $(IDIR_READER)read_raw_file.h $(IDIR_RECON)setup.h
	$(CC) $(CFLAGS) -c -o $@ $<

obj/interp.o: interp.c $(IDIR_RECON)interp.h
	$(CC) $(CFLAGS) -c -o $@ $<

obj/rebin_cpu.o: rebin_cpu.c $(IDIR_RECON)rebin_cpu.h 
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: all test clean

clean:
	rm obj/*.o; rm -rf ../*~

