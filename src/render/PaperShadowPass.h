#pragma once

#include "ofMain.h"

#include <algorithm>
#include <cmath>

namespace gux {

// 色紙が背景からわずかに浮いて見えるよう、紙の下に影を落とす設定。
// 長さはキャンバスの高さに比例させ、書き出し解像度が変わっても見た目を揃える。
struct PaperShadowSettings {
  bool enabled = true;
  float lift = 1.0f;           // 浮き量。影のずれ・ぼかしをまとめて変える
  float angleDegrees = 60.0f;  // 影が落ちる向き（0 = 右, 90 = 真下）
  float opacity = 1.0f;        // 影の濃さの倍率
};

// Recipe が描いた紙の形（アルファ）から、2層の影を作って先に描く。
//   接地影: 紙のすぐ下の狭く濃い影。厚みと接地感を出す
//   環境影: 広く薄い影。紙が少し浮いている距離感を出す
// 影は 1/2 解像度で作り、縦横2回に分けてぼかす。
class PaperShadowPass {
 public:
  PaperShadowPass() = default;
  // 書き出しでは Scene ごとコピーされる。GPU資源は共有せず、コピー先で作り直す。
  PaperShadowPass(const PaperShadowPass&) {}
  PaperShadowPass& operator=(const PaperShadowPass&) { return *this; }

  template <class DrawShapes>
  void draw(int canvasWidth, int canvasHeight,
            const PaperShadowSettings& settings, DrawShapes&& drawShapes) {
    if (!settings.enabled || settings.lift <= 0.0f || settings.opacity <= 0.0f) return;
    if (canvasWidth <= 0 || canvasHeight <= 0) return;
    if (!ensureShader()) return;

    const float unit = static_cast<float>(canvasHeight) * settings.lift;
    const Layer contact{unit * 0.0010f, std::max(unit * 0.0012f, 0.6f), 0.30f};
    const Layer ambient{unit * 0.0055f, std::max(unit * 0.0075f, 1.0f), 0.16f};

    // 画面端の紙の影も切れないよう、ぼかしの広がり分だけ余白を取る。
    const float pad = std::ceil(ambient.offset + ambient.sigma * 3.0f) + 2.0f;
    const int fboWidth = static_cast<int>(
        std::ceil((canvasWidth + pad * 2.0f) * kScale));
    const int fboHeight = static_cast<int>(
        std::ceil((canvasHeight + pad * 2.0f) * kScale));
    ensureFbos(fboWidth, fboHeight);

    // 1. 紙の形を描く。色は使わず、アルファだけを影の元にする。
    source.begin();
    ofClear(0, 0, 0, 0);
    ofPushMatrix();
    ofScale(kScale, kScale);
    ofTranslate(pad, pad);
    drawShapes();
    ofPopMatrix();
    source.end();

    // 2. 2層それぞれをぼかす。上書きしたいのでブレンドは切る。
    ofPushStyle();
    ofDisableBlendMode();
    blur(contact.sigma * kScale, contactResult);
    blur(ambient.sigma * kScale, ambientResult);
    ofPopStyle();

    // 3. 紙より先に、広い影 → 狭い影の順で重ねる。
    //    透明書き出しでもアルファが二重に掛からないよう、アルファは別の式で合成する。
    const float radians = ofDegToRad(settings.angleDegrees);
    const glm::vec2 direction(std::cos(radians), std::sin(radians));
    const float drawWidth = fboWidth / kScale;
    const float drawHeight = fboHeight / kScale;

    ofPushStyle();
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    const auto composite = [&](const ofFbo& result, const Layer& layer) {
      const float alpha = ofClamp(layer.alpha * settings.opacity, 0.0f, 1.0f);
      ofSetColor(0, 0, 0, static_cast<int>(std::round(alpha * 255.0f)));
      const glm::vec2 offset = direction * layer.offset;
      result.draw(offset.x - pad, offset.y - pad, drawWidth, drawHeight);
    };
    composite(ambientResult, ambient);
    composite(contactResult, contact);
    ofPopStyle();
    ofEnableBlendMode(OF_BLENDMODE_ALPHA);
  }

