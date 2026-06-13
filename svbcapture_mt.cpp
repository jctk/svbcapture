// SVBDemo.cpp
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <climits>
#include <csignal>
#include <cctype>
#include <cstdarg>
#include <ctime>
#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>

#include "SVBCameraSDK.h"

#include <assert.h>

// Helper function to convert control type enum to string for better readability in logs
const char* GetControlTypeName(SVB_CONTROL_TYPE type)
{
	switch(type)
	{
		case SVB_GAIN: return "SVB_GAIN";
		case SVB_EXPOSURE: return "SVB_EXPOSURE";
		case SVB_GAMMA: return "SVB_GAMMA";
		case SVB_GAMMA_CONTRAST: return "SVB_GAMMA_CONTRAST";
		case SVB_WB_R: return "SVB_WB_R";
		case SVB_WB_G: return "SVB_WB_G";
		case SVB_WB_B: return "SVB_WB_B";
		case SVB_FLIP: return "SVB_FLIP";
		case SVB_FRAME_SPEED_MODE: return "SVB_FRAME_SPEED_MODE";
		case SVB_CONTRAST: return "SVB_CONTRAST";
		case SVB_SHARPNESS: return "SVB_SHARPNESS";
		case SVB_SATURATION: return "SVB_SATURATION";
		case SVB_AUTO_TARGET_BRIGHTNESS: return "SVB_AUTO_TARGET_BRIGHTNESS";
		case SVB_BLACK_LEVEL: return "SVB_BLACK_LEVEL";
		case SVB_COOLER_ENABLE: return "SVB_COOLER_ENABLE";
		case SVB_TARGET_TEMPERATURE: return "SVB_TARGET_TEMPERATURE";
		case SVB_CURRENT_TEMPERATURE: return "SVB_CURRENT_TEMPERATURE";
		case SVB_COOLER_POWER: return "SVB_COOLER_POWER";
		case SVB_BAD_PIXEL_CORRECTION_ENABLE: return "SVB_BAD_PIXEL_CORRECTION_ENABLE";
		case SVB_BAD_PIXEL_CORRECTION_THRESHOLD: return "SVB_BAD_PIXEL_CORRECTION_THRESHOLD";
		default: return "UNKNOWN";
	}
}

// Global flag to control the main loop, set to 0 when SIGINT is received
static volatile sig_atomic_t g_keepRunning = 1;
static std::mutex g_logMutex;

static void Logf(int cameraIndex, const char* format, ...)
{
	std::lock_guard<std::mutex> lock(g_logMutex);
	printf("[camera %d] ", cameraIndex);

	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	fflush(stdout);
}

// Signal handler for SIGINT to allow graceful shutdown
static void HandleSignal(int sig)
{
	if (sig == SIGINT)
	{
		g_keepRunning = 0;
	}
}

class CameraRunOptions
{
public:
	int cameraIndex;
	SVB_CAMERA_MODE cameraMode;
	double exposureSeconds;
	int gainValue;
	int offsetValue;

	CameraRunOptions()
		: cameraIndex(-1)
		, cameraMode(SVB_MODE_NORMAL)
		, exposureSeconds(0.5)
		, gainValue(100)
		, offsetValue(20)
	{
	}
};

class CameraPreviewState
{
public:
	std::string windowName;
	std::atomic<int> desiredGainValue;
	std::atomic<int> maxGainValue;
	std::atomic<long> desiredExposureMSeconds;
	std::atomic<bool> isFinished;
	std::mutex frameMutex;
	cv::Mat latestPreviewFrame;
	bool hasFrame;

	CameraPreviewState(const CameraRunOptions& options, const std::string& name)
		: windowName(name)
		, desiredGainValue(options.gainValue)
		, maxGainValue(500)
		, desiredExposureMSeconds(static_cast<long>(options.exposureSeconds * 1000.0))
		, isFinished(false)
		, hasFrame(false)
	{
	}
};

