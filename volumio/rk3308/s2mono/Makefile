ALSA_DIR = /usr/include/alsa
CFLAGS = -Wall -fPIC -DPIC -O3 -I$(ALSA_DIR)
LDFLAGS = -shared -lasound

all: libasound_module_pcm_s2mono.so

libasound_module_pcm_s2mono.so: s2mono.o
	$(CC) $(LDFLAGS) -o $@ $^

s2mono.o: s2mono.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.so

install: libasound_module_pcm_s2mono.so
	cp libasound_module_pcm_s2mono.so /usr/lib/arm-linux-gnueabihf/alsa-lib/