//------------------------------------------------------------------------------
// <copyright file="CoordinateMappingBasics.h" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//------------------------------------------------------------------------------


#include "resource.h"
#include "stdafx.h"
//#include <strsafe.h>

#include "KinectV1Controller.h"

#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/core/core.hpp>
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/features2d/features2d.hpp"

KinectV1Controller::KinectV1Controller():m_pNuiSensor(nullptr),m_pDrawDataStreams(nullptr),m_colorFrameCount(0),m_depthFrameCount(0)
{		
	DWORD width = 0;
    DWORD height = 0;
	m_paused = false;
	m_nearMode =false;
	m_depthTreatment = CLAMP_UNRELIABLE_DEPTHS; //DISPLAY_ALL_DEPTHS CLAMP_UNRELIABLE_DEPTHS TINT_UNRELIABLE_DEPTHS
    NuiImageResolutionToSize(cDepthResolution, width, height);
    m_depthWidth  = static_cast<LONG>(width);
    m_depthHeight = static_cast<LONG>(height);

	
	m_depthD16 = nullptr;

    NuiImageResolutionToSize(cColorResolution, width, height);
    m_colorWidth  = static_cast<LONG>(width);
    m_colorHeight = static_cast<LONG>(height);
	m_alignedDepthD16 = nullptr;
	/*
    m_colorToDepthDivisor = m_colorWidth/m_depthWidth;
	
	


	m_colorCoordinates = new LONG[m_colorWidth*m_colorHeight*2];
	*/
	//m_colorRGBX = new NuiImageBuffer();
	// m_alignedInfraredRGBX = new RGBQUAD[m_colorWidth * m_colorHeight];
	
	m_showOpt = KinectV1ColorRaw;
	m_showResolution = NUI_IMAGE_RESOLUTION_640x480;
	m_drawRGBX =   &m_colorRGBX;//&m_colorRGBX;

	m_hNextColorFrameEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	//m_hNextInfaredFrameEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	m_hNextDepthFrameEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	
    m_depthTimeStamp.QuadPart = 0;
    m_colorTimeStamp.QuadPart = 0;

	m_LastColorTimeStamp.QuadPart = 0;
	m_LastDepthTimeStamp.QuadPart = 0;

	m_FPSLimit = -1;

	m_alignmentEnabled = false;

	//cv::namedWindow("test_orig");
	//cv::namedWindow("test_alignment");
}


KinectV1Controller::~KinectV1Controller()
{
	//if (m_alignedInfraredRGBX){
	//	delete []m_alignedInfraredRGBX;
	//}

	  // clean up Direct2D renderer
    if (m_pDrawDataStreams)
    {
        delete m_pDrawDataStreams;
        m_pDrawDataStreams = nullptr;
    }


}

HRESULT KinectV1Controller::init()
{

	HRESULT hr = CreateFirstConnected();
	if(FAILED(hr))
	{
		return hr;
	}


	
	m_depthResolution = NUI_IMAGE_RESOLUTION_320x240;
	hr = openDepthStream(); //NUI_IMAGE_RESOLUTION_320x240 NUI_IMAGE_RESOLUTION_640x480
	m_colorImageType = NUI_IMAGE_TYPE_COLOR;

	m_colorResolution = NUI_IMAGE_RESOLUTION_640x480;
	hr = openColorStream();

	//hr = openInfraredStream();
	m_alignedDepthRGBX.SetImageSize(NUI_IMAGE_RESOLUTION_640x480); 
	//hr = openInfraredStream(NUI_IMAGE_RESOLUTION_640x480);

	return hr;
}