/*
 * parse command line arguments
 * --camera <index,mode,exp,gain,offset>: per-camera settings (repeatable)
 * mode: video or image
 * exp: exposure time in seconds
 * gain: 0-500
 * offset: 0-100
 * example: svbcapture_mt --camera 0,video,0.5,100,20 --camera 1,image,1.2,180,10
*/
class CommandLineOptions
{
public:
	std::vector<CameraRunOptions> cameras;

	static CommandLineOptions Parse(int argc, char* argv[])
	{
		CommandLineOptions options;

		for (int i = 1; i < argc; i++)
		{
			if (strcmp(argv[i], "--camera") == 0)
			{
				if (i + 1 >= argc)
				{
					throw std::runtime_error("missing value for --camera");
				}

				CameraRunOptions cameraOptions = ParseCameraSpec(argv[++i]);
				for (size_t j = 0; j < options.cameras.size(); j++)
				{
					if (options.cameras[j].cameraIndex == cameraOptions.cameraIndex)
					{
						throw std::runtime_error(std::string("duplicate camera index in --camera: ") + argv[i]);
					}
				}

				options.cameras.push_back(cameraOptions);
			}
			else
			{
				throw std::runtime_error(std::string("unknown option: ") + argv[i]);
			}
		}

		if (options.cameras.empty())
		{
			throw std::runtime_error(std::string("missing required option --camera\nusage: ") + argv[0] +
				" --camera <index,mode,exp,gain,offset> [--camera <index,mode,exp,gain,offset> ...]");
		}

		return options;
	}

private:
	static std::string Trim(const std::string& input)
	{
		size_t begin = 0;
		while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin])))
		{
			begin++;
		}

		size_t end = input.size();
		while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1])))
		{
			end--;
		}

		return input.substr(begin, end - begin);
	}

	static std::vector<std::string> SplitCommaSeparated(const std::string& input)
	{
		std::vector<std::string> tokens;
		size_t start = 0;

		while (start <= input.size())
		{
			const size_t commaPos = input.find(',', start);
			if (commaPos == std::string::npos)
			{
				tokens.push_back(Trim(input.substr(start)));
				break;
			}

			tokens.push_back(Trim(input.substr(start, commaPos - start)));
			start = commaPos + 1;
		}

		return tokens;
	}

	static CameraRunOptions ParseCameraSpec(const char* rawSpec)
	{
		const std::string spec = rawSpec;
		const std::vector<std::string> fields = SplitCommaSeparated(spec);
		if (fields.size() != 5)
		{
			throw std::runtime_error(std::string("invalid --camera format: ") + rawSpec + " (expected index,mode,exp,gain,offset)");
		}

		CameraRunOptions options;

		char* endPtr = NULL;
		long parsedCameraIndex = strtol(fields[0].c_str(), &endPtr, 10);
		if (endPtr == fields[0].c_str() || *endPtr != '\0' || parsedCameraIndex < 0 || parsedCameraIndex > INT_MAX)
		{
			throw std::runtime_error(std::string("invalid camera index in --camera: ") + rawSpec + " (must be a non-negative integer)");
		}
		options.cameraIndex = static_cast<int>(parsedCameraIndex);

		if (fields[1] == "video")
		{
			options.cameraMode = SVB_MODE_NORMAL;
		}
		else if (fields[1] == "image")
		{
			options.cameraMode = SVB_MODE_TRIG_SOFT;
		}
		else
		{
			throw std::runtime_error(std::string("invalid camera mode in --camera: ") + rawSpec + " (use video or image)");
		}

		endPtr = NULL;
		double parsedExposure = strtod(fields[2].c_str(), &endPtr);
		if (endPtr == fields[2].c_str() || *endPtr != '\0' || parsedExposure <= 0.0)
		{
			throw std::runtime_error(std::string("invalid exposure in --camera: ") + rawSpec + " (exp must be a number greater than 0)");
		}
		options.exposureSeconds = parsedExposure;

		endPtr = NULL;
		long parsedGain = strtol(fields[3].c_str(), &endPtr, 10);
		if (endPtr == fields[3].c_str() || *endPtr != '\0' || parsedGain < 0 || parsedGain > 500)
		{
			throw std::runtime_error(std::string("invalid gain in --camera: ") + rawSpec + " (gain must be an integer between 0 and 500)");
		}
		options.gainValue = static_cast<int>(parsedGain);

		endPtr = NULL;
		long parsedOffset = strtol(fields[4].c_str(), &endPtr, 10);
		if (endPtr == fields[4].c_str() || *endPtr != '\0' || parsedOffset < 0 || parsedOffset > 100)
		{
			throw std::runtime_error(std::string("invalid offset in --camera: ") + rawSpec + " (offset must be an integer between 0 and 100)");
		}
		options.offsetValue = static_cast<int>(parsedOffset);

		return options;
	}
};

