

all:  write_encode_v2 write_encode  pdma-read pdma-write pdma-dma-start pdma-dma-stop pdma-info pdma-rw-reg

#vpath %.h ../
#VPATH=../

INCLUDE=pdma-lib.h ../pdma-ioctl.h

write_encode_v2: write_encode_v2.c pdma-lib.o $(INCLUDE)
	gcc -c $(CFLAGS) write_encode_v2.c -o write_encode_v2.o
	gcc $(CFLAGS) -o write_encode_v2 write_encode_v2.o pdma-lib.o $(INCLUDE)

write_encode: write_encode.c pdma-lib.o $(INCLUDE)
	gcc -c $(CFLAGS) write_encode.c -o write_encode.o
	gcc $(CFLAGS) -o write_encode write_encode.o pdma-lib.o $(INCLUDE)


pdma-read: pdma-read.c pdma-lib.o $(INCLUDE)
	gcc -c $(CFLAGS) pdma-read.c -o pdma-read.o
	gcc $(CFLAGS) -o pdma-read pdma-read.o pdma-lib.o $(INCLUDE)

pdma-write: pdma-write.c pdma-lib.o $(INCLUDE)
	gcc -c $(CFLAGS) pdma-write.c -o pdma-write.o
	gcc $(CFLAGS) -o pdma-write pdma-write.o pdma-lib.o $(INCLUDE)

pdma-dma-start: pdma-dma-start.c pdma-lib.o $(INCLUDE)
	gcc -c $(CFLAGS) pdma-dma-start.c -o pdma-dma-start.o
	gcc $(CFLAGS) -o pdma-dma-start pdma-dma-start.o pdma-lib.o $(INCLUDE)

pdma-dma-stop: pdma-dma-stop.c pdma-lib.o $(INCLUDE)
	gcc -c $(CFLAGS) pdma-dma-stop.c -o pdma-dma-stop.o
	gcc $(CFLAGS) -o pdma-dma-stop pdma-dma-stop.o pdma-lib.o $(INCLUDE)

pdma-info: pdma-info.c pdma-lib.o $(INCLUDE)
	gcc -c $(CFLAGS) pdma-info.c -o pdma-info.o
	gcc $(CFLAGS) -o pdma-info pdma-info.o pdma-lib.o $(INCLUDE)

pdma-rw-reg: pdma-rw-reg.c pdma-lib.o $(INCLUDE)
	gcc -c $(CFLAGS) pdma-rw-reg.c -o pdma-rw-reg.o
	gcc $(CFLAGS) -o pdma-rw-reg pdma-rw-reg.o pdma-lib.o $(INCLUDE)

pdma-lib.o: pdma-lib.c
		gcc -c $(CFLAGS) pdma-lib.c -o pdma-lib.o

clean:
	rm -f pdma-read pdma-write pdma-dma-stop pdma-dma-start pdma-info \
	pdma-rw-reg *.o

.PHONY: all clean
