CC			= gcc
CFLAGS		= -Wall -O3 -c -g
LDFLAGS		=
SOURCES		= ../netrace.c trace_viewer.c
OBJECTS		= $(SOURCES:.c=.o)
EXECUTABLE	= trace_viewer

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(INCLUDES) $(LIBRARIES) $(OBJECTS) -o $(EXECUTABLE) $(LIBS)

.o: $(SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBRARIES) $< -o $@ $(LIBS)

.c:
	$(CC) $(CFLAGS) $(INCLUDES) $(LIBRARIES) $< -o $@ $(LIBS)

clean:
	rm -f $(EXECUTABLE) *.o