HRESULT KinectV1Controller:: CreateFirstConnected()
{
	INuiSensor * pNuiSensor;
    HRESULT hr=E_FAIL;

    int iSensorCount = 0;
    hr = NuiGetSensorCount(&iSensorCount);
    if (FAILED(hr)||iSensorCount==0)
    {
        return E_FAIL;
    }

    // Look at each Kinect sensor
    for (int i = 0; i < iSensorCount; ++i)
    {
        // Create the sensor so we can check status, if we can't create it, move on to the next
        hr = NuiCreateSensorByIndex(i, &pNuiSensor);
        if (FAILED(hr))
        {
            continue;
        }

        // Get the status of the sensor, and if connected, then we can initialize it
        hr = pNuiSensor->NuiStatus();
        if (S_OK == hr)
        {
            m_pNuiSensor = pNuiSensor;
            break;
        }

        // This sensor wasn't OK, so release it since we're not using it
        pNuiSensor->Release();
    }

    if (NULL != m_pNuiSensor)
    {
		    // Initialize Nui sensor
     hr = m_pNuiSensor->NuiInitialize(
        NUI_INITIALIZE_FLAG_USES_DEPTH_AND_PLAYER_INDEX
        | NUI_INITIALIZE_FLAG_USES_SKELETON
        | NUI_INITIALIZE_FLAG_USES_COLOR
        | NUI_INITIALIZE_FLAG_USES_AUDIO);

        
    }

    if (NULL == m_pNuiSensor || FAILED(hr))
    {
        //SetStatusMessage(L"No ready Kinect found!");
        return E_FAIL;
    }

    return hr;

}

HANDLE KinectV1Controller::getCorlorFrameEvent()
{
	return m_hNextColorFrameEvent;
}

HANDLE KinectV1Controller::getDepthFrameEvent()
{
	return m_hNextDepthFrameEvent;
}

void KinectV1Controller::Update()
{

    if (NULL == m_pNuiSensor)
    {
        return;
    }
	const int eventCount = 2;
    HANDLE hEvents[eventCount];

	hEvents[0] = m_hNextColorFrameEvent;
	hEvents[1] = m_hNextDepthFrameEvent;
	//hEvents[2] = m_hNextInfaredFrameEvent;


	MsgWaitForMultipleObjects(eventCount, hEvents, FALSE, INFINITE, QS_ALLINPUT);

	if(WAIT_OBJECT_0 == WaitForSingleObject(m_hNextColorFrameEvent, 0))
	{
		ProcessColor();
	}

	if(WAIT_OBJECT_0 == WaitForSingleObject(m_hNextDepthFrameEvent, 0) )
	{
		ProcessDepth();
	}


	//show();
	

}

void KinectV1Controller::showOptUpdated(KinectV1ImageRecordType showOpt)
{
	if(m_showOpt == showOpt)
	{
		return;
	}
	m_showResolution = NUI_IMAGE_RESOLUTION_640x480;
	switch(showOpt)
	{
		case Color_Raw:

			m_colorImageType = NUI_IMAGE_TYPE_COLOR;
			m_colorResolution = NUI_IMAGE_RESOLUTION_640x480;
			openColorStream();
			m_drawRGBX = &m_colorRGBX;
			
			break;

		case Depth_Raw:
			m_depthResolution = NUI_IMAGE_RESOLUTION_640x480;
			openDepthStream();
			m_drawRGBX = &m_DepthRGBX;
			break;
		default:break;
	}
	m_showOpt = showOpt;
	
	return ;

}

void KinectV1Controller::showResolutionUpdated(int showResolution)
{
	
	switch(m_showOpt)
	{
		case KinectV1ColorRaw:

			showcolorResolutionUpdated((v1ColorType)(showResolution));
			break;

		case KinectV1DepthRaw:
			showdepthResolutionUpdated((v1DepthType)(showResolution));
			break;
		default:break;
	}
}


void KinectV1Controller::recordingResolutionUpdated(KinectV1ImageRecordType opt,int showResolution)
{
	switch(opt)
	{
		case KinectV1ColorRaw:

			showcolorResolutionUpdated((v1ColorType)(showResolution));
			break;

		case KinectV1DepthRaw:
			showdepthResolutionUpdated((v1DepthType)(showResolution));
			break;
		default:break;
	}
}

