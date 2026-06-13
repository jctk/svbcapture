// SVBDemo.cpp
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <climits>
#include <csignal>
#include <ctime>
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

// Main function to initialize the camera, set parameters, and start the capture loop
int main(int argc, char* argv[])
{
	// Register the signal handler for SIGINT to allow graceful shutdown when Ctrl+C is pressed
	signal(SIGINT, HandleSignal);

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
	int cameraIndex = -1;
	SVB_CAMERA_MODE cameraModeOption = SVB_MODE_NORMAL;
	double exposureSeconds = 0.5;
	int gainValue = 100;
	int offsetValue = 20;
	double displayRatio = 0.25;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--camera-index") == 0 && i + 1 < argc)
		{
			cameraIndex = atoi(argv[++i]);
		}
		else if (strcmp(argv[i], "--camera-mode") == 0 && i + 1 < argc)
		{
			const char* mode = argv[++i];
			if (strcmp(mode, "video") == 0)
			{
				cameraModeOption = SVB_MODE_NORMAL;
			}
			else if (strcmp(mode, "image") == 0)
			{
				cameraModeOption = SVB_MODE_TRIG_SOFT;
			}
			else
			{
				printf("invalid --camera-mode: %s (use video or image)\n", mode);
				return 1;
			}
		}
		else if (strcmp(argv[i], "--exp") == 0 && i + 1 < argc)
		{
			char* endPtr = NULL;
			double parsedExposure = strtod(argv[++i], &endPtr);
			if (endPtr == argv[i] || *endPtr != '\0' || parsedExposure <= 0.0)
			{
				printf("invalid --exp: %s (must be a number greater than 0)\n", argv[i]);
				return 1;
			}
			exposureSeconds = parsedExposure;
		}
		else if (strcmp(argv[i], "--gain") == 0 && i + 1 < argc)
		{
			char* endPtr = NULL;
			long parsedGain = strtol(argv[++i], &endPtr, 10);
			if (endPtr == argv[i] || *endPtr != '\0' || parsedGain < 0 || parsedGain > 500)
			{
				printf("invalid --gain: %s (must be an integer between 0 and 500)\n", argv[i]);
				return 1;
			}
			gainValue = static_cast<int>(parsedGain);
		}
		else if (strcmp(argv[i], "--offset") == 0 && i + 1 < argc)
		{
			char* endPtr = NULL;
			long parsedOffset = strtol(argv[++i], &endPtr, 10);
			if (endPtr == argv[i] || *endPtr != '\0' || parsedOffset < 0 || parsedOffset > 100)
			{
				printf("invalid --offset: %s (must be an integer between 0 and 100)\n", argv[i]);
				return 1;
			}
			offsetValue = static_cast<int>(parsedOffset);
		}
		else if (strcmp(argv[i], "--ratio") == 0 && i + 1 < argc)
		{
			char* endPtr = NULL;
			double parsedRatio = strtod(argv[++i], &endPtr);
			if (endPtr == argv[i] || *endPtr != '\0' || parsedRatio < 0.1 || parsedRatio > 1.0)
			{
				printf("invalid --ratio: %s (must be a number between 0.1 and 1.0)\n", argv[i]);
				return 1;
			}
			displayRatio = parsedRatio;
		}
		else if (strcmp(argv[i], "--exp") == 0 || strcmp(argv[i], "--gain") == 0 || strcmp(argv[i], "--offset") == 0 || strcmp(argv[i], "--ratio") == 0)
		{
			printf("missing value for %s\n", argv[i]);
			return 1;
		}
	}

	if (cameraIndex < 0)
	{
		printf("usage: %s --camera-index <index> [--camera-mode <video|image>] [--exp <seconds>] [--gain <0-500>] [--offset <0-100>] [--ratio <0.1-1.0>]\n", argv[0]);
		return 1;
	}

	int cameraNum = SVBGetNumOfConnectedCameras();
	printf("Scan camera number: %d\n\n", cameraNum);

	if (cameraIndex >= cameraNum)
	{
		printf("camera index out of range: %d (available: 0-%d)\n", cameraIndex, cameraNum - 1);
		return 1;
	}

	SVB_ERROR_CODE ret;
	int cameraID = -1;
	std::string cameraFriendlyName;
	{
		SVB_CAMERA_INFO cameraInfo;
		ret = SVBGetCameraInfo(&cameraInfo, cameraIndex);
		if (ret != SVB_SUCCESS)
		{
			printf("get camera info failed.\n");
			return 1;
		}
		else
		{
			printf("Friendly name: %s\n", cameraInfo.FriendlyName);
			printf("Port type: %s\n", cameraInfo.PortType);
			printf("SN: %s\n", cameraInfo.CameraSN);
			printf("Device ID: 0x%x\n", cameraInfo.DeviceID);
			printf("Camera ID: %d\n", cameraInfo.CameraID);
			cameraID = cameraInfo.CameraID;
			cameraFriendlyName = cameraInfo.FriendlyName;
		}
	}

	//////////////////////////////////////
	// open the camera
	ret = SVBOpenCamera(cameraID);
	if (ret != SVB_SUCCESS)
	{
		printf("open camera failed.\n");
		return -1;
	}

	// get camera property
	SVB_CAMERA_PROPERTY cameraProperty;
	ret = SVBGetCameraProperty(cameraID, &cameraProperty);
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


	// get the number of controls
	int numOfControls = 0;
	ret = SVBGetNumOfControls(cameraID, &numOfControls);
	if (ret != SVB_SUCCESS)
	{
		printf("\nget number of controls failed.\n");
		return -1;
	}
	else {
		printf("\nnumber of controls: %d\n", numOfControls);
	}

	std::vector<SVB_CONTROL_CAPS> controlCapsList;
	controlCapsList.reserve(numOfControls);

	// get the controls information
	for (int i = 0; i < numOfControls; i++)
	{
		SVB_CONTROL_CAPS caps;
		ret = SVBGetControlCaps(cameraID, i, &caps);
		if (ret != SVB_SUCCESS)
		{
			printf("get control caps failed.\n");
			continue;
		}
		printf(
			"control index: %d, type: %d (%s), name: %s, desc: %s, min/max/default: %ld/%ld/%ld, auto: %s, writable: %s\n",
			i,
			caps.ControlType,
			GetControlTypeName((SVB_CONTROL_TYPE)caps.ControlType),
			caps.Name,
			caps.Description,
			caps.MinValue,
			caps.MaxValue,
			caps.DefaultValue,
			caps.IsAutoSupported ? "YES" : "NO",
			caps.IsWritable ? "YES" : "NO");
		controlCapsList.push_back(caps);
	}	

	if (!controlCapsList.empty())
	{
		const SVB_CONTROL_CAPS& firstControl = controlCapsList[0];
		printf("\nStored controls: %zu\n", controlCapsList.size());
		printf("First stored control name: %s\n", firstControl.Name);
	}

	size_t maxBytesPerPixel = 1;
	for (int i = 0; i < 8; i++)
	{
		const SVB_IMG_TYPE format = cameraProperty.SupportedVideoFormat[i];
		if (format == SVB_IMG_END)
		{
			break;
		}

		const size_t bytesPerPixel = BytesPerPixelFromImageType(format);
		if (bytesPerPixel > maxBytesPerPixel)
		{
			maxBytesPerPixel = bytesPerPixel;
		}
	}

	ret = SVBSetROIFormat(cameraID, 0, 0, cameraProperty.MaxWidth, cameraProperty.MaxHeight, 1);
	assert(ret == SVB_SUCCESS);

	// set output image type, if the camera mode is normal mode, we will set it to RGB24 for color camera and Y16 for mono camera; if the camera mode is trigger mode, we will set it to RGB24 for color camera and Y16 for mono camera.
	const SVB_IMG_TYPE outputImageType =
		(cameraModeOption == SVB_MODE_NORMAL) ? SVB_IMG_RGB24 :
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

	std::vector<unsigned char> frameBuffer(frameBufferSize, 0); // Keep a large capture buffer on the heap to avoid stack overflow.

	printf("cameraMode: %d\r\n", cameraModeOption);
	ret = SVBSetCameraMode(cameraID, cameraModeOption);
	assert(ret == SVB_SUCCESS);

	// start video capture
	ret = SVBStartVideoCapture(cameraID);
	assert(ret == SVB_SUCCESS);

    // fix for SDK gain error issue
    // set exposure time
    SVBSetControlValue(cameraID, SVB_EXPOSURE, static_cast<long>(1 * 1000000L), SVB_FALSE);

    ret = SVBSetControlValue(cameraID, SVB_BAD_PIXEL_CORRECTION_ENABLE, 0, SVB_FALSE);
	assert(ret == SVB_SUCCESS);

    ret = SVBSetControlValue(cameraID, SVB_GAIN, gainValue, SVB_FALSE);
	assert(ret == SVB_SUCCESS);

    ret = SVBSetControlValue(cameraID, SVB_BLACK_LEVEL, offsetValue, SVB_FALSE);
	assert(ret == SVB_SUCCESS);

	ret = SVBSetControlValue(cameraID, SVB_EXPOSURE, exposureSeconds*1000*1000, SVB_FALSE);
	assert(ret == SVB_SUCCESS);

	const char* prompt = (cameraModeOption == SVB_MODE_NORMAL) ? "Video" : "Image";
	const std::string windowName = cameraFriendlyName.empty() ? "SVB Camera" : cameraFriendlyName;

	cv::namedWindow(windowName, cv::WINDOW_NORMAL);
	cv::resizeWindow(windowName, cameraProperty.MaxWidth*displayRatio, cameraProperty.MaxHeight*displayRatio);
	int brightness = 100;
	int contrast = 100;
	cv::createTrackbar("Brightness", windowName, &brightness, 200);
	cv::createTrackbar("Contrast", windowName, &contrast, 200);
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
		if (cameraModeOption == SVB_MODE_TRIG_SOFT)
		{
			ret = SVBSendSoftTrigger(cameraID);
			assert(ret == SVB_SUCCESS);
		}

		ret = SVBGetVideoData(cameraID, frameBuffer.data(), static_cast<long>(frameBuffer.size()), exposureSeconds*1000*1000);
		if (ret == SVB_SUCCESS)
		{
			PrintTimestampedFrameInfo(prompt, frameBuffer.size());

			{
				if (isRgb24)
					cv::cvtColor(frame, displayFrame, cv::COLOR_RGB2BGR);
				else
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

	SVBStopVideoCapture(cameraID);
	cv::destroyAllWindows();

	printf("\nclose camera\n\n");
	SVBCloseCamera(cameraID);
	return 0;
}


