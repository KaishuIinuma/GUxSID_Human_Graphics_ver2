#pragma once

#include "ofMain.h"

// Detection層が返す、特定のSceneやRendererに依存しない観測データ。
struct HumanContourData {
  vector<ofPolyline> contours;
  vector<vector<glm::vec2>> contourPoints;
  vector<glm::vec2> centroids;
  vector<ofRectangle> boundingBoxes;
  int numHumans = 0;
};
