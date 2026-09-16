#pragma once

#include "core/VisualTypes.h"
#include "ofMain.h"

#include <cmath>

namespace gux {

// RenderRecipeから再利用する、状態を持たない低レベル描画機能。
class ShapePainter {
 public:
  void drawFill(const ofPolyline& polygon, const ofColor& color) const {
    if (polygon.size() < 3) return;
    ofPath path;
    path.setFilled(true);
    path.setFillColor(color);
    path.moveTo(polygon[0]);
    for (size_t i = 1; i < polygon.size(); ++i) path.lineTo(polygon[i]);
    path.close();
    path.draw();
  }

  void drawStroke(const ofPolyline& polygon, const ofColor& color,
                  float strokeWeight, Scene4StrokeJoinType joinType) const {
    if (polygon.size() < 3) return;
    ofSetColor(color);
    ofFill();
    for (size_t i = 0; i < polygon.size(); ++i) {
      const glm::vec2 p1 = polygon[i];
      const glm::vec2 p2 = polygon[(i + 1) % polygon.size()];
      const glm::vec2 previous =
          polygon[(i - 1 + polygon.size()) % polygon.size()];
      const glm::vec2 direction = p2 - p1;
      const float length = glm::length(direction);
      if (length > 0.0f) {
        const float angle = std::atan2(direction.y, direction.x) * RAD_TO_DEG;
        ofPushMatrix();
        ofTranslate(p1.x, p1.y);
        ofRotateDeg(angle);
        ofDrawRectangle(0, -strokeWeight / 2.0f, length, strokeWeight);
        ofPopMatrix();
      }

      if (joinType == Scene4StrokeJoinType::Round) {
        ofDrawCircle(p1, strokeWeight / 2.0f);
        continue;
      }

      const glm::vec2 incoming = glm::normalize(p1 - previous);
      const glm::vec2 outgoing = glm::normalize(p2 - p1);
      const glm::vec2 normal1(-incoming.y, incoming.x);
      const glm::vec2 normal2(-outgoing.y, outgoing.x);
      const glm::vec2 miter = glm::normalize(normal1 + normal2);
      const float dot = glm::dot(normal1, miter);
      if (std::abs(dot) <= 0.05f) continue;

      float miterLength = (strokeWeight / 2.0f) / dot;
      if (miterLength > strokeWeight * 4.0f) {
        miterLength = strokeWeight * 4.0f;
      }
      const glm::vec2 outside = p1 + miter * miterLength;
      const glm::vec2 inside = p1 - miter * miterLength;

      ofMesh outsideJoint;
      outsideJoint.setMode(OF_PRIMITIVE_TRIANGLE_FAN);
      outsideJoint.addVertex(glm::vec3(p1, 0));
      outsideJoint.addVertex(
          glm::vec3(p1 + normal1 * (strokeWeight / 2.0f), 0));
      outsideJoint.addVertex(glm::vec3(outside, 0));
      outsideJoint.addVertex(
          glm::vec3(p1 + normal2 * (strokeWeight / 2.0f), 0));
      outsideJoint.draw();

      ofMesh insideJoint;
      insideJoint.setMode(OF_PRIMITIVE_TRIANGLE_FAN);
      insideJoint.addVertex(glm::vec3(p1, 0));
      insideJoint.addVertex(
          glm::vec3(p1 - normal1 * (strokeWeight / 2.0f), 0));
      insideJoint.addVertex(glm::vec3(inside, 0));
      insideJoint.addVertex(
          glm::vec3(p1 - normal2 * (strokeWeight / 2.0f), 0));
      insideJoint.draw();
    }
  }
};

}  // namespace gux