void KinectV1Controller::showcolorResolutionUpdated(v1ColorType showResolution)
{
	
	NUI_IMAGE_RESOLUTION tmp_showResolution = m_colorResolution;
	NUI_IMAGE_TYPE		tmp_colorImageType = m_colorImageType;
	//m_showResolution = showResolution;
	switch(showResolution)
	{
		case RESOLUTION_RGBRESOLUTION640X480FPS30:
			m_colorImageType = NUI_IMAGE_TYPE_COLOR;
			m_colorResolution = NUI_IMAGE_RESOLUTION_640x480;                
			break;
		case RESOLUTION_RGBRESOLUTION1280X960FPS12:
			m_colorImageType = NUI_IMAGE_TYPE_COLOR;
			m_colorResolution = NUI_IMAGE_RESOLUTION_1280x960; 
			break;
		case RESOLUTION_YUVRESOLUTION640X480FPS15:
			m_colorImageType = NUI_IMAGE_TYPE_COLOR_YUV;
			m_colorResolution = NUI_IMAGE_RESOLUTION_640x480; 
			break;
		case RESOLUTION_INFRAREDRESOLUTION640X480FPS30:
			m_colorImageType = NUI_IMAGE_TYPE_COLOR_INFRARED;
			m_colorResolution = NUI_IMAGE_RESOLUTION_640x480; 
			break;
		case RESOLUTION_RAWBAYERRESOLUTION640X480FPS30:
			m_colorImageType = NUI_IMAGE_TYPE_COLOR_RAW_BAYER;
			m_colorResolution = NUI_IMAGE_RESOLUTION_640x480; 
			break;
		case RESOLUTION_RAWBAYERRESOLUTION1280X960FPS12:
			m_colorImageType = NUI_IMAGE_TYPE_COLOR_RAW_BAYER;
			m_colorResolution = NUI_IMAGE_RESOLUTION_1280x960; 
			break;
		default:break;
	}

	if(tmp_colorImageType != m_colorImageType || tmp_showResolution!= m_colorResolution)
	{
		openColorStream();
	}

	return;
}

void KinectV1Controller::showdepthResolutionUpdated(v1DepthType showResolution)
{
	
	NUI_IMAGE_RESOLUTION tmp_showResolution = m_depthResolution;
	switch(showResolution)
	{
		case RESOLUTION_DEPTHESOLUTION640X480FPS30:
			m_depthResolution = NUI_IMAGE_RESOLUTION_640x480;                
			break;
		case RESOLUTION_DEPTHRESOLUTION320X240FPS30:
			m_depthResolution = NUI_IMAGE_RESOLUTION_320x240; 
			break;
		case RESOLUTION_DEPTHRESOLUTION80X60FP30:
			m_depthResolution = NUI_IMAGE_RESOLUTION_80x60; 
			break;

		default:break;
	}
	if(tmp_showResolution != m_depthResolution)
	{
		openDepthStream();
	}
	return;
}


