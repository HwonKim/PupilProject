#include <iostream>
#include <fstream>

#include "picam_v4l2_ctrl.h"

#define XRES 640
#define YRES 480

using namespace std;

int main(int argc, char *argv[])
{
	Picam picam("/dev/video0", XRES, YRES);

	picam.mainloop(1, 60);

	return 0;
}
