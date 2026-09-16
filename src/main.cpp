#include "ofApp.h"

int main() {
  // ofGLWindowSettings ではなく ofGLFWWindowSettings に変更します
  ofGLFWWindowSettings settings;
  settings.setSize(1920, 1080);
  settings.title = "Main Window";
  settings.windowMode = OF_WINDOW;
  auto mainWindow = ofCreateWindow(settings);

  // GUIウィンドウ側も ofGLFWWindowSettings に変更します
  // ★修正: サイズをofApp::controlWindowWidth/Heightから取得するようにした。
  //   (ofAppのインスタンス生成前なので、ofApp::の静的メンバとして参照する)
  ofGLFWWindowSettings guiSettings;
  guiSettings.setSize(ofApp::controlWindowWidth, ofApp::controlWindowHeight);
  guiSettings.title = "Controls";
  guiSettings.shareContextWith = mainWindow; // ofGLFWWindowSettings で使用可能になります
  auto guiWindow = ofCreateWindow(guiSettings);

  auto mainApp = std::make_shared<ofApp>();

  // ofApp側のguiWindowメンバ変数に参照をセット
  mainApp->guiWindow = std::dynamic_pointer_cast<ofAppGLFWWindow>(guiWindow);

  // ★追加: メインウィンドウへの参照も保持しておく
  //   (動画読み込み時にウィンドウサイズを動画の解像度・縦横比へ
  //    合わせてリサイズするために使用する)
  mainApp->mainWindow = std::dynamic_pointer_cast<ofAppGLFWWindow>(mainWindow);

  // GUIウィンドウの描画イベントを ofApp::drawGui に紐付け
  ofAddListener(guiWindow->events().draw, mainApp.get(), &ofApp::drawGui);

  // ★追加: Control Window(guiWindow)に動画ファイルがドロップされたときも
  //   読み込めるようにする。
  //   (ofRunApp(mainWindow, mainApp) の紐付けだけでは、
  //    メインウィンドウへのドロップしか検知できないため)
  //   ofAddListenerは「参照渡し」のシグネチャを要求するため、
  //   値渡しのofApp::dragEventではなく、
  //   専用のofApp::onGuiWindowFileDraggedを紐付ける。
  ofAddListener(guiWindow->events().fileDragEvent, mainApp.get(), &ofApp::onGuiWindowFileDragged);
  // Controls Windowが残っている状態でMain Windowを復帰できるよう、
  // Controls側のキーイベントもofAppへ渡す。
  ofAddListener(guiWindow->events().keyPressed, mainApp.get(), &ofApp::onGuiWindowKeyPressed);

  ofRunApp(mainWindow, mainApp);
  ofRunMainLoop();
}