void KinectV1Controller::show()
{

	 
	m_pDrawDataStreams->setSize(m_drawRGBX->GetWidth(),m_drawRGBX->GetHeight(),m_drawRGBX->GetWidth() * sizeof(RGBQUAD));
	//m_pDrawDataStreams->initialize(m_liveViewWindow, m_pD2DFactory, cColorWidth, cColorHeight, cColorWidth * sizeof(RGBQUAD));
	//m_pDrawDataStreams->initialize(m_hWnd, m_pD2DFactory, m_drawRGBX->GetWidth(),m_drawRGBX->GetHeight(),m_drawRGBX->GetWidth() * sizeof(RGBQUAD));
	HRESULT hr = m_pDrawDataStreams->beginDrawing();
	hr = m_pDrawDataStreams->drawBackground(reinterpret_cast<BYTE*>(m_drawRGBX->GetBuffer()), m_drawRGBX->GetWidth() * m_drawRGBX->GetHeight()* sizeof(RGBQUAD));
	m_pDrawDataStreams->DrawFPS(m_showFps);

	hr = m_pDrawDataStreams->endDrawing();
	
}


 
void KinectV1Controller::ProcessDepth()
{

	HRESULT hr;
	//if (WAIT_OBJECT_0 == WaitForSingleObject(GetFrameReadyEvent(), 0))
	
    NUI_IMAGE_FRAME imageFrame;
	m_depthMutex.lock();
    // Attempt to get the depth frame
    hr = m_pNuiSensor->NuiImageStreamGetNextFrame(m_pDepthStreamHandle, 0, &imageFrame);
    if (FAILED(hr))
    {
		m_depthMutex.unlock();
        return;
    }
	
	if (m_paused || ifDumpDepthFrame(&imageFrame))
    {
        // Stream paused. Skip frame process and release the frame.
        goto ReleaseFrame;
    }
	m_depthTimeStamp = imageFrame.liTimeStamp;
    BOOL nearMode;
    INuiFrameTexture* pTexture;

    // Get the depth image pixel texture
    hr = m_pNuiSensor->NuiImageFrameGetDepthImagePixelFrameTexture(m_pDepthStreamHandle, &imageFrame, &nearMode, &pTexture);
    if (FAILED(hr))
    {
        goto ReleaseFrame;
    }

    NUI_LOCKED_RECT lockedRect;

    // Lock the frame data so the Kinect knows not to modify it while we're reading it
    pTexture->LockRect(0, &lockedRect, NULL, 0);

    // Make sure we've received valid data
    if (lockedRect.Pitch != 0)
    {
        // Conver depth data to color image and copy to image buffer
		//m_DepthRGBX.CopyDepth(lockedRect.pBits, lockedRect.size, nearMode, m_depthTreatment);
	
		
		getRawDepthData(lockedRect.pBits, lockedRect.size, m_depthD16,m_depthWidth, m_depthHeight);
        //memcpy((BYTE *)&m_depthD16[0],,);

		m_DepthRGBX.CopyDepth(lockedRect.pBits, lockedRect.size, nearMode, m_depthTreatment);
		//AlignDepthToColorSpace();
		/*
		m_pDrawDataStreams->setSize(m_DepthRGBX.GetWidth(),m_DepthRGBX.GetHeight(),m_DepthRGBX.GetWidth() * sizeof(RGBQUAD));
		hr = m_pDrawDataStreams->beginDrawing();
		hr = m_pDrawDataStreams->drawBackground(reinterpret_cast<BYTE*>(m_DepthRGBX.GetBuffer()), m_DepthRGBX.GetWidth() * m_DepthRGBX.GetHeight()* sizeof(RGBQUAD));
		m_pDrawDataStreams->endDrawing();
		*/

    }


    // Done with the texture. Unlock and release it
    pTexture->UnlockRect(0);
    pTexture->Release();

	UpdateDepthFrameRate();

	if(m_showOpt ==  Depth_Raw) 
	{
		m_showFps = m_depthFps;
		show();
		//int a = 5;
		//cv::Mat m_depthImage = cv::Mat(m_DepthRGBX.GetHeight(), m_DepthRGBX.GetWidth(), CV_8UC4, m_DepthRGBX.GetBuffer(), cv::Mat::AUTO_STEP);
		//cv::imshow("DepthWindow",m_depthImage);
		//boost::shared_ptr<cv::Mat> m_depthImagePtr(new cv::Mat());
		//*m_depthImagePtr = m_depthImage.clone();
		//imageUpdated[1](m_depthImagePtr);


	}
ReleaseFrame:
    // Release the frame
    m_pNuiSensor->NuiImageStreamReleaseFrame(m_pDepthStreamHandle, &imageFrame);
	m_depthMutex.unlock();
	return;
}





void KinectV1Controller::UpdateSensorAndStatus(DWORD changedFlags)
{

}


void KinectV1Controller::UpdateNscControlStatus()
{

}

void KinectV1Controller::setImageRenderer(ImageRenderer* renderer,ID2D1Factory* pD2DFactory ){
	m_pDrawDataStreams = renderer; 
	m_pD2DFactory = pD2DFactory;
}

void KinectV1Controller::SetIcon(HWND hWnd)
{
	m_hWnd = hWnd;
}





