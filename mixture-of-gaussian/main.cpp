#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/video/video.hpp>

#include <iostream>
#include <memory>

#include <clw/clw.h>

#include "QPCTimer.h"
#include "ConfigFile.h"
#include "FrameGrabber.h"

#include "MixtureOfGaussianGPU.h"
#include "GrayscaleGPU.h"
#include "BayerFilterGPU.h"

namespace clwutils
{
	std::vector<clw::Device> pickSingleDevice()
	{
		std::vector<clw::Platform> platforms = clw::availablePlatforms();
		if(platforms.empty())
		{
			std::cerr << "No OpenCL platfrom has been detected!\n";
			std::exit(-1);
		}

		clw::Platform platform;

		// Jesli mamy tylko jedna platforme to ja wybierz bez pytania
		if(platforms.size() == 1)
		{
			platform = platforms[0];
			std::cout << "Using OpenCL platform: " << platform.name() 
				<< ", " << platform.versionString() << std::endl;
		}
		else
		{
			std::cout << "List of available OpenCL platforms: \n";

			for(size_t index = 0; index < platforms.size(); ++index)
			{
				std::cout << "  " << index + 1 << ") " 
					<< platforms[index].name() << std::endl;
			}

			int choice = 0;
			while(choice > (int)(platforms.size()) || choice <= 0)
			{
				std::cout << "Choose OpenCL platform: ";
				std::cin >> choice;
				std::cin.clear();
				std::cin.sync();
			}
			platform = platforms[choice-1];
		}

		std::vector<clw::Device> devices = clw::devices(clw::All, platform);

		// Wybierz urzadzenie
		if(devices.size() == 1)
		{
			std::cout << "Using " << devices[0].name() << std::endl;
		}
		else
		{
			std::cout << "\nList of available devices:\n";
			for(size_t index = 0; index < devices.size(); ++index)
			{
				std::cout << "  " << index + 1 << ") " << devices[index].name()
					<< ", " << devices[index].vendor() << std::endl;
			}
			std::cout << "\n";

			int choice = 0;
			while(choice > (int)(devices.size()) || choice <= 0)
			{
				std::cout << "Choose OpenCL device: ";
				std::cin >> choice;
				std::cin.clear();
				std::cin.sync();
			}

			clw::Device dev = devices[choice-1];
			devices.clear();
			devices.push_back(dev);
		}

		return devices;
	}
}

class WorkerCPU
{
public:
	WorkerCPU(ConfigFile& cfg)
		: showIntermediateFrame(false)
		, cfg(cfg)
	{}

	bool init(const std::string& videoStream)
	{
		learningRate = std::stof(cfg.value("LearningRate", "MogParameters"));
		const int nmixtures = std::stoi(cfg.value("NumMixtures", "MogParameters"));
		if(nmixtures <= 0)
		{
			std::cerr << "Parameter NumMixtures is wrong, must be more than 0\n";
			return false;
		}

		// Inicjalizuj frame grabbera
#if defined(SAPERA_SUPPORT)
		// Sprawdz suffix videoStream (.ccf)
		size_t pos = videoStream.find_last_of(".ccf");
		if(pos+1 == videoStream.length())
		{
			grabber = std::unique_ptr<FrameGrabber>(new SaperaFrameGrabber());
		}
		else
#endif
		{
			grabber = std::unique_ptr<FrameGrabber>(new OpenCvFrameGrabber());
		}

		if(!grabber->init(videoStream))
			return false;
		dstFrame = cv::Mat(grabber->frameHeight(), grabber->frameWidth(), CV_8UC1);

		// Pobierz dane o formacie ramki
		int width = grabber->frameWidth();
		int height = grabber->frameHeight();
		int channels = grabber->frameNumChannels();

		mog = cv::BackgroundSubtractorMOG(200, nmixtures,
			std::stof(cfg.value("BackgroundRatio", "MogParameters")));
		mog.initialize(cv::Size(height, width), CV_8UC1);

		std::cout << "\n  frame width: " << width <<
			"\n  frame height: " << height << 
			"\n  num channels: " << channels << "x" << grabber->framePixelDepth() << " bits \n";
		//inputFrameSize = width * height * channels * sizeof(cl_uchar);

		if(channels == 3)
		{
			std::cout << "  preprocessing frame: grayscalling\n";
			preprocess = 1;
		}
		else if(channels == 1 && grabber->needBayer())
		{
			std::string bayerCfg = cfg.value("Bayer", "General");
			if(bayerCfg == "RG") bayer = Bayer_RG;
			else if(bayerCfg == "BG") bayer = Bayer_BG;
			else if(bayerCfg == "GR") bayer = Bayer_GR;
			else if(bayerCfg == "GB") bayer = Bayer_GB;
			else
			{
				std::cerr << "Unknown 'Bayer' parameter (must be RG, BG, GR or GB)";
				return false;
			}
			std::cout << "  preprocessing frame: bayer " << bayerCfg << "\n";
			preprocess = 2;
		}
		else
		{
			std::cout << "  preprocessing frame: none (already monochrome format)\n";
			preprocess = 0;
		}

		showIntermediateFrame = cfg.value("ShowIntermediateFrame", "General") == "yes";
		if(showIntermediateFrame)
			interFrame = cv::Mat(height, width, CV_8UC1);

		return true;
	}

