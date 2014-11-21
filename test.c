#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "beagleblue.h"

void callback(char *buf)
{
	printf("%s", buf);
	fflush(stdout);
	if (strncmp(buf, "exit", 4) == 0) {
		beagleblue_exit();
	}
}

int main()
{
	char buf[1024] = { 0 };
	beagleblue_init(&callback);
	int i = 0;
	while (1) {
		sprintf(buf, "{\"temperature\":{\"temp0\":%d,\"temp1\":%d}}", i, i);
		beagleblue_android_send("{\"temperature\":{\"temp0\":%d,\"temp1\":1.356}}", BUFFER_SIZE);
		memset(buf, 0, sizeof(buf));
		i++;
		sleep(1);
	}
	return 0;
}