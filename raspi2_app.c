#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#define BUF_SIZE 4
#define PATH_SIZE 20
#define PIN_NUMBER 18

int main(int argc, char *argv[])
{
	int fd, value;
	char path[PATH_SIZE];
	char buf[BUF_SIZE];
	unsigned int pin_number;

	pin_number = 23;
	if(argc != 2){
		printf("Option low/high must be used\n");
	}

	fd = open("/dev/raspi2GPIO23", O_WRONLY);
	if(fd < 0){
		perror("Error opening GPIO pins\n");
	}
	printf("Set GPIO pins to output, logic level %s\n", argv[1]);
	strncpy(buf, "out", 3);
	buf[3] = '\0';
	if(write(fd, buf, sizeof(buf)) < 0){
		perror("write, set pin output\n");
	}

	value = atoi(argv[1]);
	printf("value : %d\n", value);
	if(value == 1){
		strncpy(buf, "1", 1);
		buf[1] = '\0';
	}else if (value == 0){
		strncpy(buf, "0", 1);
		buf[1] = '\0';
	}else{
		printf("Invalid logic value\n");
	}

	if(write(fd, buf, sizeof(buf)) < 0){
		perror("write, set GPIO state of GPIO pins");
	}
	else{
		printf("write %d success\n", value);
	}
	close(fd);
	return EXIT_SUCCESS;
}