	void processFrame()
	{
		cv::Mat sourceMogFrame;

		// Grayscalling
		if(preprocess == 1)
		{
			cv::cvtColor(srcFrame, sourceMogFrame, CV_BGR2GRAY);
		}
		// Bayer filter
		else if(preprocess == 2)
		{
			switch(bayer)
			{
			case Bayer_RG: cv::cvtColor(srcFrame, sourceMogFrame, CV_BayerRG2GRAY); break;
			case Bayer_BG: cv::cvtColor(srcFrame, sourceMogFrame, CV_BayerBG2GRAY); break;
			case Bayer_GR: cv::cvtColor(srcFrame, sourceMogFrame, CV_BayerGR2GRAY); break;
			case Bayer_GB: cv::cvtColor(srcFrame, sourceMogFrame, CV_BayerGB2GRAY); break;
			}			
		}
		// Passthrough
		else
		{
			sourceMogFrame = srcFrame;
		}

		if(showIntermediateFrame && preprocess != 0)
			interFrame = sourceMogFrame;

		mog(sourceMogFrame, dstFrame, learningRate);
	}

	bool grabFrame()
	{
		bool success;
		srcFrame = grabber->grab(&success);
		return success;
	}

	const cv::Mat& finalFrame() const { return dstFrame; }
	const cv::Mat& sourceFrame() const { return srcFrame; }
	const cv::Mat& intermediateFrame() const { return interFrame; }

private:
	int preprocess; // 0 - no preprocess (frame is gray)
	                // 1 - frame is rgb, grayscaling
	                // 2 - frame needs bayerFilter
	bool showIntermediateFrame;

	std::unique_ptr<FrameGrabber> grabber;
	cv::Mat srcFrame;
	cv::Mat dstFrame;
	cv::Mat interFrame;

	EBayerFilter bayer;
	cv::BackgroundSubtractorMOG mog;

	ConfigFile& cfg;
	float learningRate;

private:
	WorkerCPU(const WorkerCPU&);
	WorkerCPU& operator=(const WorkerCPU&);
};

class Worker
{
public:
	Worker(const clw::Context& context,
		const clw::Device& device, 
		const clw::CommandQueue& queue,
		ConfigFile& cfg)
		: context(context)
		, device(device)
		, queue(queue)
		, inputFrameSize(0)
		, showIntermediateFrame(false)
		, mogGPU(context, device, queue)
		, grayscaleGPU(context, device, queue)
		, bayerFilterGPU(context, device, queue)
		, cfg(cfg)
	{
	}

