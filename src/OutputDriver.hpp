#pragma once

#include "Logger.hpp"
#include "FrameDerivatives.hpp"
#include "FaceTracker.hpp"
#include "MarkerTracker.hpp"

using namespace std;

namespace YerFace {

//FIXME - we don't want to use named pipes ultimately -- this is just a thin slice to get our data into blender quickly
#define OUTPUTDRIVER_NAMED_PIPE "/tmp/yerface"

class OutputDriver {
public:
	OutputDriver(FrameDerivatives *myFrameDerivatives, FaceTracker *myFaceTracker);
	~OutputDriver();
	void handleCompletedFrame(void);
private:
	FrameDerivatives *frameDerivatives;
	FaceTracker *faceTracker;
	Logger *logger;

	bool autoBasisTransmitted;

	int pipeHandle;
};

}; //namespace YerFace
