#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/video/video.hpp>

#include <iostream>

#include "QPCTimer.h"
#include "MixtureOfGaussianCPU.h"
#include "clw/clw.h"

int main(int, char**)
{
#if 1
	if(1)
	{
		std::cout << "List of available platforms: \n";
		std::vector<clw::Platform> platforms = clw::availablePlatforms();
		clw::for_each(platforms, [=](const clw::Platform& platform)
		{
			std::cout << "  * " << platform.name() 
				<< ", " << platform.versionString() << std::endl;
			std::cout << "    -> list of supported extensions: \n";

			std::vector<std::string> exts = platform.extensions();
			clw::for_each(exts, [=](const std::string& ext)
			{
				std::cout << "         " << ext << std::endl;
			});
		});

		std::cout << "\nList of available devices: \n";

		std::vector<clw::Device> devs = clw::allDevices();
		clw::for_each(devs, [=](const clw::Device& device)
		{
			std::cout << "  * " << device.name() << std::endl;
			std::cout << "  \t-> " << device.version() << std::endl;
			std::cout << "  \t-> " << device.vendor() << std::endl;
			std::cout << "  \t-> " << device.driverVersion() << std::endl;
			std::cout << "  \t-> language version: " << device.languageVersion() << std::endl;
			std::cout << "  \t-> supports double precision: " << (device.supportsDouble() ? "yes" : "no") << std::endl;
			std::cout << "  \t-> supports galf precision: " << (device.supportsHalf() ? "yes" : "no") << std::endl;
			std::cout << "  \t-> list of suported extensions: \n";

			std::vector<std::string> exts = device.extensions();
			clw::for_each(exts, [=](const std::string& ext)
			{
				std::cout << "  \t     " << ext << std::endl;
			});
		});
		std::cout << "\n\n";
	}

	clw::Context context;
	//if(!context.create(clw::devices(clw::All, clw::defaultPlatform())))
	if(!context.create(clw::Default))
	{
		std::cerr << "couldn't create context, quitting\n";
		std::exit(-1);
	}
	clw::Device device = context.devices()[0];
	std:: cout << "Default device: " << device.name() << "\n\n";
	std::cout << "  -> list of supported 2d image formats: \n";
	std::vector<clw::ImageFormat> formats = context.supportedImage2DFormats();
	clw::for_each(formats, [=](const clw::ImageFormat& format)
	{
		std::cout 
			<< "     " << clw::channelOrderName(format.order) << ", " 
			<< clw::channelTypeName(format.type) << std::endl;
	});

	//clw::CommandQueue queue = context.createCommandQueue(
	//	clw::Property_ProfilingEnabled, device);

	//clw::Program prog = context.buildProgramFromSourceFile("kernel2.cl");
	//clw::Kernel kernel = prog.createKernel("gaussian");

#else
	// Open sample video
	cv::VideoCapture cap("surveillance.webm");
	if(!cap.isOpened())
	{
		exit(-1);
	}

	// Retrieve frame size
	const int cols = static_cast<int>(cap.get(CV_CAP_PROP_FRAME_WIDTH));
	const int rows = static_cast<int>(cap.get(CV_CAP_PROP_FRAME_HEIGHT));

	MixtureOfGaussianCPU MOG(rows, cols);
	//cv::BackgroundSubtractorMOG MOG;
	QPCTimer timer;;

	for(;;)
	{
		// Get a new frame from camera
		cv::Mat frame;
		cap >> frame; 

		if(frame.rows == 0 || frame.cols == 0)
			break;

		// Convert it to grayscale
		cvtColor(frame, frame, CV_BGR2GRAY);

		// Estimate the background
		double start = timer.currentTime();

		cv::Mat	frameMask;
		MOG(frame, frameMask, -1);

		double stop = timer.currentTime();

		std::cout << 1.0 / (stop - start) << std::endl;		

		// Show them
		cv::imshow("Mixture of Guassian", frameMask);
		cv::imshow("Original video", frame);

		int key = cv::waitKey(30);
		if(key >= 0)
			break;
	}
#endif
}