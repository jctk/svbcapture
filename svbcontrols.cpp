// SVBDemo.cpp
//
#include <stdio.h>
#include <string.h>

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

int main()
{
	int cameraNum = SVBGetNumOfConnectedCameras();
	printf("Scan camera number: %d\n\n", cameraNum);

	SVB_ERROR_CODE ret;
	int cameraID = -1;
	for (int i = 0; i < cameraNum; i++)
	{
		SVB_CAMERA_INFO cameraInfo;
		ret = SVBGetCameraInfo(&cameraInfo, i);
		if (ret == SVB_SUCCESS)
		{
			printf("Friendly name: %s\n", cameraInfo.FriendlyName);
			printf("Port type: %s\n", cameraInfo.PortType);
			printf("SN: %s\n", cameraInfo.CameraSN);
			printf("Device ID: 0x%x\n", cameraInfo.DeviceID);
			printf("Camera ID: %d\n", cameraInfo.CameraID);
			cameraID = cameraInfo.CameraID;
		}

		//////////////////////////////////////
		// open the camera
		ret = SVBOpenCamera(cameraID);
		if (ret != SVB_SUCCESS)
		{
			printf("open camera failed.\n");
			return -1;
		}

		// get the number of controls
		int numOfControls = 0;
		ret = SVBGetNumOfControls(cameraID, &numOfControls);
		if (ret != SVB_SUCCESS)
		{
			printf("\nget number of controls failed.\n"	);
		}
		else {
			printf("\nnumber of controls: %d\n", numOfControls);
		}

		// get the controls information
		for (int j = 0; j < numOfControls; j++)
		{
			SVB_CONTROL_CAPS caps;
			printf("\ncontrol index: %d\n", j);
			ret = SVBGetControlCaps(cameraID, j, &caps);
			if (ret != SVB_SUCCESS)
			{
				printf("get control caps failed.\n");
				continue;
			}
			printf("control type: %d (%s)\n", caps.ControlType, GetControlTypeName((SVB_CONTROL_TYPE)caps.ControlType));
			printf("control name: %s\n", caps.Name);
			printf("control Description: %s\n", caps.Description);
			printf("Maximum value: %ld\n", caps.MaxValue);
			printf("minimum value: %ld\n", caps.MinValue);
			printf("default value: %ld\n", caps.DefaultValue);
			printf("is auto supported: %s\n", caps.IsAutoSupported ? "YES" : "NO");
			printf("is writable: %s\n", caps.IsWritable ? "YES" : "NO");
		}	

		printf("close camera\n\n");
		SVBCloseCamera(cameraID);
	}
	return 0;
}


