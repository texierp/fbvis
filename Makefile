CC = cc
CFLAGS = -Wall -O2 -DSTB_IMAGE_IMPLEMENTATION
LDFLAGS = -lm

all: fbvis
%.o: %.c
	$(CC) -c $(CFLAGS) $<
fbvis: fbvis.o draw.o ppm.o stb_image.o lodepng.o
	$(CC) -o $@ $^ $(LDFLAGS)
clean:
	rm -f *.o fbvis
