// SVBDemo.cpp
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <climits>
#include <csignal>
#include <ctime>
#include <stdexcept>
#include <string>
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

// Signal handler for SIGINT to allow graceful shutdown
static void HandleSignal(int sig)
{
	if (sig == SIGINT)
	{
		g_keepRunning = 0;
	}
}

/*
 * parse command line arguments
 * --camera-index <index>: specify which camera to use (default: 0)
 * --camera-mode <normal|trigger>: camera mode (default: normal)
 * --exp <seconds>: exposure time in seconds (default: 0.5)
 * --gain <0-500>: gain value (default: 100)
 * --offset <0-100>: offset value (default: 20)
 * --ratio <0.1-1.0>: display scale factor (default: 0.25)
 * example: svbcontrols --camera-index 1
*/
class CommandLineOptions
{
public:
	int cameraIndex;
	SVB_CAMERA_MODE cameraMode;
	double exposureSeconds;
	int gainValue;
	int offsetValue;
	double displayRatio;

	CommandLineOptions()
		: cameraIndex(-1)
		, cameraMode(SVB_MODE_NORMAL)
		, exposureSeconds(0.5)
		, gainValue(100)
		, offsetValue(20)
		, displayRatio(0.25)
	{
	}

	static CommandLineOptions Parse(int argc, char* argv[])
	{
		CommandLineOptions options;

		for (int i = 1; i < argc; i++)
		{
			if (strcmp(argv[i], "--camera-index") == 0)
			{
				if (i + 1 >= argc)
				{
					throw std::runtime_error("missing value for --camera-index");
				}

				char* endPtr = NULL;
				long parsedCameraIndex = strtol(argv[++i], &endPtr, 10);
				if (endPtr == argv[i] || *endPtr != '\0' || parsedCameraIndex < 0 || parsedCameraIndex > INT_MAX)
				{
					throw std::runtime_error(std::string("invalid --camera-index: ") + argv[i] + " (must be a non-negative integer)");
				}

				options.cameraIndex = static_cast<int>(parsedCameraIndex);
			}
			else if (strcmp(argv[i], "--camera-mode") == 0)
			{
				if (i + 1 >= argc)
				{
					throw std::runtime_error("missing value for --camera-mode");
				}

				const char* mode = argv[++i];
				if (strcmp(mode, "video") == 0)
				{
					options.cameraMode = SVB_MODE_NORMAL;
				}
				else if (strcmp(mode, "image") == 0)
				{
					options.cameraMode = SVB_MODE_TRIG_SOFT;
				}
				else
				{
					throw std::runtime_error(std::string("invalid --camera-mode: ") + mode + " (use video or image)");
				}
			}
			else if (strcmp(argv[i], "--exp") == 0)
			{
				if (i + 1 >= argc)
				{
					throw std::runtime_error("missing value for --exp");
				}

				char* endPtr = NULL;
				double parsedExposure = strtod(argv[++i], &endPtr);
				if (endPtr == argv[i] || *endPtr != '\0' || parsedExposure <= 0.0)
				{
					throw std::runtime_error(std::string("invalid --exp: ") + argv[i] + " (must be a number greater than 0)");
				}

				options.exposureSeconds = parsedExposure;
			}
			else if (strcmp(argv[i], "--gain") == 0)
			{
				if (i + 1 >= argc)
				{
					throw std::runtime_error("missing value for --gain");
				}

				char* endPtr = NULL;
				long parsedGain = strtol(argv[++i], &endPtr, 10);
				if (endPtr == argv[i] || *endPtr != '\0' || parsedGain < 0 || parsedGain > 500)
				{
					throw std::runtime_error(std::string("invalid --gain: ") + argv[i] + " (must be an integer between 0 and 500)");
				}

				options.gainValue = static_cast<int>(parsedGain);
			}
			else if (strcmp(argv[i], "--offset") == 0)
			{
				if (i + 1 >= argc)
				{
					throw std::runtime_error("missing value for --offset");
				}

				char* endPtr = NULL;
				long parsedOffset = strtol(argv[++i], &endPtr, 10);
				if (endPtr == argv[i] || *endPtr != '\0' || parsedOffset < 0 || parsedOffset > 100)
				{
					throw std::runtime_error(std::string("invalid --offset: ") + argv[i] + " (must be an integer between 0 and 100)");
				}

				options.offsetValue = static_cast<int>(parsedOffset);
			}
			else if (strcmp(argv[i], "--ratio") == 0)
			{
				if (i + 1 >= argc)
				{
					throw std::runtime_error("missing value for --ratio");
				}

				char* endPtr = NULL;
				double parsedRatio = strtod(argv[++i], &endPtr);
				if (endPtr == argv[i] || *endPtr != '\0' || parsedRatio < 0.1 || parsedRatio > 1.0)
				{
					throw std::runtime_error(std::string("invalid --ratio: ") + argv[i] + " (must be a number between 0.1 and 1.0)");
				}

				options.displayRatio = parsedRatio;
			}
			else
			{
				throw std::runtime_error(std::string("unknown option: ") + argv[i]);
			}
		}

		if (options.cameraIndex < 0)
		{
			throw std::runtime_error(std::string("missing required option --camera-index\nusage: ") + argv[0] +
				" --camera-index <index> [--camera-mode <video|image>] [--exp <seconds>] [--gain <0-500>] [--offset <0-100>] [--ratio <0.1-1.0>]");
		}

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

	int LoadControlCaps()
	{
		SVB_ERROR_CODE ret;
		int numOfControls = 0;
		ret = SVBGetNumOfControls(GetCameraID(), &numOfControls);
		if (ret != SVB_SUCCESS)
		{
			printf("\nget number of controls failed.\n");
			return -1;
		}

		printf("\nnumber of controls: %d\n", numOfControls);
		controlCapsList.clear();
		controlCapsList.reserve(numOfControls);
		std::fill(controlCapsIndexByType.begin(), controlCapsIndexByType.end(), -1);

		for (int i = 0; i < numOfControls; i++)
		{
			SVB_CONTROL_CAPS caps;
			ret = SVBGetControlCaps(GetCameraID(), i, &caps);
			if (ret != SVB_SUCCESS)
			{
				printf("get control caps failed.\n");
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
static void PrintTimestampedFrameInfo(const char* label, size_t frameSize)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	time_t now = ts.tv_sec;
	long msec = ts.tv_nsec / 1000000;
	struct tm localTm;
	char timestamp[32] = {0};
	localtime_r(&now, &localTm);
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &localTm);
	printf("\r[%s.%03ld] %s: %zu bytes", timestamp, msec, label, frameSize);
	fflush(stdout);
}

static int PrintCameraProperty(int cameraID, SVB_CAMERA_PROPERTY& cameraProperty)
{
	SVB_ERROR_CODE ret = SVBGetCameraProperty(cameraID, &cameraProperty);
	assert(ret == SVB_SUCCESS);

	printf("\nCamera Property:\n");
	printf("Max width: %ld\n", cameraProperty.MaxWidth);
	printf("Max height: %ld\n", cameraProperty.MaxHeight);
	printf("Is color camera: %s\n", cameraProperty.IsColorCam ? "YES" : "NO");
	printf("Bayer pattern: %d\n", cameraProperty.BayerPattern);
	printf("Supported bins: ");
	for (int i = 0; i < 16 && cameraProperty.SupportedBins[i] != 0; i++)
	{
		printf("%d ", cameraProperty.SupportedBins[i]);
	}
	printf("\nSupported video formats: ");
	for (int i = 0; i < 8 && cameraProperty.SupportedVideoFormat[i] != SVB_IMG_TYPE(0); i++)
	{
		printf("%d ", cameraProperty.SupportedVideoFormat[i]);
	}
	printf("\nMax bit depth: %d\n", cameraProperty.MaxBitDepth);
	printf("Is trigger camera: %s\n", cameraProperty.IsTriggerCam ? "YES" : "NO");
	return 0;
}

// Helper function to print control capabilities for all controls of the camera profile.
static int PrintControlCapabilities(const CameraProfile& cameraProfile)
{
	for (size_t i = 0; i < cameraProfile.controlCapsList.size(); i++)
	{
		const SVB_CONTROL_CAPS& caps = cameraProfile.controlCapsList[i];
		printf(
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
		printf("\nStored controls: %zu\n", cameraProfile.controlCapsList.size());
		printf("First stored control name: %s\n", firstControl.Name);
	}

	return 0;
}

// Helper function to configure the camera for capture based on the camera properties and command line options
static int ConfigureCaptureSession(
	int cameraID,
	const SVB_CAMERA_PROPERTY& cameraProperty,
	const CommandLineOptions& options,
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

	printf("frame buffer size: %zu bytes (roi %ldx%ld, %zu bytes/pixel)\n",
		frameBufferSize,
		cameraProperty.MaxWidth,
		cameraProperty.MaxHeight,
		frameBytesPerPixel);

	frameBuffer.assign(frameBufferSize, 0); // Keep a large capture buffer on the heap to avoid stack overflow.

	printf("cameraMode: %d\r\n", options.cameraMode);
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
	const SVB_CAMERA_PROPERTY& cameraProperty,
	const CameraProfile& cameraProfile,
	const CommandLineOptions& options,
	SVB_IMG_TYPE outputImageType,
	std::vector<unsigned char>& frameBuffer)
{
	SVB_ERROR_CODE ret;
	const int exposureTrackbarScale = 10000;
	const SVB_CONTROL_CAPS* gainCaps = cameraProfile.FindControlCaps(SVB_GAIN);
	const SVB_CONTROL_CAPS* exposureCaps = cameraProfile.FindControlCaps(SVB_EXPOSURE);
	int gainTrackbarValue = options.gainValue;
	int exposureTrackbarValue = static_cast<int>(options.exposureSeconds * 1000); // convert seconds to milliseconds for the trackbar
	const int maxGainTrackbarValue = (gainCaps != NULL) ? static_cast<int>(gainCaps->MaxValue) : 500;
	if (exposureTrackbarValue < 1)
	{
		exposureTrackbarValue = 1;
	}
	else if (exposureTrackbarValue > exposureTrackbarScale)
	{
		exposureTrackbarValue = exposureTrackbarScale;
	}
	int appliedGainValue = -1;
	double appliedExposureMSeconds = -1.0;

	const char* prompt = (options.cameraMode == SVB_MODE_NORMAL) ? "Video" : "Image";
	const std::string windowName = cameraProfile.GetFriendlyName().empty() ? "SVB Camera" : cameraProfile.GetFriendlyName();

	cv::namedWindow(windowName, cv::WINDOW_NORMAL);
	cv::resizeWindow(windowName, 500, 500);

	int brightness = 100;
	int contrast = 100;
	cv::createTrackbar("Brightness", windowName, &brightness, 200);
	cv::createTrackbar("Contrast", windowName, &contrast, 200);
	cv::createTrackbar("Gain", windowName, &gainTrackbarValue, maxGainTrackbarValue);
	cv::createTrackbar("Exposure(ms)", windowName, &exposureTrackbarValue, exposureTrackbarScale);

	const int cvType = (outputImageType == SVB_IMG_Y16) ? CV_16UC1 : CV_8UC3;
	const bool isRgb24 = (outputImageType == SVB_IMG_RGB24);

	cv::Mat frame(static_cast<int>(cameraProperty.MaxHeight), static_cast<int>(cameraProperty.MaxWidth), cvType, frameBuffer.data());
	cv::Mat displayFrame;
	cv::Mat previewFrame;
	cv::Mat adjustedFrame;

	const int maxBitDepth = std::min<int>(cameraProperty.MaxBitDepth, 16);
	const double maxRawValue = (maxBitDepth > 0) ? (static_cast<double>((1 << maxBitDepth) - 1)) : 65535.0;

	while (g_keepRunning)
	{
		const int currentGainValue = gainTrackbarValue;
		const long currentExposureMSeconds = exposureTrackbarValue;
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
			PrintTimestampedFrameInfo(prompt, frameBuffer.size());

			{
				displayFrame = frame;

				if (displayFrame.depth() == CV_16U)
					displayFrame.convertTo(previewFrame, CV_8U, 255.0 / maxRawValue);
				else
					previewFrame = displayFrame;

				const double alpha = contrast / 100.0;
				const double beta = (brightness - 100) * 2.55;
				previewFrame.convertTo(adjustedFrame, -1, alpha, beta);

				cv::imshow(windowName, adjustedFrame);
				if (cv::waitKey(1) == 'q')
					g_keepRunning = 0;

				// ウィンドウの状態チェック
				double prop = cv::getWindowProperty(windowName, cv::WND_PROP_VISIBLE);
				if (prop < 1)
					g_keepRunning = 0;
			}
		}
		else // if (ret != SVB_ERROR_TIMEOUT)
		{
			printf("SVBGetVideoData failed: %d\n", ret);
		}
	}

	return 0;
}

// Main function to initialize the camera, set parameters, and start the capture loop
static int RunCaptureLoop(const CameraProfile& cameraProfile, const CommandLineOptions& options)
{
	const int cameraID = cameraProfile.GetCameraID();
	SVB_ERROR_CODE ret = SVBOpenCamera(cameraID);
	if (ret != SVB_SUCCESS)
	{
		printf("open camera failed.\n");
		return -1;
	}

	SVB_CAMERA_PROPERTY cameraProperty;
	if (PrintCameraProperty(cameraID, cameraProperty) != 0)
	{
		SVBCloseCamera(cameraID);
		return -1;
	}

	CameraProfile loadedCameraProfile = cameraProfile;
	if (loadedCameraProfile.LoadControlCaps() != 0)
	{
		SVBCloseCamera(cameraID);
		return -1;
	}

	if (PrintControlCapabilities(loadedCameraProfile) != 0)
	{
		SVBCloseCamera(cameraID);
		return -1;
	}

	SVB_IMG_TYPE outputImageType = SVB_IMG_END;
	std::vector<unsigned char> frameBuffer;
	if (ConfigureCaptureSession(cameraID, cameraProperty, options, outputImageType, frameBuffer) != 0)
	{
		SVBCloseCamera(cameraID);
		return -1;
	}

	const int previewResult = RunPreviewLoop(
		cameraID,
		cameraProperty,
		loadedCameraProfile,
		options,
		outputImageType,
		frameBuffer);

	SVBStopVideoCapture(cameraID);
	cv::destroyAllWindows();

	printf("\nclose camera\n\n");
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

	if (options.cameraIndex >= cameraNum)
	{
		printf("camera index out of range: %d (available: 0-%d)\n", options.cameraIndex, cameraNum - 1);
		return 1;
	}

	CameraProfile cameraProfile;
	try
	{
		cameraProfile = CameraProfile::Load(options.cameraIndex);
	}
	catch (const std::exception& ex)
	{
		printf("%s\n", ex.what());
		return 1;
	}

	printf("Friendly name: %s\n", cameraProfile.cameraInfo.FriendlyName);
	printf("Port type: %s\n", cameraProfile.cameraInfo.PortType);
	printf("SN: %s\n", cameraProfile.cameraInfo.CameraSN);
	printf("Device ID: 0x%x\n", cameraProfile.cameraInfo.DeviceID);
	printf("Camera ID: %d\n", cameraProfile.cameraInfo.CameraID);

	// Start the main capture and preview loop
	return RunCaptureLoop(cameraProfile, options);
}


