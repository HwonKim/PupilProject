#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdio.h> //sprintf, fopen, fflush etc...
#include <iostream>


#include <stdexcept>

#include <linux/videodev2.h>

#include "picam_v4l2_ctrl.h"

#define CLEAR(x) memset(&(x),0, sizeof(x))

using namespace std;

static int xioctl(int fh, unsigned long int request, void *arg)
{
	int r;

	do{
		r = ioctl(fh, request, arg);
	}while(-1 == r && EINTR == errno);

	return r;
}

Picam::Picam(const string& device, int width, int height) :
	device(device), xres(width), yres(height)
{
	force_format = true;
	frame_number = 0;
	open_device();
	init_device();
	start_capturing();
}

Picam::~Picam()
{
	stop_capturing();
	uninit_device();
	close_device();

}

void Picam::open_device(void)
{
	struct stat st;

	if(-1 == stat(device.c_str(), &st))
		throw runtime_error(device + " : cannot identify! ");

	if(!S_ISCHR(st.st_mode))
		throw runtime_error(device + " is no device");

	fd = open(device.c_str(), O_RDWR | O_NONBLOCK, 0);

	if(-1 == fd)
		throw runtime_error(device + " : cannot open! ");
}


void Picam::init_device(void)
{
	struct v4l2_capability cap;
	struct v4l2_cropcap cropcap;
	struct v4l2_crop crop;
	struct v4l2_format fmt;
	unsigned int min;

	if(-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)){
		if(EINVAL == errno){
			throw runtime_error(device + " is no v4l2 device");
		}else{
			throw runtime_error("VIDIOC_QUERYCAP");
		}
	}

	if(!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
		throw runtime_error(device + " is no video capture device");

	if(!(cap.capabilities & V4L2_CAP_STREAMING))
		throw runtime_error(device + " does not support streaming i/o");

	// Select video input, video standard and true here //

	CLEAR(cropcap);

	cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if( 0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)){
		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		crop.c = cropcap.defrect;

		if( -1 == xioctl(fd, VIDIOC_S_CROP, &crop)){
			switch(errno){
				case EINVAL:
					// cropping is not support //
					break;
				default:
					break;
			}
		}

	}else{
	}

	CLEAR(fmt);

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if(force_format){
		fmt.fmt.pix.width = xres;
		fmt.fmt.pix.height = yres;
		fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
		fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

		if( -1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
			throw runtime_error("VIDIOC_S_FMT");

		
		if(fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG)
			throw runtime_error("Picam does not support MJPEG format");

		stride = fmt.fmt.pix.bytesperline;
	}else{
		if( -1 == xioctl(fd, VIDIOC_G_FMT, &fmt)){
			throw runtime_error("VIDIOC_G_FMT");
		}
	}

	set_fps(60);
	init_mmap();
}

void Picam::init_mmap(void)
{
	struct v4l2_requestbuffers req;

	CLEAR(req);

	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if( -1 == xioctl(fd, VIDIOC_REQBUFS, &req)){
		if(EINVAL == errno){
			throw runtime_error(device + " does not support memory mapping");
		} else {
			throw runtime_error("VIDIOC_REQBUFS");
		}
	}

	if (req.count < 2)
		throw runtime_error(string("Insufficient buffer memory on ") + device);

	buffers = (buffer*) calloc(req.count, sizeof(*buffers));

	if(!buffers)
		throw runtime_error("Out of memory");

	for(n_buffers = 0; n_buffers < req.count; ++n_buffers){
		struct v4l2_buffer buf;

		CLEAR(buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = n_buffers;

		if( -1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
			throw runtime_error("VIDIOC_QUERYBUF");

		buffers[n_buffers].size = buf.length;
		buffers[n_buffers].data = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);

		if(MAP_FAILED == buffers[n_buffers].data)
			throw runtime_error("mmap");
	}
}

void Picam::start_capturing(void)
{
	unsigned int i;
	enum v4l2_buf_type type;

	for(i = 0; i < n_buffers; ++i){
		struct v4l2_buffer buf;

		CLEAR(buf);
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;

		if( -1 == xioctl(fd, VIDIOC_QBUF, &buf))
			throw runtime_error("VIDIOC_QBUF");

		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if( -1 == xioctl(fd, VIDIOC_STREAMON, &type))
			throw runtime_error("VIDIOC_STREAMON");

	}
}

const void Picam::mainloop(int timeout, int count)
{
	while(count-- > 0){
		for(;;){
			fd_set fds;
			struct timeval tv;
			int r;

			FD_ZERO(&fds);
			FD_SET(fd, &fds);

			tv.tv_sec = timeout;
			tv.tv_usec = 0;

			r = select(fd + 1, &fds, NULL, NULL, &tv);

			if(-1 == r){
				if(EINTR == errno)
					continue;
				throw runtime_error("select");
			}

			if( 0 == r){
				throw runtime_error(device + " : select timeout");
			}

			if(read_frame())
				break;

		}
	}
}


bool Picam::read_frame()
{
	struct v4l2_buffer buf;
	unsigned int i;

	CLEAR(buf);

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;

	if ( -1 == xioctl(fd, VIDIOC_DQBUF, &buf)){
		switch(errno){
			case EAGAIN:
				return -1;
			case EIO:
			default:
				throw runtime_error("VIDIOC_DQBUF");
		}
	}

	assert(buf.index < n_buffers);

	process_image(buffers[buf.index].data, buf.bytesused);

	if(-1 == xioctl(fd, VIDIOC_QBUF, &buf))
		throw runtime_error("VIDIOC_QBUF");

	return 1;

}


void Picam::process_image(void *p, int size)
{
	char filename[15];
	++frame_number;
	sprintf(filename, "frame%d.jpg", frame_number);
	
	FILE *fp = fopen(filename, "wb");
	fwrite(p, size, 1, fp);

	fflush(fp);
	fclose(fp);
}

void Picam::stop_capturing(void)
{
	enum v4l2_buf_type type;

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if( -1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
		throw runtime_error("VIDIOC_STREAMOFF");

}

void Picam::uninit_device(void)
{
	unsigned int i;

	for(i = 0; i < n_buffers; ++i)
	{
		if( -1 == munmap(buffers[i].data, buffers[i].size))
			throw runtime_error("munmap");
	}
	free(buffers);
}

void Picam::close_device(void)
{
	if( -1 == close(fd))
		throw runtime_error("close");

	fd = -1;
}


void Picam::set_fps(int fps){
	struct v4l2_streamparm streamparm;
	CLEAR(streamparm);
	streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	streamparm.parm.capture.timeperframe.numerator = 1;
	streamparm.parm.capture.timeperframe.denominator = fps; 
	if(xioctl(fd, VIDIOC_S_PARM, &streamparm))
		throw runtime_error("VIDIOC_S_PARM");
}

