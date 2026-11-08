// mtest: the engine's command line (docs/commands.md). The commands and their options are the
// catalog's (catalog()): each command is a subcommand and each of its parameters an option, so the
// CLI, the service and the MCP tools take the same parameters and give one answer (dispatch()).
// `--json` prints the data instead of the text.
import std;
import mcpplibs.cmdline;
import mcpplibs.mtest.engine.model;
import mcpplibs.mtest.engine.commands;
import mcpplibs.mtest.engine.service;

namespace cmdline = mcpplibs::cmdline;
using namespace mcpplibs::mtest::engine;

namespace {

// The parameter a command's first word gives; the words after it are programs (a selection's)
// or, for `ci`, pull requests.
constexpr std::array<std::pair<std::string_view, std::string_view>, 7> first_word = {{
    {"quarantine", "action"},
    {"store", "action"},
    {"coverage", "action"},
    {"analyze", "metric"},
    {"ci", "action"},
    {"explain", "case"},
    {"history", "case"},
}};

// A command of the command line that is another's with one parameter given.
struct shorthand {
    std::string_view name, of, key, value;
};
constexpr std::array<shorthand, 2> shorthands = {{
    {"hot", "analyze", "metric", "hot"},
    {"path", "analyze", "metric", "path"},
}};

// Parameters whose value on the command line is a command line, split into words.
constexpr std::array<std::string_view, 1> command_lines = {"run_under"};

std::string option_of(std::string key) {
    std::ranges::replace(key, '_', '-');
    return key;
}

// The option of a property inside an object parameter (`report.junit`): its own name, unless that
// is the global `--json`, which becomes `--report-json`.
std::string nested_option(std::string_view parent, std::string_view key) {
    return key == "json" ? option_of(std::format("{}_{}", parent, key))
                         : option_of(std::string(key));
}

std::string help_of(const json& schema) {
    std::string help = schema.value("description", "");
    if (auto e = schema.find("enum"); e != schema.end()) {
        std::string values;
        for (const auto& v : *e)
            values += (values.empty() ? "" : ", ") + v.get<std::string>();
        help += (help.empty() ? "" : ": ") + values;
    }
    return help;
}

void add_option(cmdline::App& app, const std::string& name, const json& schema) {
    const auto type = schema.value("type", "string");
    auto option = cmdline::Option(name).help(help_of(schema));
    if (type != "boolean")
        option = option.takes_value().value_name(type == "integer" ? "N" : "VALUE");
    if (type == "array" || type == "object")
        option = option.multiple();
    app = std::move(app).option(std::move(option));
}

cmdline::App command(std::string_view name, const json& tool) {
    auto app = cmdline::App(name).description(tool.value("description", ""));
    for (const auto& [command, key] : first_word)
        if (command == name)
            app = std::move(app).arg(cmdline::Arg(key));
    const auto& properties = tool["inputSchema"]["properties"];
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        if (it.key() == "argv")
            continue;  // the words after `--`
        if (auto nested = it.value().find("properties"); nested != it.value().end()) {
            for (auto n = nested->begin(); n != nested->end(); ++n)
                add_option(app, nested_option(it.key(), n.key()), n.value());
            continue;
        }
        add_option(app, option_of(it.key()), it.value());
    }
    return app;
}

const json* tool_named(const json& tools, std::string_view name) {
    for (const auto& t : tools)
        if (t["name"] == name)
            return &t;
    return nullptr;
}

cmdline::App application(const json& tools) {
    auto app = cmdline::App("mtest")
                   .version("0.4.0")
                   .description("run tests of any language, and ask about their runs")
                   .option(cmdline::Option("json").global().help("print the answer's data"));
    for (const auto& t : tools)
        app = std::move(app).subcommand(command(t["name"].get<std::string>(), t));
    for (const auto& s : shorthands)
        if (const auto* of = tool_named(tools, s.of))
            app = std::move(app).subcommand(
                command(s.name, {{"description", std::format("{} {}", s.of, s.value)},
                                 {"inputSchema", (*of)["inputSchema"]}}));
    // The step `git bisect run` repeats, with bisect's parameters.
    if (const auto* bisect = tool_named(tools, "bisect"))
        app = std::move(app).subcommand(
            command("bisect-step", {{"description", "one step of bisect (git bisect run)"},
                                    {"inputSchema", (*bisect)["inputSchema"]}}));
    app = std::move(app)
              .subcommand(
                  cmdline::App("serve").description("JSON-RPC and MCP on the standard streams"))
              .subcommand(cmdline::App("version").description("the engine's version"));
    return app;
}

// A value of the command line as its schema's type; a usage error when it is not one.
std::expected<json, std::string> typed(const std::string& option, const json& schema,
                                       const std::string& value) {
    const auto type = schema.value("type", "string");
    if (type == "integer") {
        std::int64_t v = 0;
        auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), v);
        if (ec != std::errc{} || end != value.data() + value.size())
            return std::unexpected(std::format("--{} takes a number", option));
        return v;
    }
    if (type == "number") {
        double v = 0;
        auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), v);
        if (ec != std::errc{} || end != value.data() + value.size())
            return std::unexpected(std::format("--{} takes a number", option));
        return v;
    }
    if (auto e = schema.find("enum");
        e != schema.end() && std::ranges::find(*e, json(value)) == e->end())
        return std::unexpected(std::format("--{} takes one of: {}", option, help_of(schema)));
    return value;
}