 private:
  struct Layer {
    float offset;  // キャンバス px
    float sigma;   // キャンバス px
    float alpha;
  };

  static constexpr float kScale = 0.5f;
  static constexpr int kTaps = 16;  // 片側のサンプル数（シェーダーと合わせる）

  bool ensureShader() {
    if (shaderReady) return true;
    if (shaderFailed) return false;
    const std::string vertex = R"(
      #version 120
      void main() {
        gl_TexCoord[0] = gl_MultiTexCoord0;
        gl_Position = ftransform();
      }
    )";
    const std::string fragment = R"(
      #version 120
      uniform sampler2D src;
      uniform vec2 stepUV;
      uniform float sigmaTaps;
      void main() {
        vec2 uv = gl_TexCoord[0].st;
        float sum = 0.0;
        float weightSum = 0.0;
        float denominator = 2.0 * sigmaTaps * sigmaTaps;
        for (int i = -16; i <= 16; ++i) {
          float x = float(i);
          float weight = exp(-(x * x) / denominator);
          sum += texture2D(src, uv + stepUV * x).a * weight;
          weightSum += weight;
        }
        gl_FragColor = vec4(1.0, 1.0, 1.0, sum / weightSum);
      }
    )";
    shaderReady = blurShader.setupShaderFromSource(GL_VERTEX_SHADER, vertex) &&
                  blurShader.setupShaderFromSource(GL_FRAGMENT_SHADER, fragment) &&
                  blurShader.linkProgram();
    if (!shaderReady) {
      shaderFailed = true;
      ofLogError("PaperShadowPass") << "Blur shader failed. Paper shadow is disabled.";
    }
    return shaderReady;
  }

  void ensureFbos(int width, int height) {
    if (source.isAllocated() && source.getWidth() == width &&
        source.getHeight() == height) {
      return;
    }
    ofFboSettings settings;
    settings.width = width;
    settings.height = height;
    settings.internalformat = GL_RGBA;
    settings.textureTarget = GL_TEXTURE_2D;
    settings.useDepth = false;
    settings.useStencil = false;
    settings.numSamples = 0;
    settings.minFilter = GL_LINEAR;
    settings.maxFilter = GL_LINEAR;
    settings.wrapModeHorizontal = GL_CLAMP_TO_EDGE;
    settings.wrapModeVertical = GL_CLAMP_TO_EDGE;
    for (ofFbo* fbo : {&source, &scratch, &contactResult, &ambientResult}) {
      fbo->allocate(settings);
      fbo->begin();
      ofClear(0, 0, 0, 0);
      fbo->end();
    }
  }

  // sigma は FBO の px。サンプル間隔を広げて、33サンプルで ±3σ を覆う。
  void blur(float sigma, ofFbo& destination) {
    const float stepPx = std::max(sigma * 3.0f / kTaps, 0.25f);
    const float sigmaTaps = std::max(sigma / stepPx, 0.5f);
    pass(source, scratch, glm::vec2(stepPx / source.getWidth(), 0.0f), sigmaTaps);
    pass(scratch, destination, glm::vec2(0.0f, stepPx / scratch.getHeight()), sigmaTaps);
  }

  void pass(ofFbo& from, ofFbo& to, const glm::vec2& stepUV, float sigmaTaps) {
    to.begin();
    ofClear(0, 0, 0, 0);
    blurShader.begin();
    blurShader.setUniformTexture("src", from.getTexture(), 0);
    blurShader.setUniform2f("stepUV", stepUV);
    blurShader.setUniform1f("sigmaTaps", sigmaTaps);
    ofSetColor(255);
    from.draw(0, 0);
    blurShader.end();
    to.end();
  }

  ofShader blurShader;
  bool shaderReady = false;
  bool shaderFailed = false;
  ofFbo source;
  ofFbo scratch;
  ofFbo contactResult;
  ofFbo ambientResult;
};

}  // namespace gux
