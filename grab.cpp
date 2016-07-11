#include <iostream>
#include <fstream>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdexcept> // runtime_error
#include <string.h> // strncpy
#include <fcntl.h> //open option
#include <pthread.h> //thread

#include "picam_v4l2_ctrl.h"

#define XRES 640
#define YRES 480
#define BUF_SIZE 4
#define IR_DEV "/dev/raspi2GPIO18"
#define WHITE_DEV "/dev/raspi2GPIO23"

using namespace std;

int fd_White;

void *t_function(void* data)
{
	char* buf = (char*)data;
	
	sleep(1);
	if(write(fd_White, buf, sizeof(buf))< 0)
		cout << "white led on fail" << endl;
	usleep(20000);

	strncpy(buf, "0", 1);
	buf[1] = '\0';
	if(write(fd_White, buf, sizeof(buf)) <0)
		cout << "white led off fail" << endl;

}

int main(int argc, char *argv[])
{

	int fd_IR;
	pthread_t p_thread;
	int thr_id;
	char buf[BUF_SIZE];
	Picam picam("/dev/video0", XRES, YRES);

	fd_IR = open(IR_DEV, O_WRONLY);
	fd_White = open(WHITE_DEV, O_WRONLY);

	if(fd_IR < 0)
		runtime_error("Can not open IR GPIO DEV");

	if(fd_White < 0)
		cout << "white led dev open error" << endl;

	strncpy(buf, "out", 3);
	buf[3] = '\0';

	if(write(fd_IR, buf, sizeof(buf)) < 0){
		runtime_error("Can not set output IR LED");
		exit(EXIT_FAILURE);
	}
	if(write(fd_White, buf, sizeof(buf)) < 0){
		cout << "white led output error" << endl;
	}
	

	strncpy(buf, "1", 1);
	buf[1] = '\0';

	if(write(fd_IR, buf, sizeof(buf)) < 0){
		runtime_error("Can not write On");
		exit(EXIT_FAILURE);
	}


	thr_id = pthread_create(&p_thread, NULL, t_function, (void*)buf);
	if(thr_id < 0){
		runtime_error("Create Thread failed");
		exit(0);
	}

	picam.mainloop(1, 180);

	strncpy(buf, "0", 1);
	buf[1] = '\0';

	if(write(fd_IR, buf, sizeof(buf)) < 0){
		runtime_error("Can not write Off");
		exit(EXIT_FAILURE);
	}

	pthread_detach(p_thread);
	return 0;
}
