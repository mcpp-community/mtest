module mcpplibs.mtest.engine.service;

import std;
import mcpplibs.mtest.engine.model;
import mcpplibs.mtest.engine.commands;
import mcpplibs.mtest.engine.session;

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
    // The test session (mcpp test spec O9), made on its first method.
    std::unique_ptr<session> tests;
    std::mutex tests_lock;
    auto the_session = [&]() -> session& {
        std::lock_guard g(tests_lock);
        if (!tests)
            tests = std::make_unique<session>([](std::string_view method, const json& params) {
                send({{"jsonrpc", "2.0"}, {"method", std::string(method)}, {"params", params}});
            });
        return *tests;
    };
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
                    {"serverInfo", {{"name", "mtest"}, {"version", "0.6.3"}}},
                    {"instructions",
                     "Run tests and ask about runs: run, plan, explain, history, analyze, graph, "
                     "gate, ci. A test session (test/plan, test/run, test/add, test/cancel, "
                     "test/status) runs an mcpp project's cases as they are asked for. "
                     "Each result carries a `conclusion`; a failure's `again` repeats it."}}}});
            continue;
        }
        if (method == "notifications/initialized" || method == "initialized")
            continue;
        if (method == "shutdown") {
            {
                std::lock_guard g(tests_lock);
                if (tests)
                    tests->shutdown();
            }
            send({{"jsonrpc", "2.0"}, {"id", id}, {"result", nullptr}});
            continue;
        }
        if (method.starts_with("test/")) {
            const auto verb = method.substr(5);
            requests.emplace_back([&, id, verb, params, notification] {
                auto& t = the_session();
                json r = verb == "plan"     ? t.plan(params)
                         : verb == "run"    ? t.run(params)
                         : verb == "add"    ? t.add(params)
                         : verb == "cancel" ? t.cancel(params)
                         : verb == "status"
                             ? t.status(params)
                             : json{{"error", std::format("no method test/{}", verb)}};
                if (notification)
                    return;
                if (r.contains("error"))
                    send(error_reply(id, -32602, r["error"].get<std::string>()));
                else
                    send({{"jsonrpc", "2.0"}, {"id", id}, {"result", r}});
            });
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
    // The input ended: what was asked for runs to its end (`shutdown` is how a client cancels).
    requests.clear();
    {
        std::lock_guard g(tests_lock);
        if (tests) {
            tests->drain();
            tests->shutdown();
        }
    }
    return 0;
}

}  // namespace mcpplibs::mtest::engine