// Class to hold camera information and control capabilities, and provide helper functions to load and access them
class CameraProfile
{
public:
	SVB_CAMERA_INFO cameraInfo;
	std::vector<SVB_CONTROL_CAPS> controlCapsList;

	CameraProfile()
		: controlCapsIndexByType(SVB_BAD_PIXEL_CORRECTION_THRESHOLD + 1, -1)
	{
		memset(&cameraInfo, 0, sizeof(cameraInfo));
	}

	static CameraProfile Load(int cameraIndex)
	{
		CameraProfile profile;
		SVB_ERROR_CODE ret = SVBGetCameraInfo(&profile.cameraInfo, cameraIndex);
		if (ret != SVB_SUCCESS)
		{
			throw std::runtime_error("get camera info failed.");
		}

		return profile;
	}

	int GetCameraID() const
	{
		return cameraInfo.CameraID;
	}

	std::string GetFriendlyName() const
	{
		return cameraInfo.FriendlyName;
	}

	int LoadControlCaps(int cameraIndex)
	{
		SVB_ERROR_CODE ret;
		int numOfControls = 0;
		ret = SVBGetNumOfControls(GetCameraID(), &numOfControls);
		if (ret != SVB_SUCCESS)
		{
			Logf(cameraIndex, "get number of controls failed.\n");
			return -1;
		}

		Logf(cameraIndex, "number of controls: %d\n", numOfControls);
		controlCapsList.clear();
		controlCapsList.reserve(numOfControls);
		std::fill(controlCapsIndexByType.begin(), controlCapsIndexByType.end(), -1);

		for (int i = 0; i < numOfControls; i++)
		{
			SVB_CONTROL_CAPS caps;
			ret = SVBGetControlCaps(GetCameraID(), i, &caps);
			if (ret != SVB_SUCCESS)
			{
				Logf(cameraIndex, "get control caps failed.\n");
				continue;
			}

			const int storedIndex = static_cast<int>(controlCapsList.size());
			controlCapsList.push_back(caps);
			if (caps.ControlType >= 0 && caps.ControlType < static_cast<int>(controlCapsIndexByType.size()))
			{
				controlCapsIndexByType[caps.ControlType] = storedIndex;
			}
		}

		return 0;
	}

	const SVB_CONTROL_CAPS* FindControlCaps(SVB_CONTROL_TYPE controlType) const
	{
		if (controlType < 0 || controlType >= static_cast<int>(controlCapsIndexByType.size()))
		{
			return NULL;
		}

		const int index = controlCapsIndexByType[controlType];
		if (index < 0 || index >= static_cast<int>(controlCapsList.size()))
		{
			return NULL;
		}

		return &controlCapsList[index];
	}

private:
	std::vector<int> controlCapsIndexByType;
};

// Helper function to determine bytes per pixel based on the image type
static size_t BytesPerPixelFromImageType(SVB_IMG_TYPE imgType)
{
	switch (imgType)
	{
		case SVB_IMG_RAW8:
		case SVB_IMG_Y8:
			return 1;
		case SVB_IMG_RAW10:
		case SVB_IMG_RAW12:
		case SVB_IMG_RAW14:
		case SVB_IMG_RAW16:
		case SVB_IMG_Y10:
		case SVB_IMG_Y12:
		case SVB_IMG_Y14:
		case SVB_IMG_Y16:
			return 2;
		case SVB_IMG_RGB24:
			return 3;
		case SVB_IMG_RGB32:
			return 4;
		default:
			return 1;
	}
}

