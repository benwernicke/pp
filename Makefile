SRC = main.c
OUT = main
FLAGS = -g -Wall -pedantic -lpthread

all:
	gcc $(SRC) $(FLAGS) -o $(OUT)

clean:
	rm $(OUT) || rm -rf package