HRESULT KinectV1Controller::openColorStream()
{
    // Open color stream.

	HRESULT hr;
	m_colorMutex.lock();
	if (m_pNuiSensor)
    {
     
     hr = m_pNuiSensor->NuiImageStreamOpen(m_colorImageType,
                                                  m_colorResolution,
                                                  0,
                                                  2,
                                                  m_hNextColorFrameEvent,
                                                  &m_pColorStreamHandle);

	}

	if (SUCCEEDED(hr))
    {
        //m_pNuiSensor->NuiImageStreamSetImageFrameFlags(m_hStreamHandle, m_nearMode ? NUI_IMAGE_STREAM_FLAG_ENABLE_NEAR_MODE : 0);   // Set image flags
		m_colorRGBX.SetImageSize(m_colorResolution); // Set source image resolution to image buffer
		DWORD width = 0;
		DWORD height = 0;
		NuiImageResolutionToSize(m_colorResolution, width, height);
		m_colorWidth  = static_cast<LONG>(width);
		m_colorHeight = static_cast<LONG>(height);
		if(m_alignmentEnabled)
		{
			if(m_alignedDepthD16)
			{
				delete m_alignedDepthD16;
			}
			m_alignedDepthD16 = new USHORT [m_colorWidth*m_colorHeight];
		}
    }
	m_colorMutex.unlock();
    return hr;
}


HRESULT KinectV1Controller::openDepthStream()
{
	HRESULT hr;
    // Open color stream.
	m_depthMutex.lock();
	NUI_IMAGE_TYPE imageType = HasSkeletalEngine(m_pNuiSensor) ? NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX : NUI_IMAGE_TYPE_DEPTH;
	//NUI_IMAGE_TYPE imageType = NUI_IMAGE_TYPE_DEPTH;
    // Open depth stream
    hr = m_pNuiSensor->NuiImageStreamOpen(imageType,
                                                  m_depthResolution,
                                                  0,
                                                  2,
                                                  m_hNextDepthFrameEvent,
                                                  &m_pDepthStreamHandle);
    if (SUCCEEDED(hr))
    {
        m_pNuiSensor->NuiImageStreamSetImageFrameFlags(m_pDepthStreamHandle, m_nearMode ? NUI_IMAGE_STREAM_FLAG_ENABLE_NEAR_MODE : 0);   // Set image flags

		
		DWORD width = 0;
		DWORD height = 0;
		NuiImageResolutionToSize(m_depthResolution, width, height);
		if(m_depthWidth != width || !m_depthD16)
		{
			m_depthWidth  = static_cast<LONG>(width);
			m_depthHeight = static_cast<LONG>(height);

			if(m_depthD16)
			{
				delete m_depthD16;
				//m_depthD16 = nullptr;
			
			}
			m_depthD16 = new USHORT[m_depthWidth*m_depthHeight];
		}
		m_DepthRGBX.SetImageSize(m_depthResolution); // Set source image resolution to image buffer
    }
	m_depthMutex.unlock();
    return hr;


}

HRESULT KinectV1Controller::openIbodyStream(){
	HRESULT hr = E_FAIL;

	return hr;
}

HRESULT KinectV1Controller::openAudioStream(){
	HRESULT hr = E_FAIL;

	return hr;
}


void KinectV1Controller::ProcessColor()
{

    HRESULT hr;

    NUI_IMAGE_FRAME imageFrame;
	m_colorMutex.lock();
    // Attempt to get the color frame
    hr = m_pNuiSensor->NuiImageStreamGetNextFrame(m_pColorStreamHandle, 0, &imageFrame);
    if (FAILED(hr))
    {
		m_colorMutex.unlock();
        return;
    }

	
    if (m_paused || ifDumpColorFrame(&imageFrame))
    {
        // Stream paused. Skip frame process and release the frame.
        goto ReleaseFrame;
    }

	m_colorTimeStamp = imageFrame.liTimeStamp;
    INuiFrameTexture* pTexture = imageFrame.pFrameTexture;

    // Lock the frame data so the Kinect knows not to modify it while we are reading it
    NUI_LOCKED_RECT lockedRect;
    pTexture->LockRect(0, &lockedRect, NULL, 0);

    // Make sure we've received valid data
    if (lockedRect.Pitch != 0)
    {
			m_colorFrameArrived = true;
 
			//m_colorRGBX.CopyRGB(lockedRect.pBits, lockedRect.size);
			switch (m_colorImageType)
			{
				case NUI_IMAGE_TYPE_COLOR_RAW_BAYER:    // Convert raw bayer data to color image and copy to image buffer
					m_colorRGBX.CopyBayer(lockedRect.pBits, lockedRect.size);
					break;

				case NUI_IMAGE_TYPE_COLOR_INFRARED:     // Convert infrared data to color image and copy to image buffer
					m_colorRGBX.CopyInfrared(lockedRect.pBits, lockedRect.size);
					break;

				default:    // Copy color data to image buffer
					m_colorRGBX.CopyRGB(lockedRect.pBits, lockedRect.size);
					break;
			}
			UpdateColorFrameRate();
			if(m_showOpt ==  Color_Raw) 
			{
				m_showFps = m_colorFps;
				show();
			}

    }

    // Unlock frame data
    pTexture->UnlockRect(0);

ReleaseFrame:
    m_pNuiSensor->NuiImageStreamReleaseFrame(m_pColorStreamHandle, &imageFrame);
	m_colorMutex.unlock();
}