// Helper function to print frame information with a timestamp for better debugging and performance monitoring
static void PrintTimestampedFrameInfo(int cameraIndex, const char* label, size_t frameSize)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	time_t now = ts.tv_sec;
	long msec = ts.tv_nsec / 1000000;
	struct tm localTm;
	char timestamp[32] = {0};
	localtime_r(&now, &localTm);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &localTm);
	Logf(cameraIndex, "[%s.%03ld] %s: %zu bytes\n", timestamp, msec, label, frameSize);
}

static int PrintCameraProperty(int cameraIndex, int cameraID, SVB_CAMERA_PROPERTY& cameraProperty)
{
	SVB_ERROR_CODE ret = SVBGetCameraProperty(cameraID, &cameraProperty);
	assert(ret == SVB_SUCCESS);

	Logf(cameraIndex, "Camera Property:\n");
	Logf(cameraIndex, "Max width: %ld\n", cameraProperty.MaxWidth);
	Logf(cameraIndex, "Max height: %ld\n", cameraProperty.MaxHeight);
	Logf(cameraIndex, "Is color camera: %s\n", cameraProperty.IsColorCam ? "YES" : "NO");
	Logf(cameraIndex, "Bayer pattern: %d\n", cameraProperty.BayerPattern);

	{
		std::lock_guard<std::mutex> lock(g_logMutex);
		printf("[camera %d] Supported bins: ", cameraIndex);
		for (int i = 0; i < 16 && cameraProperty.SupportedBins[i] != 0; i++)
		{
			printf("%d ", cameraProperty.SupportedBins[i]);
		}
		printf("\n");
	}

	{
		std::lock_guard<std::mutex> lock(g_logMutex);
		printf("[camera %d] Supported video formats: ", cameraIndex);
		for (int i = 0; i < 8 && cameraProperty.SupportedVideoFormat[i] != SVB_IMG_TYPE(0); i++)
		{
			printf("%d ", cameraProperty.SupportedVideoFormat[i]);
		}
		printf("\n");
	}

	Logf(cameraIndex, "Max bit depth: %d\n", cameraProperty.MaxBitDepth);
	Logf(cameraIndex, "Is trigger camera: %s\n", cameraProperty.IsTriggerCam ? "YES" : "NO");
	return 0;
}

// Helper function to print control capabilities for all controls of the camera profile.
static int PrintControlCapabilities(int cameraIndex, const CameraProfile& cameraProfile)
{
	for (size_t i = 0; i < cameraProfile.controlCapsList.size(); i++)
	{
		const SVB_CONTROL_CAPS& caps = cameraProfile.controlCapsList[i];
		Logf(
			cameraIndex,
			"control index: %d, type: %d (%s), name: %s, desc: %s, min/max/default: %ld/%ld/%ld, auto: %s, writable: %s\n",
			static_cast<int>(i),
			caps.ControlType,
			GetControlTypeName((SVB_CONTROL_TYPE)caps.ControlType),
			caps.Name,
			caps.Description,
			caps.MinValue,
			caps.MaxValue,
			caps.DefaultValue,
			caps.IsAutoSupported ? "YES" : "NO",
			caps.IsWritable ? "YES" : "NO");
	}

	if (!cameraProfile.controlCapsList.empty())
	{
		const SVB_CONTROL_CAPS& firstControl = cameraProfile.controlCapsList[0];
		Logf(cameraIndex, "Stored controls: %zu\n", cameraProfile.controlCapsList.size());
		Logf(cameraIndex, "First stored control name: %s\n", firstControl.Name);
	}

	return 0;
}

