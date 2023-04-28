#include <array>
#include <cassert>
#include <iostream>
#include <set>

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

namespace ft {

class exception : public std::exception {
private:
  std::string message;

public:
  exception(const std::string &message) : message(message) {}
  const char *what() const noexcept override { return this->message.c_str(); }
};

void setFdNonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    throw ft::exception("fcntl(F_SETFL)");
}

class ServerSocket {
private:
  int fd;
  bool listening;

public:
  ServerSocket(int port) noexcept(false) : listening(false) {
    if (port <= 0 || port > 65535)
      throw ft::exception("Invalid port");
    this->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (this->fd < 0) {
      throw ft::exception("socket()");
    }
    try {
      {
        int on = 1;
        if (setsockopt(this->fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
          throw ft::exception("setsockopt()");
      }

      setFdNonblock(this->fd);

      {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(this->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
          throw ft::exception("bind()");
      }
    } catch (const std::exception &e) {
      close(this->fd);
      throw e;
    }
  }
  ~ServerSocket() {
    if (this->fd >= 0)
      close(this->fd);
  }
  ServerSocket(const ft::ServerSocket &copy) = delete;
  ServerSocket(ServerSocket &&move) noexcept : fd(move.fd) { move.fd = -1; }
  ServerSocket &operator=(const ft::ServerSocket &copy) = delete;
  ServerSocket &operator=(ServerSocket &&other) noexcept {
    close(this->fd);
    this->fd = other.fd;
    other.fd = -1;
    return *this;
  }

  void listen() noexcept(false) {
    if (this->listening)
      throw ft::exception("Already listening");
    if (::listen(this->fd, SOMAXCONN) < 0)
      throw ft::exception("listen()");
    this->listening = true;
  }

  int getFd() const noexcept { return this->fd; }
};

class Server {
private:
  ServerSocket socket;
  bool started;

public:
  Server(int port) noexcept(false) : socket(port), started(false) {}
  ~Server() {}
  Server(const ft::Server &copy) = delete;
  Server(Server &&move) noexcept = delete;
  Server &operator=(const ft::Server &copy) = delete;
  Server &operator=(Server &&other) noexcept = delete;

private:
  static int maxSocketId(int serverSocket, const std::set<int> &clientSockets) {
    if (clientSockets.empty()) {
      return serverSocket;
    } else {
      return std::max(serverSocket, *clientSockets.rbegin());
    }
  }

public:
  void start() {
    if (started)
      throw ft::exception("Already started");
    this->socket.listen();
    this->started = true;

    std::set<int> clientSockets;

    fd_set fds;

    while (true) {
      FD_ZERO(&fds);
      FD_SET(this->socket.getFd(), &fds);

      for (auto sock : clientSockets) {
        FD_SET(sock, &fds);
      }

      int max = maxSocketId(this->socket.getFd(), clientSockets);
      select(max + 1, &fds, nullptr, nullptr, nullptr);

      auto it = clientSockets.begin();
      while (it != clientSockets.end()) {
        int sock = *it;
        if (FD_ISSET(sock, &fds)) {
          std::array<char, BUFFER_SIZE> buf;
          int nRead = recv(sock, buf.data(), BUFFER_SIZE, 0);
          if (nRead <= 0 && errno != EAGAIN) {
            shutdown(sock, SHUT_RDWR);
            close(sock);
            it = clientSockets.erase(it);
          } else if (nRead > 0) {
            send(sock, buf.data(), nRead, 0);
            ++it;
          } else {
            ++it;
          }
        } else {
          ++it;
        }
      }
      if (FD_ISSET(socket.getFd(), &fds)) {
        int sock = accept(socket.getFd(), 0, 0);
        if (sock == -1) {
          throw ft::exception("accept()");
        }
        setFdNonblock(sock);
        clientSockets.insert(sock);
      }
    }
  }
};

} // namespace ft

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: port" << std::endl;
    return EXIT_SUCCESS;
  }
  try {
    ft::Server server(atoi(argv[1]));
    server.start();
  } catch (const ft::exception &e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
