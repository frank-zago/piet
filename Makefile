CFLAGS = -O2 -ggdb -Wall -Wmissing-prototypes
LDLIBS=-lSDL_image -lSDL

all: piet

piet.c: piet.h

clean:
	rm -f *.o core piet

test:
	./piet hw1-11.gif 11
	./piet hw4-1.gif 1
	./piet hw3-5.gif 5
	./piet helloworld-mondrian.png 1
	echo 12 | ./piet piet_factorial.png 1
	./piet hi.png 16
	./piet Piet_hello.png 1
	echo 32 54 | ./piet adder.png 1
	./piet piet_pi.ppm 1
	echo 12 3 | ./piet power2.png 1
	./piet hw6_big.png 5
	./piet tetris.png 1
	./piet helloworld-piet.gif 1
	./piet helloworld-piet.gif 2
	echo "It's alive." | ./piet cowsay.png 1
	./piet 99bottles.png 1
	echo 10 6 | ./piet ./euclid_clint_big.png 10
