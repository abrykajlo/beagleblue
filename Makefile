all:
	gcc -o test test.c beagleblue.c -lbluetooth -pthread
clean:
	rm test