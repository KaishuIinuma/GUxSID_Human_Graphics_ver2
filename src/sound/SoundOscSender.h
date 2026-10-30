#pragma once

#include "sound/OscUdpSender.h"
#include "tracking/TrackedObject.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <array>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gux {

// 音の外部ソフトへ渡す、1人分の情報（キャンバスのピクセル単位）。
struct SoundPerson {
  ObjectId id = 0;
  glm::vec2 centroid{0.0f, 0.0f};
  float area = 0.0f;
  // 同じ合体グループの人は同じ値になる。グループ内で最小のIDを使う。
  ObjectId group = 0;
  // 画面に描かれる輪郭の頂点。動きの量（最大移動量）を測るのに使う。
  std::vector<glm::vec2> vertices;
};

// 人物の情報をOSCで音のソフト（TouchDesignerなど）へ送る。
//
// TouchDesignerのOSC In CHOPで扱いやすいよう、人ごとに固定の番号（スロット）を
// 割り当て、毎フレーム全スロットを送る。チャンネル数が増減しない。
//   /gux/slot/0〜15  active x y size speed group
//     active: 人がいれば1、いなければ0。いなくなった後も最後の位置を送り続ける。
//     送信量が多い（毎フレーム16件）ため、setSlotMessagesEnabled(true) の時だけ送る。
//     SfxPlayer は使っていない。TouchDesigner で使う時にオンにする。
//
// 出入り・合体などの出来事（OSC In DATのコールバック向け）:
//   /gux/enter  id slot x y size     人が現れた
//   /gux/exit   id slot              人がいなくなった
//   /gux/person id x y size speed group   毎フレーム1人ずつ
//   /gux/count  people groups        毎フレーム
//   /gux/merge  idA idB x y          2人がくっついた（x yは2人の中間）
//   /gux/split  idA idB x y          くっついていた2人が離れた
//   /gux/clear                       送信開始時・停止時（音を全部消す合図）
//
// ドレミ（ハ長調の8音）を人に割り当てる。現れた順に、空いている一番低い音を使う。
//   /gux/note/0〜7  active level motion x y id   毎フレーム8音すべて
//     0=ド(C4) 1=レ 2=ミ 3=ファ 4=ソ 5=ラ 6=シ 7=ド(C5)
//     level : 動きから決めた音量 0〜1（速く立ち上がり、ゆっくり下がる）
//     motion: 輪郭の頂点のうち、一番大きく動いた頂点の速さ（画面の高さ/秒）
//     いなくなると active=0 になり、level が0まで下がってから音が空く。
//   /gux/noteon  note midi id        音が人に割り当てられた
//   /gux/noteoff note midi id        その人がいなくなった
class SoundOscSender {
 public:
  // 人が一瞬検出から外れても、この時間内に戻れば退場扱いにしない。
  float exitGraceSeconds = 0.4f;
  // 合体・分離がこの時間続いたら確定として送る（境目でのチラつき防止）。
  float mergeHoldSeconds = 0.15f;
  // 速さの平滑化の時定数（秒）。
  float speedSmoothingSeconds = 0.25f;

  static constexpr int SlotCount = 16;
  static constexpr int NoteCount = 8;
  static constexpr std::array<int, NoteCount> NoteMidi = {60, 62, 64, 65, 67, 69, 71, 72};

  // 動きの量を音量へ変える範囲（画面の高さ/秒）。
  // motionFloor 以下は0（輪郭の細かい揺れを無視）、motionFull 以上で最大。
  float motionFloor = 0.15f;
  float motionFull = 1.5f;
  // 音量の立ち上がり・下がりの時定数（秒）。
  float levelAttackSeconds = 0.05f;
  float levelReleaseSeconds = 0.6f;
  // この時間輪郭が更新されなければ、動いていないとみなす。
  float motionStaleSeconds = 0.3f;

  void setEnabled(bool value) {
    if (enabled == value) return;
    if (enabled) sendClear();
    enabled = value;
    reset();
    if (enabled) sendClear();
  }

  bool isEnabled() const { return enabled; }

  // /gux/slot を送るかどうか。スロットの割り当て自体は、送らない時も続ける。
  void setSlotMessagesEnabled(bool value) { slotMessagesEnabled = value; }

  void setTarget(const std::string& host, int port) {
    if (host == targetHost && port == targetPort && sender.isOpen()) return;
    targetHost = host;
    targetPort = port;
    if (!sender.open(host, port)) {
      ofLogWarning("SoundOscSender") << "OSC送信先を開けませんでした: " << host
                                     << ":" << port;
      return;
    }
    reset();
    if (enabled) sendClear();
    ofLogNotice("SoundOscSender") << "OSC送信先: " << host << ":" << port;
  }

