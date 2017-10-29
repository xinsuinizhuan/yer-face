
#include "opencv2/objdetect.hpp"
#include "opencv2/videoio.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/tracking.hpp"

// best available resolution / rate: ffplay -pixel_format mjpeg -video_size 1920x1080 /dev/video1
// best recording solution: ffmpeg -framerate 30 -y -f video4linux2 -pixel_format mjpeg -video_size 1920x1080 -i /dev/video0 -f pulse -i default -acodec copy -vcodec copy /tmp/output.mkv

#include "FaceTracker.hpp"
#include "FrameDerivatives.hpp"
#include "Metrics.hpp"

#include <iostream>
#include <stdio.h>

using namespace std;
using namespace cv;
using namespace YerFace;

String face_cascade_name;
String eyes_cascade_name;
String capture_file;
String window_name = "Performance Capture Tests";

double imageScaleFactor = 0.5;

FrameDerivatives *frameDerivatives;
FaceTracker *faceTracker;
Metrics *metrics;
unsigned long frameNum = 0;

int main( int argc, const char** argv ) {
	//Command line options.
	CommandLineParser parser(argc, argv,
		"{help h||}"
		"{face_cascade|/usr/local/share/OpenCV/haarcascades/haarcascade_frontalface_default.xml|}"
		"{eyes_cascade|/usr/local/share/OpenCV/haarcascades/haarcascade_eye.xml|}"
		"{capture_file|/dev/video0|}");

	parser.about("Yer Face: The butt of all the jokes. (A stupid facial performance capture engine for cartoon animation.)");
	if(parser.get<bool>("help")) {
		parser.printMessage();
		return 1;
	}

	//A couple of OpenCV classes.
	face_cascade_name = parser.get<string>("face_cascade");
	eyes_cascade_name = parser.get<string>("eyes_cascade");
	capture_file = parser.get<string>("capture_file");
	VideoCapture capture;
	Mat frame;

	//Instantiate our classes.
	frameDerivatives = new FrameDerivatives(imageScaleFactor);
	faceTracker = new FaceTracker(face_cascade_name, frameDerivatives);

	//Open the video stream.
	capture.open(capture_file);
	if(!capture.isOpened()) {
		fprintf(stderr, "Failed opening video stream\n");
		return 1;
	}

	metrics = new Metrics(30);

	while(capture.read(frame)) {
		// Start timer
		metrics->startFrame();
		frameNum++;

		if(frame.empty()) {
			fprintf(stderr, "Breaking on no frame ready...\n");
			break;
		}

		frameDerivatives->setCurrentFrame(frame);
		faceTracker->processCurrentFrame();
		faceTracker->renderPreviewHUD();

		metrics->endFrame();

		Mat previewFrame = frameDerivatives->getPreviewFrame();

		//Display some metrics on frame.
		char metricsStringA[256];
		snprintf(metricsStringA, 256, "Times: <Avg %.02fms, Worst %.02fms>", metrics->getAverageTimeSeconds() * 1000.0, metrics->getWorstTimeSeconds() * 1000.0);
		char metricsStringB[256];
		snprintf(metricsStringB, 256, "FPS: <%.02f>", metrics->getFPS());
		//fprintf(stderr, "Frame %lu %s, %s\n", frameNum, metricsStringA, metricsStringB);
		putText(previewFrame, metricsStringA, Point(25,50), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,255), 2);
		putText(previewFrame, metricsStringB, Point(25,75), FONT_HERSHEY_SIMPLEX, 0.75, Scalar(0,0,255), 2);

		//Display preview frame.
		imshow(window_name, previewFrame);
		char c = (char)waitKey(1);
		if(c == 27) {
			fprintf(stderr, "Breaking on user escape...\n");
			break;
		}
	}
	return 0;
}