void	KinectV1Controller::AlignDepthToColorSpace()
{

	INuiCoordinateMapper *pMapper;

	NUI_COLOR_IMAGE_POINT* colorPoints = new NUI_COLOR_IMAGE_POINT[ m_depthWidth * m_depthHeight]; //color points
    NUI_DEPTH_IMAGE_PIXEL* depthPoints = new NUI_DEPTH_IMAGE_PIXEL[ m_depthWidth * m_depthHeight ]; //depth points


	m_pNuiSensor->NuiGetCoordinateMapper(&pMapper);
	HRESULT hr = pMapper->MapDepthFrameToColorFrame(m_depthResolution, m_depthWidth * m_depthHeight, depthPoints, m_colorImageType, m_colorResolution,m_depthWidth * m_depthHeight, colorPoints);
	
	for (int i = 0; i <m_depthWidth * m_depthHeight; i++)
       if (colorPoints[i].x >0 && colorPoints[i].x < m_colorWidth && colorPoints[i].y>0 &&    colorPoints[i].y < m_colorHeight)
		   *(m_alignedDepthD16 + colorPoints[i].x + colorPoints[i].y*m_colorWidth) = *(m_depthD16 + i );
	
	
	delete colorPoints;
	delete depthPoints;
	/*  //for testing
	cv::Mat m_depthImage1 = cv::Mat( m_depthHeight, m_depthWidth,CV_16UC1, m_depthD16, cv::Mat::AUTO_STEP);
	cv::Mat m_depthImage = cv::Mat( m_colorHeight, m_colorWidth,CV_16UC1, m_alignedDepthD16, cv::Mat::AUTO_STEP);
	cv::Mat m_colorImage = cv::Mat(m_depthHeight, m_depthWidth, CV_8UC4, m_DepthRGBX.GetBuffer(),cv::Mat::AUTO_STEP);

	cv::Mat depthf(m_colorHeight,m_colorWidth, CV_8UC1);
	cv::Mat depthf1(m_colorHeight,m_colorWidth, CV_8UC1);
	m_depthImage.convertTo(depthf, CV_8UC1, 255.0/2048.0);
	m_depthImage1.convertTo(depthf1, CV_8UC1, 255.0/2048.0);
	cv::imshow("test_alignment",depthf);

		cv::Mat imageCopy;
		imageCopy = m_colorImage.clone();
		cv::cvtColor(imageCopy, m_colorImage, CV_BGRA2BGR);

	cv::imshow("test_orig",m_colorImage);

	*/
}


/// <summary>
/// Set and reset near mode
/// </summary>
/// <param name="nearMode">True to enable near mode. False to disable</param>
void KinectV1Controller::SetNearMode(bool nearMode)
{
    m_nearMode = nearMode;
    if (INVALID_HANDLE_VALUE != m_pDepthStreamHandle)
    {
        m_pNuiSensor->NuiImageStreamSetImageFrameFlags(m_pDepthStreamHandle, (m_nearMode ? NUI_IMAGE_STREAM_FLAG_ENABLE_NEAR_MODE : 0));
    }
}