  void stop() {
    if (enabled) sendClear();
    reset();
  }

  void update(const std::vector<SoundPerson>& people, float canvasWidth,
              float canvasHeight, float nowSeconds) {
    if (!enabled || !sender.isOpen()) return;
    if (canvasWidth <= 0.0f || canvasHeight <= 0.0f) return;

    const float canvasArea = canvasWidth * canvasHeight;
    std::set<ObjectId> visibleIds;
    std::set<ObjectId> groups;

    for (const auto& person : people) {
      visibleIds.insert(person.id);
      groups.insert(person.group);

      const glm::vec2 position(person.centroid.x / canvasWidth,
                               person.centroid.y / canvasHeight);
      const float size = std::clamp(person.area / canvasArea, 0.0f, 1.0f);

      auto found = states.find(person.id);
      if (found == states.end()) {
        PersonState state;
        state.position = position;
        state.lastMoveSeconds = nowSeconds;
        state.slot = takeFreeSlot();
        state.note = takeFreeNote(person.id);
        found = states.emplace(person.id, state).first;
        if (state.note >= 0) {
          sender.send(OscMessage("/gux/noteon")
                          .add(state.note)
                          .add(NoteMidi[state.note])
                          .add(toInt(person.id)));
        }
        sender.send(OscMessage("/gux/enter")
                        .add(toInt(person.id))
                        .add(state.slot)
                        .add(position.x)
                        .add(position.y)
                        .add(size));
      }

      PersonState& state = found->second;
      updateSpeed(state, position, nowSeconds);
      updateMotion(state, person.vertices, canvasHeight, nowSeconds);
      state.position = position;
      state.size = size;
      state.group = person.group;
      state.lastSeenSeconds = nowSeconds;

      sender.send(OscMessage("/gux/person")
                      .add(toInt(person.id))
                      .add(position.x)
                      .add(position.y)
                      .add(size)
                      .add(state.speed)
                      .add(toInt(person.group)));
    }

    // 退場判定。猶予時間を過ぎた人だけを消す。
    for (auto it = states.begin(); it != states.end();) {
      if (visibleIds.count(it->first) == 0 &&
          nowSeconds - it->second.lastSeenSeconds > exitGraceSeconds) {
        const int slot = it->second.slot;
        sender.send(OscMessage("/gux/exit").add(toInt(it->first)).add(slot));
        if (slot >= 0) slots[slot].active = false;
        const int note = it->second.note;
        if (note >= 0) {
          notes[note].active = false;
          notes[note].motion = 0.0f;
          sender.send(OscMessage("/gux/noteoff")
                          .add(note)
                          .add(NoteMidi[note])
                          .add(toInt(it->first)));
        }
        it = states.erase(it);
      } else {
        ++it;
      }
    }

    for (const auto& [id, state] : states) {
      if (state.slot < 0) continue;
      SlotState& slot = slots[state.slot];
      slot.active = true;
      slot.position = state.position;
      slot.size = state.size;
      slot.speed = state.speed;
      slot.group = state.group;
    }
    sendSlots();

    updateNotes(nowSeconds);
    sendNotes();

    updatePairs(people, nowSeconds);

    sender.send(OscMessage("/gux/count")
                    .add(static_cast<int>(people.size()))
                    .add(static_cast<int>(groups.size())));
  }

 private:
  struct PersonState {
    glm::vec2 position{0.0f, 0.0f};
    float size = 0.0f;
    float speed = 0.0f;
    ObjectId group = 0;
    float lastSeenSeconds = 0.0f;
    float lastMoveSeconds = 0.0f;
    float lastUpdateSeconds = -1.0f;
    int slot = -1;  // 空きがなければ-1（スロットには出ない）
    int note = -1;  // 8音が埋まっていれば-1（音は鳴らない）
    std::vector<glm::vec2> previousVertices;
    float lastShapeSeconds = -1.0f;
    float motion = 0.0f;  // 最大移動量の速さ（画面の高さ/秒）
  };

  struct NoteState {
    bool used = false;    // 人に割り当て中、または音量が下がりきっていない
    bool active = false;  // 割り当てた人が画面にいる
    ObjectId id = 0;
    float level = 0.0f;
    float motion = 0.0f;
    glm::vec2 position{0.5f, 0.5f};
  };

  struct SlotState {
    bool used = false;    // 誰かに割り当て中
    bool active = false;  // 送信値のactive
    glm::vec2 position{0.5f, 0.5f};
    float size = 0.0f;
    float speed = 0.0f;
    ObjectId group = 0;
  };

