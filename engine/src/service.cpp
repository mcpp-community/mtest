module mcpplibs.mtest.engine.service;

import std;
import mcpplibs.mtest.engine.model;
import mcpplibs.mtest.engine.commands;

namespace mcpplibs::mtest::engine {

namespace {

std::mutex out_lock;

void send(const json& message) {
    std::lock_guard g(out_lock);
    std::cout << dumped(message) << '\n';
    std::cout.flush();
}

json error_reply(const json& id, int code, std::string message) {
    return {{"jsonrpc", "2.0"},
            {"id", id},
            {"error", {{"code", code}, {"message", std::move(message)}}}};
}

}  // namespace

int serve(const std::filesystem::path& cwd) {
    std::vector<std::jthread> requests;
    for (std::string line; std::getline(std::cin, line);) {
        if (line.empty() || line == "\r")
            continue;
        auto m = json::parse(line, nullptr, false);
        if (m.is_discarded() || !m.is_object()) {
            send(error_reply(nullptr, -32700, "not a JSON object"));
            continue;
        }
        const json id = m.value("id", json(nullptr));
        const std::string method = m.value("method", "");
        const json params = m.value("params", json::object());
        const bool notification = !m.contains("id");
        if (method == "initialize") {
            send({{"jsonrpc", "2.0"},
                  {"id", id},
                  {"result",
                   {{"protocolVersion", params.value("protocolVersion", std::string("2025-06-18"))},
                    {"capabilities", {{"tools", {{"listChanged", false}}}}},
                    {"serverInfo", {{"name", "mtest"}, {"version", "0.5.0"}}},
                    {"instructions",
                     "Run tests and ask about runs: run, plan, explain, history, analyze, graph, "
                     "gate, ci. "
                     "Each result carries a `conclusion`; a failure's `again` repeats it."}}}});
            continue;
        }
        if (method == "notifications/initialized" || method == "initialized")
            continue;
        if (method == "shutdown") {
            send({{"jsonrpc", "2.0"}, {"id", id}, {"result", nullptr}});
            continue;
        }
        if (method == "exit")
            break;
        if (method == "tools/list") {
            send({{"jsonrpc", "2.0"}, {"id", id}, {"result", {{"tools", catalog()["tools"]}}}});
            continue;
        }
        if (method == "catalog") {
            send({{"jsonrpc", "2.0"}, {"id", id}, {"result", catalog()}});
            continue;
        }
        const bool mcp_call = method == "tools/call";
        const std::string command = mcp_call ? params.value("name", "") : method;
        const json arguments = mcp_call ? params.value("arguments", json::object()) : params;
        requests.emplace_back([=] {
            auto events = [&](const json& e) {
                send({{"jsonrpc", "2.0"}, {"method", "mtest/event"}, {"params", e}});
            };
            auto a = dispatch(cwd, command, arguments, events);
            if (notification)
                return;
            if (mcp_call) {
                send({{"jsonrpc", "2.0"},
                      {"id", id},
                      {"result",
                       {{"content",
                         json::array({{{"type", "text"},
                                       {"text", a.data.value("conclusion", std::string{}) + "\n" +
                                                    a.text}}})},
                        {"structuredContent", a.data},
                        {"isError", a.status == 2 || a.status == 3}}}});
            } else if (a.status == 2) {
                send(error_reply(id, -32602, a.data.value("error", std::string("bad parameters"))));
            } else {
                send({{"jsonrpc", "2.0"},
                      {"id", id},
                      {"result", {{"status", a.status}, {"data", a.data}}}});
            }
        });
    }
    return 0;
}

}  // namespace mcpplibs::mtest::engine