// One parameter from its option, into `params[key]`.
std::expected<void, std::string> take(const cmdline::ParsedArgs& parsed, const std::string& key,
                                      const std::string& option, const json& schema, json& params) {
    const auto given = parsed.option_or_empty(option);
    if (!given.is_set())
        return {};
    const auto type = schema.value("type", "string");
    if (type == "boolean") {
        params[key] = true;
        return {};
    }
    if (type == "object") {  // NAME=VALUE pairs
        for (const auto& pair : given.values) {
            const auto eq = pair.find('=');
            if (eq == std::string::npos)
                return std::unexpected(std::format("--{} takes NAME=VALUE", option));
            params[key][pair.substr(0, eq)] = pair.substr(eq + 1);
        }
        return {};
    }
    if (type == "array") {
        const bool words = std::ranges::contains(command_lines, key);
        const json items = schema.value("items", json::object());
        for (const auto& value : given.values) {
            if (!words) {
                auto v = typed(option, items, value);
                if (!v)
                    return std::unexpected(v.error());
                params[key].push_back(*v);
                continue;
            }
            for (auto word : std::views::split(value, ' '))
                if (!word.empty())
                    params[key].push_back(std::string(word.begin(), word.end()));
        }
        return {};
    }
    auto v = typed(option, schema, given.value());
    if (!v)
        return std::unexpected(v.error());
    params[key] = *v;
    return {};
}

// The parameters of a command from what the command line gave.
std::expected<json, std::string> parameters(std::string_view name, const json& tool,
                                            const cmdline::ParsedArgs& parsed) {
    json params = json::object();
    const auto& properties = tool["inputSchema"]["properties"];
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        if (auto nested = it.value().find("properties"); nested != it.value().end()) {
            for (auto n = nested->begin(); n != nested->end(); ++n) {
                json inner = params.value(it.key(), json::object());
                auto r = take(parsed, n.key(), nested_option(it.key(), n.key()), n.value(), inner);
                if (!r)
                    return std::unexpected(r.error());
                if (!inner.empty())
                    params[it.key()] = inner;
            }
            continue;
        }
        if (auto r = take(parsed, it.key(), option_of(it.key()), it.value(), params); !r)
            return std::unexpected(r.error());
    }
    std::vector<std::string> words = parsed.positionals;
    for (const auto& [command, key] : first_word)
        if (command == name && !words.empty()) {
            auto v = typed(std::string(key), properties[std::string(key)], words.front());
            if (!v)
                return std::unexpected(v.error());
            params[std::string(key)] = *v;
            words.erase(words.begin());
        }
    for (auto& w : words)
        params[name == "ci" ? "prs" : "programs"].push_back(w);
    return params;
}

}  // namespace

int main(int argc, char** argv) {
    // The words after `--` are one program and its arguments, not the command line's.
    cmdline::Argv words(argv, argv + argc);
    std::vector<std::string> program;
    if (auto dash = std::ranges::find(words, "--"); dash != words.end()) {
        program.assign(dash + 1, words.end());
        words.erase(dash, words.end());
    }
    const json tools = catalog()["tools"];
    const auto app = application(tools);
    if (words.size() < 2 || words[1] == "help") {
        app.print_help();
        return words.size() < 2 ? 2 : 0;
    }
    auto parsed = app.parse_from(words);
    if (!parsed) {
        if (parsed.error().is_error()) {
            std::println(std::cerr, "mtest: {}", parsed.error().message);
            return 2;
        }
        return 0;  // --help or --version, printed
    }
    if (!parsed->has_subcommand()) {
        std::println(std::cerr, "mtest: no command {} (mtest help)", parsed->positional(0));
        return 2;
    }
    const std::string name(parsed->subcommand_name());
    const auto& sub = parsed->subcommand()->get();
    if (name == "version") {
        std::println("mtest 0.4.0");
        return 0;
    }
    const auto cwd = std::filesystem::current_path();
    if (name == "serve")
        return serve(cwd);

    std::string dispatched = name;
    json fixed = json::object();
    for (const auto& s : shorthands)
        if (s.name == name) {
            dispatched = s.of;
            fixed[std::string(s.key)] = s.value;
        }
    const json* tool = tool_named(tools, name == "bisect-step" ? "bisect" : dispatched);
    auto params = parameters(name, *tool, sub);
    if (!params) {
        std::println(std::cerr, "mtest: {}", params.error());
        return 2;
    }
    params->update(fixed);
    if (!program.empty())
        (*params)["argv"] = program;
    auto a = dispatch(cwd, name == "bisect-step" ? name : dispatched, *params, {});
    if (parsed->is_flag_set("json"))
        std::println("{}", dumped(a.data, 2));
    else if (a.status == 2)
        std::print(std::cerr, "{}", a.text);
    else
        std::print("{}", a.text);
    return a.status;
}