// Helper function to configure the camera for capture based on the camera properties and command line options
static int ConfigureCaptureSession(
	int cameraID,
	int cameraIndex,
	const SVB_CAMERA_PROPERTY& cameraProperty,
	const CameraRunOptions& options,
	SVB_IMG_TYPE& outputImageType,
	std::vector<unsigned char>& frameBuffer)
{
	SVB_ERROR_CODE ret;

	ret = SVBSetROIFormat(cameraID, 0, 0, cameraProperty.MaxWidth, cameraProperty.MaxHeight, 1);
	assert(ret == SVB_SUCCESS);

	// set output image type, if the camera mode is normal mode, we will set it to RGB24 for color camera and Y16 for mono camera; if the camera mode is trigger mode, we will set it to RGB24 for color camera and Y16 for mono camera.
	outputImageType =
		(options.cameraMode == SVB_MODE_NORMAL) ? SVB_IMG_RGB24 :
		(cameraProperty.IsColorCam ? SVB_IMG_RGB24 : SVB_IMG_Y16);
	
	ret = SVBSetOutputImageType(cameraID, outputImageType);
	assert(ret == SVB_SUCCESS);

	const size_t frameBytesPerPixel = BytesPerPixelFromImageType(outputImageType);

	const size_t frameBufferSize = (
		cameraProperty.MaxBitDepth + 7) / 8 * cameraProperty.MaxWidth * cameraProperty.MaxHeight * frameBytesPerPixel;

	Logf(cameraIndex, "frame buffer size: %zu bytes (roi %ldx%ld, %zu bytes/pixel)\n",
		frameBufferSize,
		cameraProperty.MaxWidth,
		cameraProperty.MaxHeight,
		frameBytesPerPixel);

	frameBuffer.assign(frameBufferSize, 0); // Keep a large capture buffer on the heap to avoid stack overflow.

	Logf(cameraIndex, "cameraMode: %d\n", options.cameraMode);
	ret = SVBSetCameraMode(cameraID, options.cameraMode);
	assert(ret == SVB_SUCCESS);

	// start video capture
	ret = SVBStartVideoCapture(cameraID);
	assert(ret == SVB_SUCCESS);

	// fix for SDK gain error issue
	// set exposure time
	SVBSetControlValue(cameraID, SVB_EXPOSURE, static_cast<long>(1 * 1000000L), SVB_FALSE);

	ret = SVBSetControlValue(cameraID, SVB_BAD_PIXEL_CORRECTION_ENABLE, 0, SVB_FALSE);
	assert(ret == SVB_SUCCESS);

	ret = SVBSetControlValue(cameraID, SVB_GAIN, options.gainValue, SVB_FALSE);
	assert(ret == SVB_SUCCESS);

	ret = SVBSetControlValue(cameraID, SVB_BLACK_LEVEL, options.offsetValue, SVB_FALSE);
	assert(ret == SVB_SUCCESS);

	ret = SVBSetControlValue(cameraID, SVB_EXPOSURE, options.exposureSeconds*1000L*1000L, SVB_FALSE);
	assert(ret == SVB_SUCCESS);

	ret = SVBWhiteBalanceOnce(cameraID);
	assert(ret == SVB_SUCCESS);

	return 0;
}

