#ifndef AUTOMATON_CORE_NETWORK_RPC_H_
#define AUTOMATON_CORE_NETWORK_RPC_H_

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <boost/asio.hpp>  // NOLINT
#include <boost/bind.hpp>  // NOLINT
#include "automaton/core/io/io.h"

using boost::asio::ip::tcp;


namespace automaton {
namespace core {
namespace network {


class session {
 public:
  explicit session(boost::asio::io_service& io_service); // NOLINT

  tcp::socket& socket();

  void start();

  void handle_read(const boost::system::error_code& error, size_t bytes_transferred);

  void handle_write(const boost::system::error_code& error);

 private:
  tcp::socket socket_;
  static const size_t kBufferSize = 1024;
  char data_[kBufferSize];
};


class server {
 public:
  server(uint16_t port, std::string(*func)(std::string));
  void handle_accept(session* new_session, const boost::system::error_code& error);
  void run();
  void stop();

 private:
  std::string (*handler)(std::string json_str);
  boost::asio::io_service io_service;
  tcp::acceptor acceptor;
  std::thread* worker;
};

}  // namespace network
}  // namespace core
}  // namespace automaton

#endif  // AUTOMATON_CORE_NETWORK_RPC_H_