	bool init(const std::string& videoStream)
	{
		learningRate = std::stof(cfg.value("LearningRate", "MogParameters"));
		const int nmixtures = std::stoi(cfg.value("NumMixtures", "MogParameters"));
		if(nmixtures <= 0)
		{
			std::cerr << "Parameter NumMixtures is wrong, must be more than 0\n";
			return false;
		}

		int workGroupSizeX = std::stoi(cfg.value("X", "WorkGroupSize"));
		int workGroupSizeY = std::stoi(cfg.value("Y", "WorkGroupSize"));

		if(workGroupSizeX <= 0 || workGroupSizeY <= 0)
		{
			std::cerr << "Parameter X or Y in WorkGroupSize is wrong, must be more than 0\n";
			return false;
		}

		// Inicjalizuj frame grabbera

#if defined(SAPERA_SUPPORT)
		// Sprawdz suffix videoStream (.ccf)
		size_t pos = videoStream.find_last_of(".ccf");
		if(pos+1 == videoStream.length())
		{
			grabber = std::unique_ptr<FrameGrabber>(new SaperaFrameGrabber());
		}
		else
#endif
		{
			grabber = std::unique_ptr<FrameGrabber>(new OpenCvFrameGrabber());
		}
		
		if(!grabber->init(videoStream))
			return false;
		dstFrame = cv::Mat(grabber->frameHeight(), grabber->frameWidth(), CV_8UC1);

		// Pobierz dane o formacie ramki
		int width = grabber->frameWidth();
		int height = grabber->frameHeight();
		int channels = grabber->frameNumChannels();

		// Initialize MoG on GPU
		mogGPU.setMixtureParameters(200,
			std::stof(cfg.value("VarianceThreshold", "MogParameters")),
			std::stof(cfg.value("BackgroundRatio", "MogParameters")),
			std::stof(cfg.value("InitialWeight", "MogParameters")),
			std::stof(cfg.value("InitialVariance", "MogParameters")),
			std::stof(cfg.value("MinVariance", "MogParameters")));
		mogGPU.init(width, height, workGroupSizeX, workGroupSizeY, nmixtures);

		std::cout << "\n  frame width: " << width <<
			"\n  frame height: " << height << 
			"\n  num channels: " << channels << "x" << grabber->framePixelDepth() << " bits \n";
		inputFrameSize = width * height * channels * sizeof(cl_uchar);

		if(channels == 3)
		{
			std::cout << "  preprocessing frame: grayscalling\n";

			// Initialize Grayscaling on GPU
			grayscaleGPU.init(width, height, workGroupSizeX, workGroupSizeY);
			preprocess = 1;

			clFrame = context.createBuffer
				(clw::Access_ReadOnly, clw::Location_Device, inputFrameSize);
		}
		else if(channels == 1 && grabber->needBayer())
		{
			std::string bayerCfg = cfg.value("Bayer", "General");
			EBayerFilter bayer;
			if(bayerCfg == "RG") bayer = Bayer_RG;
			else if(bayerCfg == "BG") bayer = Bayer_BG;
			else if(bayerCfg == "GR") bayer = Bayer_GR;
			else if(bayerCfg == "GB") bayer = Bayer_GB;
			else
			{
				std::cerr << "Unknown 'Bayer' parameter (must be RG, BG, GR or GB)";
				return false;
			}
			std::cout << "  preprocessing frame: bayer " << bayerCfg << "\n";

			bayerFilterGPU.init(width, height, workGroupSizeX, workGroupSizeX, bayer);
			preprocess = 2;

			clFrame = context.createBuffer
				(clw::Access_ReadOnly, clw::Location_Device, inputFrameSize);
		}
		else
		{
			std::cout << "  preprocessing frame: none (already monochrome format)\n";
			preprocess = 0;
			clFrameGray = context.createImage2D(
				clw::Access_ReadOnly, clw::Location_Device,
				clw::ImageFormat(clw::Order_R, clw::Type_Normalized_UInt8), width, height);
		}

		showIntermediateFrame = cfg.value("ShowIntermediateFrame", "General") == "yes";
		if(showIntermediateFrame)
			interFrame = cv::Mat(height, width, CV_8UC1);

		return true;
	}

