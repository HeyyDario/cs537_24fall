CC = gcc
CFLAGS = -Wall -Wextra -Werror -pedantic -std=gnu18
LOGIN = chengtao
SUBMITPATH = ~cs537-1/handin/$(LOGIN)/p3
TARGET = wsh

.PHONY: all
all: $(TARGET) $(TARGET)-dbg

$(TARGET): $(TARGET).c $(TARGET).h
	$(CC) $(CFLAGS) -O2 -o $@ $< 

$(TARGET)-dbg: $(TARGET).c $(TARGET).h
	$(CC) $(CFLAGS) -Og -ggdb -o $@ $< 

clean: 
	rm -f $(TARGET) $(TARGET)-dbg

submit: clean
	cp -r ../../p3/ $(SUBMITPATH)

