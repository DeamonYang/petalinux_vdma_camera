APP = dma-proxy-test

# Add any other object files to this list below
APP_OBJS = dma-proxy-test.o

all: $(APP)

$(APP): $(APP_OBJS)
	$(CC) -lm -lpthread $(LDFLAGS) -o $@ $(APP_OBJS) $(LDLIBS)

clean:
	-rm -f $(APP) *.elf *.gdb *.o