	clw::Event processFrame()
	{
		clw::Image2D sourceMogFrame;

		// Grayscaling
		if(preprocess == 1)
		{
			clw::Event e0 = queue.asyncWriteBuffer(clFrame, srcFrame.data, 0, inputFrameSize);
			clw::Event e1 = grayscaleGPU.process(clFrame);
			sourceMogFrame = grayscaleGPU.output();
		}
		// Bayer filter
		else if(preprocess == 2)
		{
			clw::Event e0 = queue.asyncWriteBuffer(clFrame, srcFrame.data, 0, inputFrameSize);
			clw::Event e1 = bayerFilterGPU.process(clFrame);
			sourceMogFrame = bayerFilterGPU.output();
		}
		// Passthrough
		else
		{
			clw::Event e0 = queue.asyncWriteImage2D(clFrameGray, srcFrame.data, 0, 0, srcFrame.cols, srcFrame.rows);
			sourceMogFrame= clFrameGray;
		}

		if(showIntermediateFrame && preprocess != 0)
		{
			queue.asyncReadImage2D(sourceMogFrame, interFrame.data, 0, 0, interFrame.cols, interFrame.rows);
		}
		
		clw::Event e2 = mogGPU.process(sourceMogFrame, learningRate);
		clw::Event e3 = queue.asyncReadImage2D(mogGPU.output(), dstFrame.data, 0, 0, dstFrame.cols, dstFrame.rows);
		return e3;
	}

	bool grabFrame()
	{
		bool success;
		srcFrame = grabber->grab(&success);
		return success;
	}

	const cv::Mat& finalFrame() const { return dstFrame; }
	const cv::Mat& sourceFrame() const { return srcFrame; }
	const cv::Mat& intermediateFrame() const { return interFrame; }

private:
	clw::Context context;
	clw::Device device;
	clw::CommandQueue queue;
	clw::Buffer clFrame;
	clw::Image2D clFrameGray;
	int inputFrameSize;
	int preprocess; // 0 - no preprocess (frame is gray)
	                // 1 - frame is rgb, grayscaling
	                // 2 - frame needs bayerFilter
	bool showIntermediateFrame;

	MixtureOfGaussianGPU mogGPU;
	GrayscaleGPU grayscaleGPU;
	BayerFilterGPU bayerFilterGPU;

	std::unique_ptr<FrameGrabber> grabber;
	cv::Mat srcFrame;
	cv::Mat dstFrame;
	cv::Mat interFrame;

	ConfigFile& cfg;
	float learningRate;

private:
	Worker(const Worker&);
	Worker& operator=(const Worker&);
};

