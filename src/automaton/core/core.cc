#include <future>
#include <iostream>
#include <regex>
#include <string>

#include "base64.h"  // NOLINT
#include "filters.h"  // NOLINT
#include <json.hpp>

#include "automaton/core/cli/cli.h"
#include "automaton/core/data/factory.h"
#include "automaton/core/data/protobuf/protobuf_factory.h"
#include "automaton/core/data/protobuf/protobuf_schema.h"
#include "automaton/core/network/http_server.h"
#include "automaton/core/network/simulated_connection.h"
#include "automaton/core/network/tcp_implementation.h"
#include "automaton/core/node/node.h"
#include "automaton/core/script/engine.h"
#include "automaton/core/smartproto/smart_protocol.h"
#include "automaton/core/io/io.h" //  IO needs to be included after boost

using automaton::core::data::factory;
using automaton::core::data::protobuf::protobuf_factory;
using automaton::core::data::protobuf::protobuf_schema;
using automaton::core::data::schema;
using automaton::core::io::get_file_contents;
using automaton::core::network::http_server;
using automaton::core::script::engine;
using automaton::core::smartproto::node;
using automaton::core::smartproto::smart_protocol;

using json = nlohmann::json;

using std::make_unique;
using std::string;
using std::unique_ptr;
using std::vector;

void string_replace(string* str,
                    const string& oldStr,
                    const string& newStr) {
  string::size_type pos = 0u;
  while ((pos = str->find(oldStr, pos)) != string::npos) {
     str->replace(pos, oldStr.length(), newStr);
     pos += newStr.length();
  }
}

static const char* automaton_ascii_logo_cstr =
  "\n\x1b[40m\x1b[1m"
  "                                                                     " "\x1b[0m\n\x1b[40m\x1b[1m"
  "                                                                     " "\x1b[0m\n\x1b[40m\x1b[1m"
  "    @197m█▀▀▀█ @39m█ █ █ @11m▀▀█▀▀ @129m█▀▀▀█ @47m█▀█▀█ @9m█▀▀▀█ @27m▀▀█▀▀ @154m█▀▀▀█ @13m█▀█ █            " "\x1b[0m\n\x1b[40m\x1b[1m" // NOLINT
  "    @197m█▀▀▀█ @39m█ ▀ █ @11m█ █ █ @129m█ ▀ █ @47m█ ▀ █ @9m█▀▀▀█ @27m█ █ █ @154m█ ▀ █ @13m█ █ █   @15mCORE     " "\x1b[0m\n\x1b[40m\x1b[1m" // NOLINT
  "    @197m▀ ▀ ▀ @39m▀▀▀▀▀ @11m▀ ▀ ▀ @129m▀▀▀▀▀ @47m▀ ▀ ▀ @9m▀ ▀ ▀ @27m▀ ▀ ▀ @154m▀▀▀▀▀ @13m▀ ▀▀▀   @15mv0.0.1   " "\x1b[0m\n\x1b[40m\x1b[1m" // NOLINT
  "                                                                     " "\x1b[0m\n@0m"
  "▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀" "\x1b[0m\n";

class rpc_server_handler: public automaton::core::network::http_server::server_handler {
  engine* script;

 public:
    explicit rpc_server_handler(engine* en): script(en) {}
    std::string handle(std::string json_cmd, http_server::status_code* s) {
      std::stringstream sstr(json_cmd);
      nlohmann::json j;
      sstr >> j;
      std::string cmd = "";
      std::string msg = "";
      if (j.find("method") != j.end() && j.find("msg") != j.end()) {
        cmd = j["method"];
        msg = j["msg"];
      } else {
        LOG(ERROR) << "ERROR in rpc server handler: Invalid request";
        *s = http_server::status_code::BAD_REQUEST;
        return "";
      }
      std::string params = "";
      LOG(INFO) << "Server received command: " << cmd << " -> " << automaton::core::io::bin2hex(msg);
      if (msg.size() > 0) {
        CryptoPP::StringSource ss(msg, true, new CryptoPP::Base64Decoder(new CryptoPP::StringSink(params)));
      }
      if ((*script)[cmd] == nullptr) {
        LOG(ERROR) << "ERROR in rpc server handler: Invalid request";
        *s = http_server::status_code::BAD_REQUEST;
        return "";
      }
      sol::protected_function_result pfr = (*script)[cmd](params);
      if (!pfr.valid()) {
        sol::error err = pfr;
        LOG(ERROR) << "ERROR in rpc server handler: " << err.what();
        *s = http_server::status_code::INTERNAL_SERVER_ERROR;
        return "";
      }
      std::string result = pfr;
      if (s != nullptr) {
        *s = http_server::status_code::OK;
      } else {
        LOG(ERROR) << "Status code variable is missing";
      }
      return result;
    }
};

