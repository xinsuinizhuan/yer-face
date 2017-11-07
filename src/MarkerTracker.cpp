
#include "MarkerType.hpp"
#include "MarkerTracker.hpp"
#include "Utilities.hpp"

#include <iostream>
#include <cstdlib>

using namespace std;
using namespace cv;

namespace YerFace {

MarkerTracker::MarkerTracker(MarkerType myMarkerType, FrameDerivatives *myFrameDerivatives, MarkerSeparator *myMarkerSeparator, EyeTracker *myEyeTracker, float myTrackingBoxPercentage, float myMaxTrackerDriftPercentage) {
	markerType = MarkerType(myMarkerType);

	if(markerType.type == NoMarkerAssigned) {
		throw invalid_argument("MarkerTracker class cannot be assigned NoMarkerAssigned");
	}
	size_t markerTrackersCount = markerTrackers.size();
	for(size_t i = 0; i < markerTrackersCount; i++) {
		if(markerTrackers[i]->getMarkerType().type == markerType.type) {
			fprintf(stderr, "Trying to construct MarkerTracker <%s> object, but one already exists!\n", markerType.toString());
			throw invalid_argument("MarkerType collision trying to construct MarkerTracker");
		}
	}
	markerTrackers.push_back(this);

	frameDerivatives = myFrameDerivatives;
	if(frameDerivatives == NULL) {
		throw invalid_argument("frameDerivatives cannot be NULL");
	}
	markerSeparator = myMarkerSeparator;
	if(markerSeparator == NULL) {
		throw invalid_argument("markerSeparator cannot be NULL");
	}
	eyeTracker = myEyeTracker;
	if(eyeTracker == NULL) {
		if(markerType.type == EyelidLeftTop || markerType.type == EyelidLeftBottom || markerType.type == EyelidRightTop || markerType.type == EyelidRightBottom) {
			throw invalid_argument("eyeTracker cannot be NULL if markerType is one of the Eyelids");
		}
	} else {
		if(markerType.type == EyelidLeftTop || markerType.type == EyelidLeftBottom) {
			if(eyeTracker->getWhichEye() != LeftEye) {
				throw invalid_argument("eyeTracker must be a LeftEye if markerType is one of the Left Eyelids");
			}
		} else if(markerType.type == EyelidRightTop || markerType.type == EyelidRightBottom) {
			if(eyeTracker->getWhichEye() != RightEye) {
				throw invalid_argument("eyeTracker must be a RightEye if markerType is one of the Right Eyelids");
			}
		} else {
			throw invalid_argument("eyeTracker should be NULL if markerType is not one of the Eyelids");
		}
	}

	trackingBoxPercentage = myTrackingBoxPercentage;
	if(trackingBoxPercentage <= 0.0) {
		throw invalid_argument("trackingBoxPercentage cannot be less than or equal to zero");
	}
	maxTrackerDriftPercentage = myMaxTrackerDriftPercentage;
	if(maxTrackerDriftPercentage <= 0.0) {
		throw invalid_argument("maxTrackerDriftPercentage cannot be less than or equal to zero");
	}

	trackerState = DETECTING;
	markerDetectedSet = false;
	markerPointSet = false;
	trackingBoxSet = false;
	markerList = NULL;

	fprintf(stderr, "MarkerTracker <%s> object constructed and ready to go!\n", markerType.toString());
}

MarkerTracker::~MarkerTracker() {
	fprintf(stderr, "MarkerTracker <%s> object destructing...\n", markerType.toString());
	for(vector<MarkerTracker *>::iterator iterator = markerTrackers.begin(); iterator != markerTrackers.end(); ++iterator) {
		if(*iterator == this) {
			markerTrackers.erase(iterator);
			return;
		}
	}
}

MarkerType MarkerTracker::getMarkerType(void) {
	return markerType;
}

TrackerState MarkerTracker::processCurrentFrame(void) {
	performTracking();

	markerDetectedSet = false;
	markerList = markerSeparator->getMarkerList();
	
	performTrackToSeparatedCorrelation();

	if(!markerDetectedSet) {
		performDetection();
	}

	if(markerDetectedSet) {
		if(!trackingBoxSet || trackerDriftingExcessively()) {
			performInitializationOfTracker();
		}
	}
	
	assignMarkerPoint();

	return trackerState;
}

void MarkerTracker::performTrackToSeparatedCorrelation(void) {
	if(markerList == NULL) {
		throw invalid_argument("MarkerTracker::performTrackToSeparatedCorrelation() called while markerList is NULL");
	}
	if((*markerList).size() < 1) {
		return;
	}
	if(!trackingBoxSet) {
		return;
	}
	Point2d trackingBoxCenter = Utilities::centerRect(trackingBox);
	list<MarkerCandidate> markerCandidateList;
	generateMarkerCandidateList(&markerCandidateList, trackingBoxCenter, &trackingBox);
	if(markerCandidateList.size() <= 0) {
		return;
	}
	if(!attemptToClaimMarkerCandidate(markerCandidateList.front())) {
		return;
	}

}

void MarkerTracker::performDetection(void) {
	if(markerList == NULL) {
		throw invalid_argument("MarkerTracker::performDetection() called while markerList is NULL");
	}
	if((*markerList).size() < 1) {
		return;
	}

	if(markerType.type == EyelidLeftTop || markerType.type == EyelidLeftBottom || markerType.type == EyelidRightTop || markerType.type == EyelidRightBottom) {
		tuple<Rect2d, bool> eyeRectTuple = eyeTracker->getEyeRect();
		Rect2d eyeRect = get<0>(eyeRectTuple);
		bool eyeRectSet = get<1>(eyeRectTuple);
		if(!eyeRectSet) {
			return;
		}
		Point2d eyeRectCenter = Utilities::centerRect(eyeRect);

		list<MarkerCandidate> markerCandidateList;
		generateMarkerCandidateList(&markerCandidateList, eyeRectCenter, &eyeRect);

		if(markerCandidateList.size() == 1) {
			if(markerType.type == EyelidLeftBottom || markerType.type == EyelidRightBottom) {
				return;
			}
			if(!attemptToClaimMarkerCandidate(markerCandidateList.front())) {
				return;
			}
		} else if(markerCandidateList.size() > 1) {
			list<MarkerCandidate>::iterator markerCandidateIterator = markerCandidateList.begin();
			MarkerCandidate markerCandidateA = *markerCandidateIterator;
			++markerCandidateIterator;
			MarkerCandidate markerCandidateB = *markerCandidateIterator;
			if(markerCandidateB.marker.center.y < markerCandidateA.marker.center.y) {
				if(markerType.type == EyelidLeftTop || markerType.type == EyelidRightTop) {
					if(!attemptToClaimMarkerCandidate(markerCandidateB)) {
						return;
					}
				} else {
					if(!attemptToClaimMarkerCandidate(markerCandidateA)) {
						return;
					}
				}
			} else {
				if(markerType.type == EyelidLeftTop || markerType.type == EyelidRightTop) {
					if(!attemptToClaimMarkerCandidate(markerCandidateA)) {
						return;
					}
				} else {
					if(!attemptToClaimMarkerCandidate(markerCandidateB)) {
						return;
					}
				}
			}
		} else {
			return;
		}
	}
}

void MarkerTracker::performInitializationOfTracker(void) {
	if(!markerDetectedSet) {
		throw invalid_argument("MarkerTracker::performInitializationOfTracker() called while markerDetectedSet is false");
	}
	trackerState = TRACKING;
	#if (CV_MINOR_VERSION < 3)
	tracker = Tracker::create("KCF");
	#else
	tracker = TrackerKCF::create();
	#endif
	trackingBox = Rect(Utilities::insetBox(markerDetected.marker.boundingRect2f(), trackingBoxPercentage));
	trackingBoxSet = true;

	tracker->init(frameDerivatives->getCurrentFrame(), trackingBox);
}

bool MarkerTracker::performTracking(void) {
	if(trackerState == TRACKING) {
		bool trackSuccess = tracker->update(frameDerivatives->getCurrentFrame(), trackingBox);
		if(!trackSuccess) {
			trackingBoxSet = false;
			return false;
		}
		trackingBoxSet = true;
		return true;
	}
	return false;
}

bool MarkerTracker::trackerDriftingExcessively(void) {
	if(!markerDetectedSet || !trackingBoxSet) {
		throw invalid_argument("MarkerTracker::trackerDriftingExcessively() called while one or both of markerDetectedSet or trackingBoxSet are false");
	}
	double actualDistance = Utilities::distance(markerDetected.marker.center, Utilities::centerRect(trackingBox));
	double maxDistance = markerDetected.sqrtArea * maxTrackerDriftPercentage;
	if(actualDistance > maxDistance) {
		fprintf(stderr, "MarkerTracker <%s>: WARNING: Optical tracker drifting excessively! Resetting it.\n", markerType.toString());
		return true;
	}
	return false;
}

bool MarkerTracker::attemptToClaimMarkerCandidate(MarkerCandidate markerCandidate) {
	if(markerList == NULL) {
		throw invalid_argument("MarkerTracker::attemptToClaimMarkerCandidate() called while markerList is NULL");
	}
	size_t markerListCount = (*markerList).size();
	if(markerCandidate.markerListIndex >= markerListCount) {
		throw invalid_argument("MarkerTracker::attemptToClaimMarkerCandidate() called with a markerCandidate whose index is outside the bounds of markerList");
	}
	MarkerSeparated *markerSeparatedCandidate = &(*markerList)[markerCandidate.markerListIndex];
	if(markerSeparatedCandidate->assignedType.type != NoMarkerAssigned) {
		fprintf(stderr, "MarkerTracker <%s>: WARNING: Attempted to claim marker %u but it was already assigned type <%s>.\n", markerType.toString(), markerCandidate.markerListIndex, markerSeparatedCandidate->assignedType.toString());
		return false;
	}
	markerSeparatedCandidate->assignedType.type = markerType.type;
	markerDetected = markerCandidate;
	markerDetectedSet = true;
	return true;
}

void MarkerTracker::assignMarkerPoint(void) {
	markerPointSet = false;
	if(markerDetectedSet && trackingBoxSet) {
		Point2d detectedPoint = Point(markerDetected.marker.center);
		Point2d trackingPoint = Point(Utilities::centerRect(trackingBox));
		double actualDistance = Utilities::distance(detectedPoint, trackingPoint);
		double maxDistance = markerDetected.sqrtArea * maxTrackerDriftPercentage;
		double detectedPointWeight = actualDistance / maxDistance;
		if(detectedPointWeight < 0.0) {
			detectedPointWeight = 0.0;
		} else if(detectedPointWeight > 1.0) {
			detectedPointWeight = 1.0;
		}
		double trackingPointWeight = 1.0 - detectedPointWeight;
		detectedPoint.x = detectedPoint.x * detectedPointWeight;
		detectedPoint.y = detectedPoint.y * detectedPointWeight;
		trackingPoint.x = trackingPoint.x * trackingPointWeight;
		trackingPoint.y = trackingPoint.y * trackingPointWeight;
		markerPoint = detectedPoint + trackingPoint;
		markerPointSet = true;
	} else if(markerDetectedSet) {
		markerPoint = markerDetected.marker.center;
		markerPointSet = true;
	} else if(trackingBoxSet) {
		markerPoint = Utilities::centerRect(trackingBox);
		markerPointSet = true;
	} else {
		trackerState = LOST;
		fprintf(stderr, "MarkerTracker <%s> Lost marker completely! Will keep searching...\n", markerType.toString());
	}	
}

void MarkerTracker::generateMarkerCandidateList(list<MarkerCandidate> *markerCandidateList, Point2d pointOfInterest, Rect2d *boundingRect) {
	if(markerList == NULL) {
		throw invalid_argument("MarkerTracker::generateMarkerCandidateList() called while markerList is NULL");
	}
	if(markerCandidateList == NULL) {
		throw invalid_argument("MarkerTracker::generateMarkerCandidateList() called with NULL markerCandidateList");
	}
	MarkerCandidate markerCandidate;
	size_t markerListCount = (*markerList).size();
	for(size_t i = 0; i < markerListCount; i++) {
		MarkerSeparated markerSeparated = (*markerList)[i];
		if(!markerSeparated.active) {
			continue;
		}
		RotatedRect marker = markerSeparated.marker;
		Rect2d markerRect = Rect(marker.boundingRect2f());
		if(boundingRect == NULL || (markerRect & (*boundingRect)).area() > 0) {
			markerCandidate.marker = marker;
			markerCandidate.markerListIndex = i;
			markerCandidate.distanceFromPointOfInterest = Utilities::distance(pointOfInterest, markerCandidate.marker.center);
			markerCandidate.sqrtArea = std::sqrt((double)(markerCandidate.marker.size.width * markerCandidate.marker.size.height));
			markerCandidateList->push_back(markerCandidate);
		}
	}
	markerCandidateList->sort(sortMarkerCandidatesByDistanceFromPointOfInterest);
}

bool MarkerTracker::sortMarkerCandidatesByDistanceFromPointOfInterest(const MarkerCandidate a, const MarkerCandidate b) {
	return (a.distanceFromPointOfInterest < b.distanceFromPointOfInterest);
}

void MarkerTracker::renderPreviewHUD(bool verbose) {
	Scalar color = Scalar(0, 0, 255);
	if(markerType.type == EyelidLeftBottom || markerType.type == EyelidRightBottom || markerType.type == EyelidLeftTop || markerType.type == EyelidRightTop) {
		color = Scalar(0, 255, 255);
		if(markerType.type == EyelidLeftTop || markerType.type == EyelidRightTop) {
			color[1] -= 128;
		}
	}
	Mat frame = frameDerivatives->getPreviewFrame();
	if(verbose) {
		if(trackingBoxSet) {
			rectangle(frame, trackingBox, color, 1);
		}
		if(markerDetectedSet) {
			Utilities::drawRotatedRectOutline(frame, markerDetected.marker, color, 1);
		}
	}
	if(markerPointSet) {
		Utilities::drawX(frame, markerPoint, color, 10, 2);
	}
}

TrackerState MarkerTracker::getTrackerState(void) {
	return trackerState;
}

tuple<Point2d, bool> MarkerTracker::getMarkerPoint(void) {
	return make_tuple(markerPoint, markerPointSet);
}

vector<MarkerTracker *> MarkerTracker::markerTrackers;

vector<MarkerTracker *> *MarkerTracker::getMarkerTrackers(void) {
	return &markerTrackers;
}

}; //namespace YerFace
