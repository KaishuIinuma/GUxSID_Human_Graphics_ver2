// openFrameworks 0.12.1 の ofAVFoundationGrabber の複製。
// 同じ名前のカメラ (例: C270 が2台) があると、元の実装では並び順が開くたびに
// 変わり、番号と実機が一致しなかった。名前が同じ時は uniqueID で並べて順番を固定する。

#pragma once

#include "ofConstants.h"

#include "ofVideoBaseTypes.h"
#include "ofTexture.h"
#include "ofThread.h"
// MARK: Template, if 
#include "ofPixels.h"
#include <mutex>

#ifdef __OBJC__

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <Accelerate/Accelerate.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

class StableCameraGrabber;

@interface GuxStableVideoCapture : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate> {

	@public
	CGImageRef currentFrame;

	int width;
	int height;

	BOOL bInitCalled;
	int deviceID;

	AVCaptureDeviceInput		*captureInput;
	AVCaptureVideoDataOutput    *captureOutput;
	AVCaptureDevice				*device;
	AVCaptureSession			*captureSession;

	StableCameraGrabber * grabberPtr;
}

-(BOOL)initCapture:(int)framerate capWidth:(int)w capHeight:(int)h;
-(void)startCapture;
-(void)stopCapture;
-(void)lockExposureAndFocus;
-(std::vector <std::string>)listDevices;
-(void)setDevice:(int)_device;
-(void)eraseGrabberPtr;

-(CGImageRef)getCurrentFrame;

@end

//------

#endif


class StableCameraGrabber : virtual public ofBaseVideoGrabber{

	public:
		StableCameraGrabber();
		~StableCameraGrabber();

		void setDeviceID(int deviceID);
		void setDesiredFrameRate(int capRate);
		bool setPixelFormat(ofPixelFormat PixelFormat);

        bool setup(int w, int h);
		void update();
		bool isFrameNew() const;
		void close();

		ofPixels&		 		getPixels();
        const ofPixels&		    getPixels() const;

		float getWidth() const{
			return width;
		}
		float getHeight() const{
			return height;
		}

        bool isInitialized() const;

		void updatePixelsCB();
		std::vector <ofVideoDevice> listDevices() const;
		ofPixelFormat getPixelFormat() const;

	protected:
		bool newFrame = false;
		bool bHavePixelsChanged = false;
		void clear();
		int width, height;

		int device = 0;
        bool bIsInit = false;

		int fps  = -1;
		ofTexture tex;
		ofPixels pixels;

		#ifdef __OBJC__
			GuxStableVideoCapture * grabber;
		#else
			void * grabber;
		#endif

	public:
		ofPixelFormat pixelFormat;
		ofPixels pixelsTmp;
		bool bLock = false;
		std::mutex capMutex;

};
