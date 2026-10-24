#pragma once

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gux {

// OSCメッセージ1件を組み立てる最小限のクラス。
// int32 / float32 / string だけを扱う（TouchDesignerのOSC In CHOP / DATで受け取れる形式）。
class OscMessage {
 public:
  explicit OscMessage(const std::string& address) : address(address) {}

  OscMessage& add(int value) {
    typeTags += 'i';
    appendInt32(arguments, static_cast<std::int32_t>(value));
    return *this;
  }

  OscMessage& add(float value) {
    typeTags += 'f';
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendUInt32(arguments, bits);
    return *this;
  }

  OscMessage& add(const std::string& value) {
    typeTags += 's';
    appendPaddedString(arguments, value);
    return *this;
  }

  std::vector<char> bytes() const {
    std::vector<char> data;
    appendPaddedString(data, address);
    appendPaddedString(data, typeTags);
    data.insert(data.end(), arguments.begin(), arguments.end());
    return data;
  }

 private:
  std::string address;
  std::string typeTags = ",";
  std::vector<char> arguments;

  static void appendPaddedString(std::vector<char>& data,
                                 const std::string& value) {
    data.insert(data.end(), value.begin(), value.end());
    // OSCの文字列はNUL終端を含めて4バイト境界まで埋める。
    const size_t padding = 4 - (value.size() % 4);
    data.insert(data.end(), padding, '\0');
  }

  static void appendInt32(std::vector<char>& data, std::int32_t value) {
    appendUInt32(data, static_cast<std::uint32_t>(value));
  }

  static void appendUInt32(std::vector<char>& data, std::uint32_t value) {
    const std::uint32_t network = htonl(value);
    const char* raw = reinterpret_cast<const char*>(&network);
    data.insert(data.end(), raw, raw + 4);
  }
};

// UDPでOSCを送るだけのソケット。受信側が起動していなくても送信は失敗扱いにしない。
class OscUdpSender {
 public:
  ~OscUdpSender() { close(); }

  bool open(const std::string& host, int port) {
    close();
    socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd < 0) return false;
    // 描画を止めないよう、送信はノンブロッキングにする。
    const int flags = ::fcntl(socketFd, F_GETFL, 0);
    ::fcntl(socketFd, F_SETFL, flags | O_NONBLOCK);

    std::memset(&target, 0, sizeof(target));
    target.sin_family = AF_INET;
    target.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &target.sin_addr) != 1) {
      close();
      return false;
    }
    return true;
  }

  void close() {
    if (socketFd >= 0) ::close(socketFd);
    socketFd = -1;
  }

  bool isOpen() const { return socketFd >= 0; }

  void send(const OscMessage& message) {
    if (socketFd < 0) return;
    const std::vector<char> data = message.bytes();
    ::sendto(socketFd, data.data(), data.size(), 0,
             reinterpret_cast<const sockaddr*>(&target), sizeof(target));
  }

 private:
  int socketFd = -1;
  sockaddr_in target{};
};

}  // namespace gux