int main(int argc, char* argv[]) {
  string automaton_ascii_logo(automaton_ascii_logo_cstr);
  string_replace(&automaton_ascii_logo, "@", "\x1b[38;5;");
  auto core_factory = std::make_shared<protobuf_factory>();

{
  automaton::core::cli::cli cli;
  engine script(core_factory);
  script.bind_core();

  // Bind smartproto::node class
  auto node_type = script.create_simple_usertype<node>();

  node_type.set(sol::call_constructor,
    sol::factories(
    [&](const std::string& id, std::string proto) -> unique_ptr<node> {
      return make_unique<node>(id, proto);
    }));

  // Bind this node to its own Lua state.
  node_type.set("add_peer", &node::add_peer);
  node_type.set("remove_peer", &node::remove_peer);
  node_type.set("connect", &node::connect);
  node_type.set("disconnect", &node::disconnect);
  node_type.set("send", &node::send_message);
  node_type.set("listen", &node::set_acceptor);

  node_type.set("msg_id", &node::find_message_id);
  node_type.set("new_msg", &node::create_msg_by_id);
  node_type.set("send", &node::send_message);

  node_type.set("dump_logs", &node::dump_logs);
  node_type.set("debug_html", &node::debug_html);

  node_type.set("script", [](node& n, std::string command) -> std::string {
    std::promise<std::string> prom;
    std::future<std::string> fut = prom.get_future();
    n.script(command, &prom);
    std::string result = fut.get();
    return result;
  });

  node_type.set("get_id", &node::get_id);
  node_type.set("get_protocol_id", &node::get_protocol_id);
  node_type.set("get_address", [](node& n) -> std::string {
    std::shared_ptr<automaton::core::network::acceptor> a = n.get_acceptor();
    if (a) {
      return a->get_address();
    }
    return "";
  });

  node_type.set("process_cmd", &node::process_cmd);

  node_type.set("call", [](node& n, std::string command) {
    n.script(command, nullptr);
  });

  node_type.set("known_peers", [](node& n) {
    LOG(DEBUG) << "getting known peers... " << &n;
    LOG(DEBUG) << n.list_known_peers();
    return sol::as_table(n.list_known_peers());
  });

  node_type.set("peers", [](node& n) {
    LOG(DEBUG) << "getting peers... " << &n;
    LOG(DEBUG) << n.list_connected_peers();
    return sol::as_table(n.list_connected_peers());
  });

  node_type.set("get_peer_address", [](node& n, uint32_t pid) {
    return n.get_peer_info(pid).address;
  });

  script.set_usertype("node", node_type);

  std::unordered_map<std::string, std::unique_ptr<node> > nodes;

  script.set_function("list_nodes_as_table", [&](){
    std::vector<std::string> result;
    for (const auto& n : nodes) {
      result.push_back(n.first);
    }
    return sol::as_table(result);
  });

  script.set_function("get_node", [&](std::string node_id) -> node* {
    const auto& n = nodes.find(node_id);
    if (n != nodes.end()) {
      return (n->second).get();
    }
    return nullptr;
  });

  script.set_function("launch_node", [&](std::string node_id, std::string protocol_id, std::string address) {
    std::cout << "launching node ... " << std::endl;
    auto n = nodes.find(node_id);
    if (n == nodes.end()) {
      nodes[node_id] = std::make_unique<node>(node_id, protocol_id);
      bool res = nodes[node_id]->set_acceptor(address.c_str());
      if (!res) {
        LOG(ERROR) << "Setting acceptor at address " << address << " failed!";
        std::cout << "!!! set acceptor failed" << std::endl;
      }
    }
  });

  script.set_function("remove_node", [&](std::string node_id) {});

  script.set_function("history_add", [&](std::string cmd){
    cli.history_add(cmd.c_str());
  });

  script.set_function("load_protocol", [&](std::string proto_id){
    smart_protocol::load(proto_id);
  });

  automaton::core::network::tcp_init();

  std::shared_ptr<automaton::core::network::simulation> sim = automaton::core::network::simulation::get_simulator();
  sim->simulation_start(100);
  cli.print(automaton_ascii_logo.c_str());

  script.safe_script(get_file_contents("automaton/examples/smartproto/common/names.lua"));
  script.safe_script(get_file_contents("automaton/examples/smartproto/common/dump.lua"));
  script.safe_script(get_file_contents("automaton/examples/smartproto/common/network.lua"));
  script.safe_script(get_file_contents("automaton/examples/smartproto/common/connections_graph.lua"));
  script.safe_script(get_file_contents("automaton/examples/smartproto/common/show_states.lua"));

  std::unordered_map<std::string, std::pair<std::string, std::string> > rpc_commands;
  uint32_t rpc_port = 0;

  std::ifstream i("automaton/core/coreinit.json");
  if (!i.is_open()) {
    LOG(ERROR) << "coreinit.json could not be opened";
  } else {
    nlohmann::json j;
    i >> j;
    i.close();
    std::vector<std::string> paths = j["protocols"];
    for (auto& p : paths) {
      script.safe_script(get_file_contents((p + "init.lua").c_str()));
      smart_protocol::load(p);
    }
    script.set_function("get_core_supported_protocols", [&](){
      std::unordered_map<std::string, std::unordered_map<std::string, std::string> > protocols;
      for (std::string proto : smart_protocol::list_protocols()) {
        protocols[proto] = smart_protocol::get_protocol(proto)->get_msgs_definitions();
      }
      return sol::as_table(protocols);
    });

    std::vector<std::string> rpc_protos = j["command_definitions"];
    for (auto& p : rpc_protos) {
      schema* rpc_schema = new protobuf_schema(get_file_contents(p.c_str()));
      script.import_schema(rpc_schema);
    }
    std::vector<std::string> rpc_luas = j["command_implementations"];
    for (auto& p : rpc_luas) {
      script.safe_script(get_file_contents(p.c_str()));
    }
    for (auto& c : j["commands"]) {
      std::cout << "loaded rpc command: " << c["cmd"] << std::endl;
      rpc_commands[c["cmd"]] = std::make_pair(c["input"], c["output"]);
    }
    rpc_port = j["rpc_config"]["default_port"];
  }
  i.close();
  // Start dump_logs thread.
  std::mutex logger_mutex;
  bool stop_logger = false;
  std::thread logger([&]() {
    while (!stop_logger) {
      // Dump logs once per second.
      std::this_thread::sleep_for(std::chrono::milliseconds(1500));
      logger_mutex.lock();
      try {
        sol::protected_function_result pfr;
        pfr = script.safe_script(
          R"(for k,v in pairs(networks) do
              dump_logs(k)
            end
          )");
        if (!pfr.valid()) {
          sol::error err = pfr;
          std::cout << "\n" << err.what() << "\n";
          break;
        }
      } catch (std::exception& e) {
        LOG(FATAL) << "Exception in logger: " << e.what();
      } catch (...) {
        LOG(FATAL) << "Exception in logger";
      }
      logger_mutex.unlock();
    }
  });

  std::shared_ptr<automaton::core::network::http_server::server_handler> s_handler(new rpc_server_handler(&script));
  http_server rpc_server(rpc_port, s_handler);
  rpc_server.run();

  while (1) {
    auto input = cli.input("\x1b[38;5;15m\x1b[1m|A|\x1b[0m ");
    if (input == nullptr) {
      cli.print("\n");
      break;
    }

    string cmd{input};
    cli.history_add(cmd.c_str());

    logger_mutex.lock();
    sol::protected_function_result pfr = script.safe_script(cmd, &sol::script_pass_on_error);
    logger_mutex.unlock();

    if (!pfr.valid()) {
      sol::error err = pfr;
      LOG(ERROR) << "Error while executing command: " << err.what();
    }
  }

  rpc_server.stop();

  stop_logger = true;
  logger.join();

  LOG(DEBUG) << "Destroying lua state & objects";

  sim->simulation_stop();
}

  LOG(DEBUG) << "tcp_release";

  automaton::core::network::tcp_release();

  LOG(DEBUG) << "tcp_release done.";

  return 0;
}