  using Pair = std::pair<ObjectId, ObjectId>;

  struct PairState {
    bool reported = false;  // 合体として送信済みか
    float changeSinceSeconds = -1.0f;  // 状態が変わり始めた時刻
  };

  OscUdpSender sender;
  std::string targetHost;
  int targetPort = 0;
  bool enabled = false;
  bool slotMessagesEnabled = false;
  std::map<ObjectId, PersonState> states;
  std::map<Pair, PairState> pairs;
  std::array<SlotState, SlotCount> slots{};
  std::array<NoteState, NoteCount> notes{};
  float lastNoteUpdateSeconds = -1.0f;

  static int toInt(ObjectId id) {
    return static_cast<int>(id & 0x7fffffff);
  }

  void reset() {
    states.clear();
    pairs.clear();
    slots = {};
    notes = {};
    lastNoteUpdateSeconds = -1.0f;
  }

  void sendClear() {
    if (!sender.isOpen()) return;
    sender.send(OscMessage("/gux/clear"));
    for (auto& slot : slots) slot.active = false;
    sendSlots();
    for (auto& note : notes) note = NoteState{};
    sendNotes();
  }

  // 空いている一番低い音を使う（音量が下がりきっていない音は使わない）。
  int takeFreeNote(ObjectId id) {
    for (int i = 0; i < NoteCount; ++i) {
      if (notes[i].used) continue;
      notes[i] = NoteState{};
      notes[i].used = true;
      notes[i].active = true;
      notes[i].id = id;
      return i;
    }
    return -1;
  }

  // 各頂点について、前の輪郭で一番近い頂点までの距離を求め、その最大値を
  // 「最大移動量」とする。頂点の並び順はフレームごとにずれることがあるため、
  // 同じ番号の頂点どうしではなく、一番近い頂点どうしで比べる。
  static float maxVertexDisplacement(const std::vector<glm::vec2>& current,
                                     const std::vector<glm::vec2>& previous) {
    float maxDistance2 = 0.0f;
    for (const auto& p : current) {
      float nearest2 = std::numeric_limits<float>::max();
      for (const auto& q : previous) {
        const glm::vec2 d = p - q;
        nearest2 = std::min(nearest2, d.x * d.x + d.y * d.y);
      }
      maxDistance2 = std::max(maxDistance2, nearest2);
    }
    return std::sqrt(maxDistance2);
  }

  // 輪郭は検出のたびにしか変わらないので、変わったときだけ速さを測る。
  void updateMotion(PersonState& state, const std::vector<glm::vec2>& vertices,
                    float canvasHeight, float nowSeconds) {
    if (vertices.empty()) return;
    if (vertices == state.previousVertices) {
      if (state.lastShapeSeconds >= 0.0f &&
          nowSeconds - state.lastShapeSeconds > motionStaleSeconds) {
        state.motion = 0.0f;
      }
      return;
    }
    if (!state.previousVertices.empty() && state.lastShapeSeconds >= 0.0f) {
      const float dt = nowSeconds - state.lastShapeSeconds;
      if (dt > 1e-3f && dt <= motionStaleSeconds) {
        const float distance =
            maxVertexDisplacement(vertices, state.previousVertices) / canvasHeight;
        state.motion = distance / dt;
      } else {
        state.motion = 0.0f;
      }
    }
    state.previousVertices = vertices;
    state.lastShapeSeconds = nowSeconds;
  }

  void updateNotes(float nowSeconds) {
    const float dt = lastNoteUpdateSeconds < 0.0f
        ? 0.0f
        : std::clamp(nowSeconds - lastNoteUpdateSeconds, 0.0f, 0.5f);
    lastNoteUpdateSeconds = nowSeconds;

    for (const auto& [id, state] : states) {
      if (state.note < 0) continue;
      NoteState& note = notes[state.note];
      note.motion = state.motion;
      note.position = state.position;
    }

    const float range = std::max(1e-3f, motionFull - motionFloor);
    for (auto& note : notes) {
      if (!note.used) continue;
      const float target = note.active
          ? std::clamp((note.motion - motionFloor) / range, 0.0f, 1.0f)
          : 0.0f;
      const float tau = target > note.level ? levelAttackSeconds : levelReleaseSeconds;
      note.level += (target - note.level) * (1.0f - std::exp(-dt / std::max(tau, 1e-3f)));
      // いなくなった人の音は、音量が下がりきってから空ける。
      if (!note.active && note.level < 0.005f) {
        note = NoteState{};
      }
    }
  }

