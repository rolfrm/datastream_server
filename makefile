OPT = -g3 -Og
LIB_SOURCES1 = datastream_server.c
LIB_SOURCES = $(addprefix src/, $(LIB_SOURCES1))
CC = gcc
TARGET = libdatastream_server.so
LIB_OBJECTS =$(LIB_SOURCES:src/%.c=obj/%.o)
LDFLAGS= -L. $(OPT) -Wextra -shared -fPIC #-lmcheck #-ftlo #setrlimit on linux 
LIBS= -liron -lmicrohttpd
ALL= $(TARGET) 
CFLAGS = -Iinclude -Isrc -std=c11 -c $(OPT) -D_GNU_SOURCE -Wall -Wextra -Werror=implicit-function-declaration -Wformat=0  -fdiagnostics-color -Wextra -Werror -Wwrite-strings -fbounds-check  #-DDEBUG

$(TARGET): directories $(LIB_OBJECTS)
	$(CC) $(LDFLAGS) $(LIB_OBJECTS) $(LIBS) --shared -o $@

all: $(ALL)

obj/%.o : src/%.c
	$(CC) $(CFLAGS) -fPIC $< -o $@ -MMD -MF $@.depends

directories:
	mkdir -p obj

depend: h-depend
clean:
	rm -f $(LIB_OBJECTS) $(ALL) obj/*.o.depends obj/*.o.depends

test: $(TARGET) obj/server_app.o
	$(CC) $(OPT) $(LIB_OBJECTS) obj/server_app.o $(LIBS)  -o $@

-include $(LIB_OBJECTS:.o=.o.depends)