CString KinectV1Controller::getColorTypeAsString(v1ColorType colorType)
{
	switch (colorType)
	{
	case RESOLUTION_RGBRESOLUTION640X480FPS30:
		return CString(L"RGB640x480");
	case RESOLUTION_RGBRESOLUTION1280X960FPS12:
		return CString(L"RGB1280x960");
	case RESOLUTION_YUVRESOLUTION640X480FPS15:
		return CString(L"YUVF640x480");
	case RESOLUTION_INFRAREDRESOLUTION640X480FPS30:
		return CString(L"INFRARED640x480");
	case RESOLUTION_RAWBAYERRESOLUTION640X480FPS30:
		return CString(L"BAYER640x480");
	case RESOLUTION_RAWBAYERRESOLUTION1280X960FPS12:
		return CString(L"BAYER1280x960");
	default:
		return CString(L"UNKNOWN TYPE");
		break;
	}
}

CString KinectV1Controller::getDepthTypeAsString(v1DepthType depthType)
{
	switch (depthType)
	{
	case RESOLUTION_DEPTHESOLUTION640X480FPS30:
		return CString(L"DEPTH640x480");
	case RESOLUTION_DEPTHRESOLUTION320X240FPS30:
		return CString(L"DEPTH320x240");
	case RESOLUTION_DEPTHRESOLUTION80X60FP30:
		return CString(L"DEPTH80x60");

	default:
		return CString(L"UNKNOWN TYPE");
		break;
	}
}


/// <summary>
/// Update frame rate
/// </summary>
void KinectV1Controller::UpdateColorFrameRate()
{
    m_colorFrameCount++;

    DWORD tickCount = GetTickCount();
    DWORD span      = tickCount - m_lastColorTick;
    if (span >= 1000)
    {
        m_colorFps            = (UINT)((double)(m_colorFrameCount - m_lastColorFrameCount) * 1000.0 / (double)span + 0.5);
        m_lastColorTick       = tickCount;
        m_lastColorFrameCount = m_colorFrameCount;
    }
}

bool  KinectV1Controller::ifDumpColorFrame(NUI_IMAGE_FRAME *frame)
{
	LARGE_INTEGER     colorTimeStamp = frame->liTimeStamp;
	static LARGE_INTEGER last_acceptedColorTS = {0};
	//UINT colorFps            = (UINT)((double)(m_colorFrameCount - m_lastColorFrameCount) * 1000.0 / (double)span + 0.5);

	if(m_FPSLimit)
	{
		double span      = double(colorTimeStamp.QuadPart-last_acceptedColorTS.QuadPart)/1000;
		if(span <(1.0 / m_FPSLimit))
		{
			return true;
		}
		last_acceptedColorTS = colorTimeStamp;
	}
		

#if 0
	static DWORD last_checkTick = 0;
	/*
	if(last_checkTick == 0)
	{

	}
	*/
	if(m_FPSLimit)
	{
		//if(m_colorFps>m_FPSLimit)
			//return true;
		//timedelta = (double(clock() - start)) 
		DWORD tickCount = clock();
		double span      = double((tickCount - last_checkTick))/CLOCKS_PER_SEC;
		
		if (span < (1.0 / m_FPSLimit))
		{
			return true;
		}
		last_checkTick = tickCount;
		
	}
#endif 
	return false;

}

bool  KinectV1Controller::ifDumpDepthFrame(NUI_IMAGE_FRAME *frame)
{
	if(!m_FPSLimit)
	{
		return false;
	}

	LARGE_INTEGER     depthTimeStamp = frame->liTimeStamp;
	static LARGE_INTEGER last_acceptedColorTS = {0};
	//static   UINT      colorFrameCount;
    //static	 UINT      lastColorFrameCount;
	double span      = double(depthTimeStamp.QuadPart-last_acceptedColorTS.QuadPart); // /1000;
	UINT depthFps            = (UINT)((double)(1) * 1000.0 / (double)span + 0.5);
	if(depthFps < m_FPSLimit)
	{
		last_acceptedColorTS = depthTimeStamp;
		return false;
	}
	
	//if(m_FPSLimit)
	//{
		//double span      = double(depthTimeStamp.QuadPart-last_acceptedColorTS.QuadPart)/1000;
		//if(span  <(1.0 / m_FPSLimit))
		//{
			//return true;
		//}
		//last_acceptedColorTS = depthTimeStamp;
		//lastColorFrameCount++;
	//}

 
	return true;
}