// Main loop to continuously capture frames from the camera, display them, and handle user input for quitting the application
static int RunPreviewLoop(
	int cameraID,
	int cameraIndex,
	const SVB_CAMERA_PROPERTY& cameraProperty,
	const CameraProfile& cameraProfile,
	const CameraRunOptions& options,
	SVB_IMG_TYPE outputImageType,
	CameraPreviewState& previewState,
	std::vector<unsigned char>& frameBuffer)
{
	SVB_ERROR_CODE ret;
	(void)cameraProfile;
	int appliedGainValue = -1;
	double appliedExposureMSeconds = -1.0;

	const char* prompt = (options.cameraMode == SVB_MODE_NORMAL) ? "Video" : "Image";

	const int cvType = (outputImageType == SVB_IMG_Y16) ? CV_16UC1 : CV_8UC3;

	cv::Mat frame(static_cast<int>(cameraProperty.MaxHeight), static_cast<int>(cameraProperty.MaxWidth), cvType, frameBuffer.data());
	cv::Mat previewFrame;

	const int maxBitDepth = std::min<int>(cameraProperty.MaxBitDepth, 16);
	const double maxRawValue = (maxBitDepth > 0) ? (static_cast<double>((1 << maxBitDepth) - 1)) : 65535.0;

	while (g_keepRunning)
	{
		const int currentGainValue = previewState.desiredGainValue.load();
		long currentExposureMSeconds = previewState.desiredExposureMSeconds.load();
		if (currentExposureMSeconds < 1)
		{
			currentExposureMSeconds = 1;
		}

		if (currentGainValue != appliedGainValue)
		{
			ret = SVBSetControlValue(cameraID, SVB_GAIN, currentGainValue, SVB_FALSE);
			assert(ret == SVB_SUCCESS);
			appliedGainValue = currentGainValue;
		}
		if (currentExposureMSeconds != appliedExposureMSeconds)
		{
			ret = SVBSetControlValue(cameraID, SVB_EXPOSURE, static_cast<long>(currentExposureMSeconds*1000L), SVB_FALSE);
			assert(ret == SVB_SUCCESS);
			appliedExposureMSeconds = currentExposureMSeconds;
		}

		if (options.cameraMode == SVB_MODE_TRIG_SOFT)
		{
			ret = SVBSendSoftTrigger(cameraID);
			assert(ret == SVB_SUCCESS);
		}

		ret = SVBGetVideoData(cameraID, frameBuffer.data(), static_cast<long>(frameBuffer.size()), static_cast<int>(currentExposureMSeconds*2+500L));
		if (ret == SVB_SUCCESS)
		{
			PrintTimestampedFrameInfo(cameraIndex, prompt, frameBuffer.size());

			if (frame.depth() == CV_16U)
			{
				frame.convertTo(previewFrame, CV_8U, 255.0 / maxRawValue);
			}
			else
			{
				previewFrame = frame;
			}

			{
				std::lock_guard<std::mutex> frameLock(previewState.frameMutex);
				previewState.latestPreviewFrame = previewFrame.clone();
				previewState.hasFrame = true;
			}
		}
		else // if (ret != SVB_ERROR_TIMEOUT)
		{
			Logf(cameraIndex, "SVBGetVideoData failed: %d\n", ret);
		}
	}

	return 0;
}

// Main function to initialize the camera, set parameters, and start the capture loop
static int RunCaptureLoop(const CameraRunOptions& options, CameraPreviewState& previewState)
{
	CameraProfile cameraProfile;
	try
	{
		cameraProfile = CameraProfile::Load(options.cameraIndex);
	}
	catch (const std::exception& ex)
	{
		Logf(options.cameraIndex, "%s\n", ex.what());
		return 1;
	}

	Logf(options.cameraIndex, "Friendly name: %s\n", cameraProfile.cameraInfo.FriendlyName);
	Logf(options.cameraIndex, "Port type: %s\n", cameraProfile.cameraInfo.PortType);
	Logf(options.cameraIndex, "SN: %s\n", cameraProfile.cameraInfo.CameraSN);
	Logf(options.cameraIndex, "Device ID: 0x%x\n", cameraProfile.cameraInfo.DeviceID);
	Logf(options.cameraIndex, "Camera ID: %d\n", cameraProfile.cameraInfo.CameraID);

	const int cameraID = cameraProfile.GetCameraID();
	SVB_ERROR_CODE ret = SVBOpenCamera(cameraID);
	if (ret != SVB_SUCCESS)
	{
		Logf(options.cameraIndex, "open camera failed.\n");
		return -1;
	}

	SVB_CAMERA_PROPERTY cameraProperty;
	if (PrintCameraProperty(options.cameraIndex, cameraID, cameraProperty) != 0)
	{
		SVBCloseCamera(cameraID);
		return -1;
	}

	CameraProfile loadedCameraProfile = cameraProfile;
	if (loadedCameraProfile.LoadControlCaps(options.cameraIndex) != 0)
	{
		SVBCloseCamera(cameraID);
		return -1;
	}

	if (PrintControlCapabilities(options.cameraIndex, loadedCameraProfile) != 0)
	{
		SVBCloseCamera(cameraID);
		return -1;
	}

	const SVB_CONTROL_CAPS* gainCaps = loadedCameraProfile.FindControlCaps(SVB_GAIN);
	if (gainCaps != NULL)
	{
		const int gainMax = static_cast<int>(gainCaps->MaxValue);
		if (gainMax > 0)
		{
			previewState.maxGainValue.store(gainMax);
		}
	}

	SVB_IMG_TYPE outputImageType = SVB_IMG_END;
	std::vector<unsigned char> frameBuffer;
	if (ConfigureCaptureSession(cameraID, options.cameraIndex, cameraProperty, options, outputImageType, frameBuffer) != 0)
	{
		SVBCloseCamera(cameraID);
		return -1;
	}

	const int previewResult = RunPreviewLoop(
		cameraID,
		options.cameraIndex,
		cameraProperty,
		loadedCameraProfile,
		options,
		outputImageType,
		previewState,
		frameBuffer);

	SVBStopVideoCapture(cameraID);

	Logf(options.cameraIndex, "close camera\n");
	SVBCloseCamera(cameraID);
	return previewResult;
}

