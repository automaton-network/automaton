#include "automaton/core/eth_rpc/eth_rpc.h"

#include <string>
#include <utility>

#include "automaton/core/crypto/cryptopp/Keccak_256_cryptopp.h"
#include "automaton/core/network/tcp_implementation.h"
#include "automaton/core/io/io.h"

using automaton::core::crypto::cryptopp::Keccak_256_cryptopp;
using automaton::core::io::bin2hex;
using automaton::core::network::connection;
using automaton::core::network::connection_id;

static const char* CRLF = "\r\n";
static const char* CRLF2 = "\r\n\r\n";
static const uint32_t BUFFER_SIZE = 512;

namespace automaton {
namespace core {
namespace eth_rpc {

std::string hash(const std::string& data) {
  Keccak_256_cryptopp hasher;
  uint8_t digest[32];
  hasher.calculate_digest(reinterpret_cast<const uint8_t*>(data.data()), data.size(), digest);
  return std::string(reinterpret_cast<char*>(digest), 32);
}

std::string dec_to_32hex(uint32_t n) {
  std::stringstream ss;
  ss << std::hex << n;
  return std::string(64-ss.str().size(), '0') + ss.str();
}

std::unordered_map<std::string, std::shared_ptr<ETHContract> > ETHContract::contracts;

void ETHContract::register_contract(const std::string& server, const std::string& address,
    std::vector<std::string> signs) {
  if (contracts.find(address) != contracts.end()) {
    LOG(INFO) << "Contract already registered!";
    return;
  }
  contracts[address] = std::shared_ptr<ETHContract>(new ETHContract(server, address, signs));
}

std::shared_ptr<ETHContract> ETHContract::get_contract(const std::string& address) {
  auto it = contracts.find(address);
  if (it == contracts.end()) {
    LOG(ERROR) << "No contract with address " << address;
    return nullptr;
  }
  return it->second;
}

ETHContract::ETHContract(const std::string& server, const std::string& address, std::vector<std::string> signs):
    call_id(0),
    server(server),
    address(address) {
  for (uint32_t i = 0; i < signs.size(); ++i) {
    signatures[signs[i]] = "0x" + (bin2hex(hash(signs[i]))).substr(0, 8);
  }
  // std::string header;
  // std::stringstream body;
  // std::stringstream chunk;
  buffer_size = BUFFER_SIZE;
  buffer = std::shared_ptr<char>(new char[buffer_size], std::default_delete<char[]>());;
}

ETHContract::~ETHContract() {}

void ETHContract::call(const std::string& from_address, const std::string& f,
    const std::string& params,
    std::function<void(const automaton::core::common::status&, const std::string&)> callback) {
  auto it = signatures.find(f);
  if (it == signatures.end()) {
    callback(automaton::core::common::status::invalid_argument("Function signature is not found!"), "");
    return;
  }
  callbacks[++call_id] = callback;
  std::stringstream data;
  data << "{\"jsonrpc\":\"2.0\",\"method\":\"eth_call\",\"params\": [{ \"to\":\"" <<
      address << "\",\"data\":\"" << it->second <<
      params << "\",\"from\":\"" << from_address << "\"},\"latest\"],\"id\":" << call_id << "}";

  std::stringstream ss;
  ss << "POST / HTTP/1.1\r\n";
  ss << "Accept: */*\r\n";
  ss << "Content-Type: application/x-www-form-urlencoded\r\n";
  ss << "Content-Length: " << data.str().size() << "\r\n";
  ss << "\r\n" << data.str();

  conn = connection::create("tcp", 1, server, shared_from_this());
  if (conn->init()) {
    LOG(DBUG) << "Connection init was successful!";
    conn->connect();
    conn->async_read(buffer, buffer_size, 0, call_id);
    conn->async_send(ss.str(), call_id);
    std::cout << "\n======= REQUEST =======\n" << ss.str() << "\n=====================\n\n";
  } else {
    conn = nullptr;
    callback(automaton::core::common::status::internal("Could not connect to server!"), "");
    callbacks.erase(call_id);
  }
}

// void ETHContract::handle_message(){}

void ETHContract::on_message_received(connection_id c, std::shared_ptr<char> buffer,
    uint32_t bytes_read, uint32_t mid) {
  // read header and body and call async_read again
  std::string s(buffer.get(), bytes_read);
  auto it = callbacks.find(mid);
  if (it == callbacks.end()) {
    LOG(ERROR) << "No such callback function!";
    conn->disconnect();
    return;
  }
  it->second(automaton::core::common::status::ok(), std::move(s));
  callbacks.erase(it);
  conn->disconnect();
  // conn->async_read(buffer, buffer_size, 0, 0);
}

void ETHContract::on_message_sent(connection_id c, uint32_t mid, const automaton::core::common::status& s) {
  LOG(INFO) << "Message " << mid << " was sent. Status: " << s;
}

void ETHContract::on_connected(connection_id c) {
  LOG(INFO) << "Connected";
}

void ETHContract::on_disconnected(connection_id c) {
  LOG(INFO) << "Disconnected";
}

void ETHContract::on_connection_error(connection_id c, const automaton::core::common::status& s) {
  if (s.code == automaton::core::common::status::OK) {
    return;
  }
  LOG(ERROR) << s;
  conn->disconnect();
}

}  // namespace eth_rpc
}  // namespace core
}  // namespace automaton