/// <summary>
/// Update frame rate
/// </summary>
void KinectV1Controller::UpdateDepthFrameRate()
{
    m_depthFrameCount++;

    DWORD tickCount = GetTickCount();
    DWORD span      = tickCount - m_lastDepthTick;
    if (span >= 1000)
    {
        m_depthFps            = (UINT)((double)(m_depthFrameCount - m_lastDepthFrameCount) * 1000.0 / (double)span + 0.5);
        m_lastDepthTick       = tickCount;
        m_lastDepthFrameCount = m_depthFrameCount;
    }
}

void KinectV1Controller::setOutPutStreamUpdater(std::shared_ptr<KinectV1OutPutStreamUpdater> KinectV1OutPutStreamUpdater)
{
	m_outPutStreamUpdater = KinectV1OutPutStreamUpdater;
}



LARGE_INTEGER KinectV1Controller::getLastColorFrameStamp()
{
	
    return  m_colorTimeStamp;
};


LARGE_INTEGER KinectV1Controller::getLastDepthFrameStamp()
{
	return  m_depthTimeStamp;
}


void KinectV1Controller::updateWriter()
{

	if(m_colorRGBX.GetBuffer() && m_DepthRGBX.GetBuffer() && m_depthD16
		&& m_LastColorTimeStamp.QuadPart != m_colorTimeStamp.QuadPart && m_LastDepthTimeStamp.QuadPart != m_depthTimeStamp.QuadPart)
	{
		
		m_LastColorTimeStamp = m_colorTimeStamp;
		m_LastDepthTimeStamp = m_depthTimeStamp;
		if(m_alignmentEnabled)
		{
			AlignDepthToColorSpace();
		}
		m_outPutStreamUpdater->startKinectV1DataCollection((RGBQUAD* )m_colorRGBX.GetBuffer(),m_depthD16,m_alignedDepthD16, m_DepthRGBX.GetWidth(), m_DepthRGBX.GetHeight(), m_colorRGBX.GetWidth(), m_colorRGBX.GetHeight());
		m_outPutStreamUpdater->stopKinectV1DataCollection();
		
	}

	
}

void KinectV1Controller::getRawDepthData(const BYTE* srcpImage, UINT size, USHORT* depthpImage, int width, int height)
{
	if(width * height*4 != size)
	{
		return;
	}

	// Initialize pixel pointers to start and end of image buffer
    NUI_DEPTH_IMAGE_PIXEL* pPixelRun = (NUI_DEPTH_IMAGE_PIXEL*)srcpImage;
    NUI_DEPTH_IMAGE_PIXEL* pPixelEnd = pPixelRun + width * height;
	
    // Run through pixels
    while (pPixelRun < pPixelEnd)
    {
        // Get pixel depth and player index
        USHORT depth = pPixelRun->depth;
        USHORT index = pPixelRun->playerIndex;
		//BYTE intensity = GetIntensity(depth);
        // Get mapped color from depth-color table
		*depthpImage = depth;
		depthpImage++;
        ++pPixelRun;
    }
}

UINT KinectV1Controller::getDepthFrameFPS()
{
	return m_depthFps;
}
UINT KinectV1Controller::getColorFrameFPS()
{
	return m_colorFps;
}

void KinectV1Controller::setLimitedFPS(int fps)
{
	m_FPSLimit = fps;
}

void KinectV1Controller::setAlignmentEnable(bool enable)
{
	m_alignmentEnabled = enable;
	if(enable)
	{
		if(m_alignedDepthD16)
		{
			delete m_alignedDepthD16;
		}
		m_alignedDepthD16 = new USHORT [m_colorWidth*m_colorHeight];
	}

}

bool KinectV1Controller::getAlignmentEnable()
{
	return m_alignmentEnabled;
}