// Main function to initialize the camera, set parameters, and start the capture loop
int main(int argc, char* argv[])
{
	// Register the signal handler for SIGINT to allow graceful shutdown when Ctrl+C is pressed
	signal(SIGINT, HandleSignal);

	CommandLineOptions options;
	try
	{
		options = CommandLineOptions::Parse(argc, argv);
	}
	catch (const std::exception& ex)
	{
		printf("%s\n", ex.what());
		return 1;
	}

	int cameraNum = SVBGetNumOfConnectedCameras();
	printf("Scan camera number: %d\n\n", cameraNum);

	for (size_t i = 0; i < options.cameras.size(); i++)
	{
		if (options.cameras[i].cameraIndex >= cameraNum)
		{
			printf("camera index out of range: %d (available: 0-%d)\n", options.cameras[i].cameraIndex, cameraNum - 1);
			return 1;
		}
	}

	// Start the main capture and preview loop
	std::vector<int> captureResults(options.cameras.size(), -1);
	std::vector<std::thread> captureThreads;
	std::vector<std::shared_ptr<CameraPreviewState> > previewStates;
	captureThreads.reserve(options.cameras.size());
	previewStates.reserve(options.cameras.size());

	const int exposureTrackbarScale = 10000;
	const char* brightnessTrackbarName = "Brightness";
	const char* contrastTrackbarName = "Contrast";
	const char* gainTrackbarName = "Gain";
	const char* exposureTrackbarName = "Exposure(ms)";
	std::vector<int> appliedGainTrackbarMax(options.cameras.size(), -1);

	for (size_t i = 0; i < options.cameras.size(); i++)
	{
		std::string windowName = std::string("SVB Camera ") + std::to_string(options.cameras[i].cameraIndex);
		try
		{
			const CameraProfile profile = CameraProfile::Load(options.cameras[i].cameraIndex);
			const std::string friendlyName = profile.GetFriendlyName();
			if (!friendlyName.empty())
			{
				windowName += std::string(" - ") + friendlyName;
			}
		}
		catch (const std::exception&)
		{
			// Keep the fallback title when camera info cannot be retrieved here.
		}

		std::shared_ptr<CameraPreviewState> state(new CameraPreviewState(options.cameras[i], windowName));

		cv::namedWindow(state->windowName, cv::WINDOW_NORMAL);
		cv::resizeWindow(state->windowName, 500, 500);
		cv::createTrackbar(brightnessTrackbarName, state->windowName, NULL, 200);
		cv::createTrackbar(contrastTrackbarName, state->windowName, NULL, 200);
		cv::createTrackbar(gainTrackbarName, state->windowName, NULL, state->maxGainValue.load());
		cv::createTrackbar(exposureTrackbarName, state->windowName, NULL, exposureTrackbarScale);
		cv::setTrackbarPos(brightnessTrackbarName, state->windowName, 100);
		cv::setTrackbarPos(contrastTrackbarName, state->windowName, 100);
		cv::setTrackbarPos(gainTrackbarName, state->windowName, state->desiredGainValue.load());

		int initialExposureMSeconds = static_cast<int>(state->desiredExposureMSeconds.load());
		if (initialExposureMSeconds < 1)
		{
			initialExposureMSeconds = 1;
		}
		else if (initialExposureMSeconds > exposureTrackbarScale)
		{
			initialExposureMSeconds = exposureTrackbarScale;
		}
		cv::setTrackbarPos(exposureTrackbarName, state->windowName, initialExposureMSeconds);

		previewStates.push_back(state);
	}

	for (size_t i = 0; i < options.cameras.size(); i++)
	{
		captureThreads.push_back(std::thread([&options, &captureResults, &previewStates, i]() {
			captureResults[i] = RunCaptureLoop(options.cameras[i], *previewStates[i]);
			previewStates[i]->isFinished.store(true);
		}));
	}

	while (g_keepRunning)
	{
		bool allFinished = true;

		for (size_t i = 0; i < previewStates.size(); i++)
		{
			CameraPreviewState& state = *previewStates[i];

			if (!state.isFinished.load())
			{
				allFinished = false;
			}

			double prop = cv::getWindowProperty(state.windowName, cv::WND_PROP_VISIBLE);
			if (prop < 1)
			{
				g_keepRunning = 0;
				break;
			}

			const int currentGainMax = state.maxGainValue.load();
			if (appliedGainTrackbarMax[i] != currentGainMax)
			{
				cv::setTrackbarMax(gainTrackbarName, state.windowName, currentGainMax);
				const int currentGainPos = cv::getTrackbarPos(gainTrackbarName, state.windowName);
				if (currentGainPos > currentGainMax)
				{
					cv::setTrackbarPos(gainTrackbarName, state.windowName, currentGainMax);
				}
				appliedGainTrackbarMax[i] = currentGainMax;
			}

			const int brightness = cv::getTrackbarPos(brightnessTrackbarName, state.windowName);
			const int contrast = cv::getTrackbarPos(contrastTrackbarName, state.windowName);
			const int gainValue = cv::getTrackbarPos(gainTrackbarName, state.windowName);
			long exposureMSeconds = cv::getTrackbarPos(exposureTrackbarName, state.windowName);

			if (exposureMSeconds < 1)
			{
				exposureMSeconds = 1;
			}

			state.desiredGainValue.store(gainValue);
			state.desiredExposureMSeconds.store(exposureMSeconds);

			cv::Mat previewFrame;
			{
				std::lock_guard<std::mutex> frameLock(state.frameMutex);
				if (state.hasFrame)
				{
					previewFrame = state.latestPreviewFrame.clone();
				}
			}

			if (!previewFrame.empty())
			{
				const double alpha = contrast / 100.0;
				const double beta = (brightness - 100) * 2.55;
				cv::Mat adjustedFrame;
				previewFrame.convertTo(adjustedFrame, -1, alpha, beta);
				cv::imshow(state.windowName, adjustedFrame);
			}
		}

		const int key = cv::waitKey(1);
		if (key == 'q')
		{
			g_keepRunning = 0;
		}

		if (allFinished)
		{
			break;
		}
	}

	g_keepRunning = 0;

	for (size_t i = 0; i < captureThreads.size(); i++)
	{
		captureThreads[i].join();
	}

	cv::destroyAllWindows();

	for (size_t i = 0; i < captureResults.size(); i++)
	{
		if (captureResults[i] != 0)
		{
			return captureResults[i];
		}
	}

	return 0;
}


