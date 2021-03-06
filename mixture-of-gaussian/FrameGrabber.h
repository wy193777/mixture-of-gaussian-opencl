#pragma once

#include <opencv2/highgui/highgui.hpp>
#include <string>

class FrameGrabber
{
public:
	virtual bool init(const std::string& stream) = 0;
	virtual void deinit() = 0;
	virtual cv::Mat grab(bool* success) = 0;
	virtual int frameWidth() const = 0;
	virtual int frameHeight() const = 0;
	virtual int frameNumChannels() const = 0;
	virtual int framePixelDepth() const = 0;
	virtual bool needBayer() const = 0;
};

class OpenCvFrameGrabber : public FrameGrabber
{
public:
	OpenCvFrameGrabber();
	virtual ~OpenCvFrameGrabber();

	virtual bool init(const std::string& stream) override;
	virtual void deinit() override;
	virtual cv::Mat grab(bool* success) override;
	virtual int frameWidth() const override;
	virtual int frameHeight() const override;
	virtual int frameNumChannels() const override;
	virtual int framePixelDepth() const override;
	virtual bool needBayer() const override;

private:
	cv::VideoCapture cap;
	int width,
		height,
		pixelDepth,
		numChannels;
};

// Make sure it isn't included in Linux builds
#if defined(SAPERA_SUPPORT) && defined(_WIN32)

// Forward declarations
class SapAcquisition;
class SapBuffer;
class SapAcqToBuf;

class SaperaFrameGrabber : public FrameGrabber
{
public:
	SaperaFrameGrabber();
	virtual ~SaperaFrameGrabber();

	virtual bool init(const std::string& stream) override;
	virtual void deinit() override;
	virtual cv::Mat grab(bool* success) override;
	virtual int frameWidth() const override;
	virtual int frameHeight() const override;
	virtual int frameNumChannels() const override;
	virtual int framePixelDepth() const override;
	virtual bool needBayer() const override;

private:
	SapAcquisition* acq;
	SapBuffer* buffer;
	SapAcqToBuf* xfer;
	IplImage* image; // Can't really use cv::Mat
	char* dummyImageData;
};

#endif