  void sendNotes() {
    for (int i = 0; i < NoteCount; ++i) {
      const NoteState& note = notes[i];
      sender.send(OscMessage("/gux/note/" + std::to_string(i))
                      .add(note.active ? 1 : 0)
                      .add(note.level)
                      .add(note.motion)
                      .add(note.position.x)
                      .add(note.position.y)
                      .add(toInt(note.id)));
    }
  }

  // 退場した人のスロットはすぐ再利用せず、長く空いている順に使う。
  // 消えていく音と新しく現れた人の音が同じスロットでぶつからないようにする。
  int takeFreeSlot() {
    int best = -1;
    for (int i = 0; i < SlotCount; ++i) {
      if (slots[i].used) continue;
      if (best < 0 || slotFreedOrder[i] < slotFreedOrder[best]) best = i;
    }
    if (best >= 0) slots[best].used = true;
    return best;
  }

  void sendSlots() {
    for (int i = 0; i < SlotCount; ++i) {
      if (!slots[i].active && slots[i].used) {
        slots[i].used = false;
        slotFreedOrder[i] = ++freedCounter;
      }
      if (!slotMessagesEnabled) continue;
      const SlotState& slot = slots[i];
      sender.send(OscMessage("/gux/slot/" + std::to_string(i))
                      .add(slot.active ? 1 : 0)
                      .add(slot.position.x)
                      .add(slot.position.y)
                      .add(slot.size)
                      .add(slot.speed)
                      .add(toInt(slot.group)));
    }
  }

  std::array<std::uint64_t, SlotCount> slotFreedOrder{};
  std::uint64_t freedCounter = 0;

  // 検出は描画より低い頻度で更新されるため、位置が変わったときだけ速さを測る。
  // 一定時間動きがなければ、速さを0へ近づける。
  void updateSpeed(PersonState& state, const glm::vec2& position,
                   float nowSeconds) {
    if (state.lastUpdateSeconds < 0.0f) {
      state.lastUpdateSeconds = nowSeconds;
      return;
    }
    const float dt = std::max(1e-3f, nowSeconds - state.lastUpdateSeconds);
    state.lastUpdateSeconds = nowSeconds;
    const float alpha = 1.0f - std::exp(-dt / speedSmoothingSeconds);

    if (position != state.position) {
      const float moveDt = std::max(1e-3f, nowSeconds - state.lastMoveSeconds);
      const float instant = glm::distance(position, state.position) / moveDt;
      state.lastMoveSeconds = nowSeconds;
      state.speed += (instant - state.speed) * alpha;
    } else if (nowSeconds - state.lastMoveSeconds > 0.2f) {
      state.speed += (0.0f - state.speed) * alpha;
    }
  }

  void updatePairs(const std::vector<SoundPerson>& people, float nowSeconds) {
    std::set<Pair> together;
    for (size_t i = 0; i < people.size(); ++i) {
      for (size_t j = i + 1; j < people.size(); ++j) {
        if (people[i].group != people[j].group) continue;
        together.insert(std::minmax(people[i].id, people[j].id));
      }
    }
    for (const auto& pair : together) pairs.try_emplace(pair);

    for (auto it = pairs.begin(); it != pairs.end();) {
      const Pair& pair = it->first;
      PairState& state = it->second;
      const bool bothPresent =
          states.count(pair.first) > 0 && states.count(pair.second) > 0;
      if (!bothPresent) {
        // どちらかが退場した場合はexitで伝わるので、分離は送らない。
        it = pairs.erase(it);
        continue;
      }

      const bool isTogether = together.count(pair) > 0;
      if (isTogether == state.reported) {
        state.changeSinceSeconds = -1.0f;
        if (!isTogether) {
          it = pairs.erase(it);
          continue;
        }
      } else {
        if (state.changeSinceSeconds < 0.0f) state.changeSinceSeconds = nowSeconds;
        if (nowSeconds - state.changeSinceSeconds >= mergeHoldSeconds) {
          state.reported = isTogether;
          state.changeSinceSeconds = -1.0f;
          const glm::vec2 middle = (states[pair.first].position +
                                    states[pair.second].position) * 0.5f;
          sender.send(OscMessage(isTogether ? "/gux/merge" : "/gux/split")
                          .add(toInt(pair.first))
                          .add(toInt(pair.second))
                          .add(middle.x)
                          .add(middle.y));
          if (!isTogether) {
            it = pairs.erase(it);
            continue;
          }
        }
      }
      ++it;
    }
  }
};

}  // namespace gux
