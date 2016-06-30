#include <string>
#include <memory>

struct buffer{
		void *data;
		size_t size;
};


class Picam{
public:
	Picam(const std::string& device = "/dev/video0", int width = 640, int height = 480);
	~Picam();

	const void mainloop(int timeout = 1, int count = 60);
private:
	// function //
	void init_mmap();

	void open_device();
	void close_device();

	void init_device();
	void uninit_device();

	void start_capturing();
	void stop_capturing();
	
	bool read_frame();
	void process_image(void *p, int size);
	void set_fps(int fps);

	// variable //
	std::string	device;
	int fd;

	struct buffer *buffers;
	unsigned int n_buffers;

	size_t xres, yres;
	size_t stride;

	bool force_format;
	int frame_count;
	unsigned int frame_number;

};
