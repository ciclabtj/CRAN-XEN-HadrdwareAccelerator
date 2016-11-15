INCLUDE = tool.h
turbo_get_decoder : turbo_get_decoder.c tool.o $(INCLUDE)
	gcc -c $(CFLAGS) turbo_get_decoder.c -o turbo_get_decoder.o
	gcc $(CFLAGS) -o turbo_get_decoder turbo_get_decoder.o tool.o $(INCLUDE)

tool.o : tool.c
	gcc -c $(CFLAGS) tool.c -o tool.o

data : data.c
    gcc -o data data.c
	
clean:
	rm -f turbo_get_decoder data *.o 
