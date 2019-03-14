#pragma once

#include "Metrics.hpp"
#include "Logger.hpp"
#include "Status.hpp"
#include "Utilities.hpp"
#include "FFmpegDriver.hpp"

#include <list>

#include "SDL.h"

#include "opencv2/imgproc.hpp"

using namespace std;
using namespace cv;

namespace YerFace {

#define YERFACE_FRAMESERVER_MAX_QUEUEDEPTH 200

class VideoFrame;

#define FRAME_STATUS_MAX 7
enum WorkingFrameStatus: unsigned int {
	FRAME_STATUS_NEW = 0, //Frame has just been inserted via insertNewFrame() but no processing has taken place yet.
	FRAME_STATUS_DETECTION = 1, //Primary face rectangle is being identified by FaceDetector
	FRAME_STATUS_TRACKING = 2, //Face raw landmarks and pose are being recovered by FaceTracker
	FRAME_STATUS_MAPPING = 3, //Primary markers position is being recovered by FaceMapper and its children.
	FRAME_STATUS_PREVIEWING = 4, //Frame is being previewed.
	FRAME_STATUS_LATE_PROCESSING = 5, //Frame is eligible for any late-stage processing (like Sphinx data).
	FRAME_STATUS_DRAINING = 6, //Last call before this frame is gone. (Frame data output)
	FRAME_STATUS_GONE = 7 //This frame is about to be freed and purged from the frame store. (No checkpoints can be registered for this status!)
};

class WorkingFrame {
public:
	Mat frame; //BGR format, at the native resolution of the input.
	Mat detectionFrame; //BGR, scaled down to DetectionScaleFactor.
	double detectionScaleFactor;
	Mat previewFrame; //BGR, same as the input frame, but possibly with some HUD stuff scribbled onto it.
	SDL_mutex *previewFrameMutex; //IMPORTANT - make sure you lock previewFrameMutex before WRITING TO or READING FROM previewFrame.
	FrameTimestamps frameTimestamps;

	WorkingFrameStatus status;
	unordered_map<string, bool> checkpoints[FRAME_STATUS_MAX + 1];
};

class FrameStatusChangeEventCallback {
public:
	WorkingFrameStatus newStatus;
	void *userdata;
	function<void(void *userdata, WorkingFrameStatus newStatus, FrameTimestamps frameTimestamps)> callback;
};

class FrameServerDrainedEventCallback {
public:
	void *userdata;
	function<void(void *userdata)> callback;
};

class FrameServer {
public:
	FrameServer(json config, Status *myStatus, bool myLowLatency);
	~FrameServer() noexcept(false);
	void setDraining(void);
	void onFrameServerDrainedEvent(FrameServerDrainedEventCallback callback);
	void onFrameStatusChangeEvent(FrameStatusChangeEventCallback callback);
	void registerFrameStatusCheckpoint(WorkingFrameStatus status, string checkpointKey);
	void insertNewFrame(VideoFrame *videoFrame);
	WorkingFrame *getWorkingFrame(FrameNumber frameNumber);
	void setWorkingFrameStatusCheckpoint(FrameNumber frameNumber, WorkingFrameStatus status, string checkpointKey);

private:
	bool isDrained(void);
	void destroyFrame(FrameNumber frameNumber);
	void setFrameStatus(FrameTimestamps frameTimestamps, WorkingFrameStatus newStatus);
	void checkStatusValue(WorkingFrameStatus status);
	static int frameHerderLoop(void *ptr);

	Status *status;
	bool lowLatency;
	bool draining;
	int detectionBoundingBox;
	double detectionScaleFactor;
	Logger *logger;
	SDL_mutex *myMutex;
	SDL_cond *myCond;
	Metrics *metrics;
	Size frameSize;
	bool frameSizeSet;

	SDL_Thread *herderThread;

	unordered_map<FrameNumber, WorkingFrame *> frameStore;

	std::vector<FrameStatusChangeEventCallback> onFrameStatusChangeCallbacks[FRAME_STATUS_MAX + 1];
	std::vector<string> statusCheckpoints[FRAME_STATUS_MAX + 1];

	std::vector<FrameServerDrainedEventCallback> onFrameServerDrainedCallbacks;
};

}; //namespace YerFace
