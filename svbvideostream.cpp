// SVBDemo.cpp
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <climits>
#include <csignal>
#include <ctime>
#include <vector>

#include "SVBCameraSDK.h"

#include <assert.h>

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

static volatile sig_atomic_t g_keepRunning = 1;

static void HandleSignal(int sig)
{
	if (sig == SIGINT)
	{
		g_keepRunning = 0;
	}
}

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

int main(int argc, char* argv[])
{
	signal(SIGINT, HandleSignal);

	/*
	 * parse command line arguments
	 * --camera-index <index>: specify which camera to use (default: 0)
	 * --camera-mode <normal|trigger>: camera mode (default: normal)
	 * example: svbcontrols --camera-index 1
	*/
	int cameraIndex = -1;
	SVB_CAMERA_MODE cameraModeOption = SVB_MODE_NORMAL;
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
	}

	if (cameraIndex < 0)
	{
		printf("usage: %s --camera-index <index> [--camera-mode <video|image>]\n", argv[0]);
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

	if (cameraModeOption == SVB_MODE_NORMAL) {
		ret = SVBSetControlValue(cameraID, SVB_EXPOSURE, 0.1 * 1000 * 1000, SVB_FALSE);
		assert(ret == SVB_SUCCESS);

		while (g_keepRunning) {
			ret = SVBGetVideoData(cameraID, frameBuffer.data(), static_cast<long>(frameBuffer.size()), 200);
			if (ret == SVB_SUCCESS)
			{
				struct timespec ts;
				clock_gettime(CLOCK_REALTIME, &ts);
				time_t now = ts.tv_sec;
				long msec = ts.tv_nsec / 1000000;
				struct tm localTm;
				char timestamp[32] = {0};
				localtime_r(&now, &localTm);
				strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &localTm);
				printf("\r[%s.%03ld] Video data: %zu bytes", timestamp, msec, frameBuffer.size());
				fflush(stdout);
			}
			else if (ret != SVB_ERROR_TIMEOUT)
			{
				printf("SVBGetVideoData failed: %d\n", ret);
			}
		}

		SVBStopVideoCapture(cameraID);
	}
	else if (cameraModeOption == SVB_MODE_TRIG_SOFT)
	{
		while (g_keepRunning)
		{
		    ret = SVBSetControlValue(cameraID, SVB_EXPOSURE, 0.5 * 1000 * 1000, SVB_FALSE);
			assert(ret == SVB_SUCCESS);

			ret = SVBSendSoftTrigger(cameraID);
			assert(ret == SVB_SUCCESS);

			ret = SVBGetVideoData(cameraID, frameBuffer.data(), static_cast<long>(frameBuffer.size()), 500*1000);
			if (ret == SVB_SUCCESS)
			{
				struct timespec ts;
				clock_gettime(CLOCK_REALTIME, &ts);
				time_t now = ts.tv_sec;
				long msec = ts.tv_nsec / 1000000;
				struct tm localTm;
				char timestamp[32] = {0};
				localtime_r(&now, &localTm);
				strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &localTm);
				printf("\r[%s.%03ld] Image data: %zu bytes", timestamp, msec, frameBuffer.size());
				fflush(stdout);
			}
			else if (ret != SVB_ERROR_TIMEOUT)
			{
				printf("SVBGetVideoData failed: %d\n", ret);
			}
		}
	}

	printf("close camera\n\n");
	SVBCloseCamera(cameraID);
	return 0;
}


