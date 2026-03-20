CC = gcc
CFLAGS = -I. -Iinclude -Iminiaudio -O2 -std=c99
SRC = src/main.c src/ksynth.c src/dsp.c src/audio.c src/miniaudio.c
OBJ = $(SRC:.c=.o)
OUT = ksynth

all: $(OUT)

$(OUT): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lm

clean:
	rm -f $(OBJ) $(OUT)
