#pragma once

#include "ofMain.h"

#include <array>
#include <cmath>

namespace gux {

// 背景のグラデーションの設定。
struct BackgroundGradientSettings {
  bool enabled = true;
  float depth = 0.08f;  // にじみの中心をどれだけ暗くするか（0.08 で白255 → 約235）
  float speed = 1.0f;   // 漂う速さの倍率
};

// 白い背景の上に、オフホワイトのにじみを4つ漂わせる。
// にじみはぼかした楕円で、重なっても暗くなりすぎないように合成する。
// 時間は Scene の経過秒を使うので、書き出しでも同じ動きになる。
class BackgroundGradientPass {
 public:
  BackgroundGradientPass() = default;
  // 書き出しでは Scene ごとコピーされる。GPU資源は共有せず、コピー先で作り直す。
  BackgroundGradientPass(const BackgroundGradientPass&) {}
  BackgroundGradientPass& operator=(const BackgroundGradientPass&) { return *this; }

  void draw(int canvasWidth, int canvasHeight, float seconds,
            const ofColor& background, const BackgroundGradientSettings& settings) {
    if (!settings.enabled || settings.depth <= 0.0f) return;
    if (canvasWidth <= 0 || canvasHeight <= 0 || !ensureShader()) return;

    const float width = static_cast<float>(canvasWidth);
    const float height = static_cast<float>(canvasHeight);
    // にじみの大きさは画面の面積に合わせる。横長の画面でも大きさが偏らない。
    const float size = std::sqrt(width * height);
    const float t = seconds * settings.speed;

    std::array<float, kBlobCount * 4> shape{};  // 中心x, 中心y, 半径x, 半径y
    std::array<float, kBlobCount * 2> extra{};  // 角度, 濃さ
    for (int i = 0; i < kBlobCount; ++i) {
      const Blob& blob = kBlobs[i];
      const float phase = blob.phase;
      const float x = blob.x + 0.08f * std::sin(t * TWO_PI / blob.period + phase);
      const float y = blob.y + 0.08f * std::cos(t * TWO_PI / (blob.period * 1.3f) + phase * 1.7f);
      const float breathe = 1.0f + 0.1f * std::sin(t * TWO_PI / (blob.period * 0.8f) + phase * 2.3f);
      const float angle = blob.angle + 0.2f * std::sin(t * TWO_PI / (blob.period * 1.6f) + phase);
      shape[i * 4 + 0] = x * width;
      shape[i * 4 + 1] = y * height;
      shape[i * 4 + 2] = blob.radiusX * size * breathe;
      shape[i * 4 + 3] = blob.radiusY * size * breathe;
      extra[i * 2 + 0] = angle;
      extra[i * 2 + 1] = blob.strength;
    }

    ofPushStyle();
    ofFill();
    ofSetColor(255);
    shader.begin();
    shader.setUniform4fv("blobShape", shape.data(), kBlobCount);
    shader.setUniform2fv("blobExtra", extra.data(), kBlobCount);
    shader.setUniform3f("baseColor", background.r / 255.0f, background.g / 255.0f,
                        background.b / 255.0f);
    shader.setUniform1f("depth", settings.depth);
    ofDrawRectangle(0.0f, 0.0f, width, height);
    shader.end();
    ofPopStyle();
  }

 private:
  struct Blob {
    float x, y;              // 画面に対する中心（0〜1）
    float radiusX, radiusY;  // 画面の大きさに対する半径
    float angle;             // 楕円の向き（ラジアン）
    float strength;          // 濃さ（0〜1）
    float period;            // 漂う周期（秒）
    float phase;
  };
  static constexpr int kBlobCount = 4;
  static constexpr Blob kBlobs[kBlobCount] = {
      {0.48f, 0.32f, 0.20f, 0.13f, -0.15f, 1.00f, 47.0f, 0.0f},
      {0.30f, 0.66f, 0.18f, 0.11f, 0.10f, 0.95f, 61.0f, 2.1f},
      {0.62f, 0.58f, 0.26f, 0.18f, 0.25f, 0.55f, 53.0f, 4.0f},
      {0.82f, 0.38f, 0.14f, 0.19f, -0.40f, 0.45f, 71.0f, 5.3f},
  };

  bool ensureShader() {
    if (shaderReady) return true;
    if (shaderFailed) return false;
    const std::string vertex = R"(
      #version 120
      varying vec2 canvasPosition;
      void main() {
        canvasPosition = gl_Vertex.xy;
        gl_Position = ftransform();
      }
    )";
    const std::string fragment = R"(
      #version 120
      uniform vec4 blobShape[4];
      uniform vec2 blobExtra[4];
      uniform vec3 baseColor;
      uniform float depth;
      varying vec2 canvasPosition;

      float hash12(vec2 p) {
        vec3 p3 = fract(vec3(p.xyx) * 0.1031);
        p3 += dot(p3, p3.yzx + 33.33);
        return fract((p3.x + p3.y) * p3.z);
      }

      void main() {
        // 重なった所が暗くなりすぎないよう、「どれにも覆われない割合」を掛け合わせる。
        float uncovered = 1.0;
        for (int i = 0; i < 4; ++i) {
          vec2 d = canvasPosition - blobShape[i].xy;
          float c = cos(blobExtra[i].x);
          float s = sin(blobExtra[i].x);
          vec2 q = vec2(c * d.x + s * d.y, -s * d.x + c * d.y) / blobShape[i].zw;
          float blob = exp(-0.5 * dot(q, q)) * blobExtra[i].y;
          uncovered *= 1.0 - blob;
        }
        float amount = (1.0 - uncovered) * depth;
        // 青をわずかに残し、ほんの少しだけ冷たいオフホワイトにする。
        vec3 color = baseColor - amount * vec3(1.0, 1.0, 0.9);
        // 8bit で縞が出ないよう、1段階未満のノイズを足す。
        color += (hash12(gl_FragCoord.xy) - 0.5) / 255.0;
        gl_FragColor = vec4(color, 1.0);
      }
    )";
    shaderReady = shader.setupShaderFromSource(GL_VERTEX_SHADER, vertex) &&
                  shader.setupShaderFromSource(GL_FRAGMENT_SHADER, fragment) &&
                  shader.linkProgram();
    if (!shaderReady) {
      shaderFailed = true;
      ofLogError("BackgroundGradientPass")
          << "Background shader failed. Background stays flat.";
    }
    return shaderReady;
  }

  ofShader shader;
  bool shaderReady = false;
  bool shaderFailed = false;
};

}  // namespace gux