int main(int, char**)
{
	ConfigFile cfg;
	if(!cfg.load("mixture-of-gaussian.cfg"))
	{
		std::cerr << "Can't load mixture-of-gaussian.cfg, qutting\n";
		std::cin.get();
		exit(-1);
	}
	std::string devpick = cfg.value("Device", "General");

#if 0

	// Initialize OpenCL
	clw::Context context;

	if(devpick == "pick")
	{
		if(!context.create(clwutils::pickSingleDevice()))
		{
			std::cerr << "Couldn't create context, quitting\n";
			std::exit(-1);
		}
	}
	else
	{
		clw::EDeviceType type = clw::Default;
		if(devpick == "gpu")
			type = clw::Gpu;
		else if(devpick == "cpu")
			type = clw::Cpu;
		if(!context.create(type))
		{
			std::cerr << "Couldn't create context, quitting\n";
			std::exit(-1);
		}
	}
	clw::Device device = context.devices()[0];
	clw::CommandQueue queue = context.createCommandQueue
		(clw::Property_ProfilingEnabled, device);

	int numVideoStreams = 0;

	std::vector<bool> finish;
	std::vector<std::unique_ptr<Worker>> workers;
	std::vector<std::string> titles;

	for(int streamId = 1; streamId <= 5; ++streamId)
	{
		std::string cfgVideoStream = "VideoStream";
		cfgVideoStream += streamId + '0';
		std::string videoStream = cfg.value(cfgVideoStream, "General");

		if(!videoStream.empty())
		{
			auto worker = std::unique_ptr<Worker>(new Worker(context ,device, queue, cfg));
			if(!worker->init(videoStream))
				continue;

			workers.emplace_back(std::move(worker));
			titles.emplace_back(std::move(videoStream));
			finish.push_back(false);

			++numVideoStreams;
		}
	}

	if(numVideoStreams < 1)
	{
		std::wcout << "No video stream to process\n";
		std::cin.get();
		std::exit(-1);
	}

	QPCTimer timer;

	int frameInterval = std::stoi(cfg.value("FrameInterval", "General"));
	frameInterval = std::min(std::max(frameInterval, 1), 100);
	bool showSourceFrame = cfg.value("ShowSourceFrame", "General") == "yes";
	bool showIntermediateFrame = cfg.value("ShowIntermediateFrame", "General") == "yes";
	std::cout << "\n";

	double start = timer.currentTime();

	for(;;)
	{
		double oldStart = start;
		start = timer.currentTime();

		std::cout << "Time between consecutive frames: " << (start - oldStart) * 1000.0 << " ms\n";

		// Grab a new frame
		for(int i = 0; i < numVideoStreams; ++i)
			finish[i] = workers[i]->grabFrame();

		bool allFinish = false;
		for(int i = 0; i < numVideoStreams; ++i)
			allFinish = allFinish || finish[i];
		if(!allFinish)
			break;

		start = timer.currentTime();

		for(int i = 0; i < numVideoStreams; ++i)
		{
			if(finish[i])
			{
				workers[i]->processFrame();
				queue.flush();
			}
		}
		queue.finish();

		double stop = timer.currentTime();

		std::cout << "Total processing and transfer time: " << (stop - start) * 1000.0 << " ms\n\n";

		for(int i = 0; i < numVideoStreams; ++i)
		{
			cv::imshow(titles[i], workers[i]->finalFrame());
			if(showSourceFrame)
				cv::imshow(titles[i] + " source", workers[i]->sourceFrame());
			if(showIntermediateFrame)
				cv::imshow(titles[i] + " intermediate frame", workers[i]->intermediateFrame());
		}

		int time = int((stop - start) * 1000);
		int delay = std::max(1, frameInterval - time);

		int key = cv::waitKey(delay);
		if(key >= 0)
			break;
	}
#else
	int numVideoStreams = 0;

	std::vector<bool> finish;
	std::vector<std::unique_ptr<WorkerCPU>> workers;
	std::vector<std::string> titles;

	for(int streamId = 1; streamId <= 5; ++streamId)
	{
		std::string cfgVideoStream = "VideoStream";
		cfgVideoStream += streamId + '0';
		std::string videoStream = cfg.value(cfgVideoStream, "General");

		if(!videoStream.empty())
		{
			auto worker = std::unique_ptr<WorkerCPU>(new WorkerCPU(cfg));
			if(!worker->init(videoStream))
				continue;

			workers.emplace_back(std::move(worker));
			titles.emplace_back(std::move(videoStream));
			finish.push_back(false);

			++numVideoStreams;
		}
	}

	if(numVideoStreams < 1)
	{
		std::wcout << "No video stream to process\n";
		std::cin.get();
		std::exit(-1);
	}

	QPCTimer timer;

	int frameInterval = std::stoi(cfg.value("FrameInterval", "General"));
	frameInterval = std::min(std::max(frameInterval, 1), 100);
	bool showSourceFrame = cfg.value("ShowSourceFrame", "General") == "yes";
	bool showIntermediateFrame = cfg.value("ShowIntermediateFrame", "General") == "yes";
	std::cout << "\n";

	double start = timer.currentTime();

	for(;;)
	{
		double oldStart = start;
		start = timer.currentTime();

		std::cout << "Time between consecutive frames: " << (start - oldStart) * 1000.0 << " ms\n";

		// Grab a new frame
		for(int i = 0; i < numVideoStreams; ++i)
			finish[i] = workers[i]->grabFrame();

		bool allFinish = false;
		for(int i = 0; i < numVideoStreams; ++i)
			allFinish = allFinish || finish[i];
		if(!allFinish)
			break;

		start = timer.currentTime();

		for(int i = 0; i < numVideoStreams; ++i)
		{
			if(finish[i])
			{
				workers[i]->processFrame();
			}
		}

		double stop = timer.currentTime();

		std::cout << "Total processing and transfer time: " << (stop - start) * 1000.0 << " ms\n\n";

		for(int i = 0; i < numVideoStreams; ++i)
		{
			cv::imshow(titles[i], workers[i]->finalFrame());
			if(showSourceFrame)
				cv::imshow(titles[i] + " source", workers[i]->sourceFrame());
			if(showIntermediateFrame)
				cv::imshow(titles[i] + " intermediate frame", workers[i]->intermediateFrame());
		}

		int time = int((stop - start) * 1000);
		int delay = std::max(1, frameInterval - time);

		int key = cv::waitKey(delay);
		if(key >= 0)
			break;
	}
#endif
}
