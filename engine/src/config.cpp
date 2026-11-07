module mcpplibs.mtest.engine.config;

import std;
import tomlplusplus;
import mcpplibs.os;

namespace mcpplibs::mtest::engine {

namespace os = mcpplibs::os;

namespace {

template <class View>
std::vector<std::string> strings(const View& n) {
    std::vector<std::string> out;
    if (auto a = n.as_array())
        for (const auto& v : *a)
            if (auto s = v.template value<std::string>())
                out.push_back(*s);
    if (auto s = n.template value<std::string>())
        out.push_back(*s);
    return out;
}

template <class View>
std::optional<std::chrono::milliseconds> duration_of(const View& n) {
    if (auto s = n.template value<std::string>())
        return parse_duration(*s);
    if (auto i = n.template value<std::int64_t>())
        return std::chrono::milliseconds{*i * 1000};
    if (auto d = n.template value<double>())
        return std::chrono::milliseconds{static_cast<std::int64_t>(*d * 1000)};
    return std::nullopt;
}

void known(const toml::table& t, std::string_view where,
           std::initializer_list<std::string_view> keys, config& c) {
    for (const auto& [k, v] : t) {
        if (std::ranges::find(keys, k.str()) == keys.end())
            c.warnings.push_back(
                std::format("{}.{} is not a key this engine knows (ignored)", where, k.str()));
    }
}

}  // namespace

std::optional<std::chrono::milliseconds> parse_duration(std::string_view text) {
    double v = 0;
    auto [p, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
    if (ec != std::errc{})
        return std::nullopt;
    std::string_view unit(p, text.data() + text.size() - p);
    double ms = unit == "ms"                ? v
                : unit == "" || unit == "s" ? v * 1000
                : unit == "m"               ? v * 60'000
                : unit == "h"               ? v * 3'600'000
                                            : -1;
    if (ms < 0)
        return std::nullopt;
    return std::chrono::milliseconds{static_cast<std::int64_t>(ms)};
}

void add_builtin_sets(config& c) {
    auto add = [&](set_spec s) {
        if (!c.sets.contains(s.name))
            c.sets[s.name] = std::move(s);
    };
    set_spec smoke;
    smoke.name = "smoke";
    smoke.tags = {"smoke"};
    smoke.budget = std::chrono::seconds{60};
    add(smoke);
    set_spec regression;
    regression.name = "regression";
    regression.regression = true;
    add(regression);
    set_spec pr;
    pr.name = "pr";
    pr.affected = true;
    pr.include = {"smoke", "regression"};
    add(pr);
    set_spec full;
    full.name = "full";
    full.all = true;
    add(full);
    set_spec release;
    release.name = "release";
    release.all = true;
    release.gate_bench = true;
    add(release);
}

std::expected<config, std::string> load_config(const std::filesystem::path& dir) {
    config c;
    c.root = dir;
    const auto file = dir / "mtest.toml";
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        add_builtin_sets(c);
        return c;
    }
    toml::table t;
    try {
        t = toml::parse_file(os::fs::utf8(file));
    } catch (const toml::parse_error& e) {
        return std::unexpected(std::format("{}: {}", os::fs::utf8(file), e.description()));
    }
    known(t, "mtest.toml",
          {"project", "programs", "discover", "host", "sets", "policy", "retry", "regression",
           "store", "trace"},
          c);
    if (auto p = t["project"].as_table()) {
        known(*p, "project", {"store", "jobs"}, c);
        if (auto s = (*p)["store"].value<std::string>())
            c.store = *s;
        if (auto j = (*p)["jobs"].value<std::int64_t>())
            c.jobs = static_cast<int>(*j);
    }
    if (auto ps = t["programs"].as_table()) {
        for (const auto& [name, node] : *ps) {
            auto pt = node.as_table();
            if (!pt)
                return std::unexpected(std::format("programs.{} must be a table", name.str()));
            known(*pt, std::format("programs.{}", name.str()),
                  {"command", "cwd", "env", "level", "resources", "sources", "tags", "timeout",
                   "isolation"},
                  c);
            program_spec s;
            s.name = std::string(name.str());
            s.argv = strings((*pt)["command"]);
            if (s.argv.empty())
                return std::unexpected(
                    std::format("programs.{}.command names no program", name.str()));
            s.cwd = (*pt)["cwd"].value_or(std::string{});
            if (auto e = (*pt)["env"].as_table())
                for (const auto& [k, v] : *e)
                    if (auto sv = v.value<std::string>())
                        s.env.emplace_back(std::string(k.str()), *sv);
            if (auto l = (*pt)["level"].value<std::int64_t>())
                s.level = static_cast<int>(*l);
            s.resources = strings((*pt)["resources"]);
            s.sources = strings((*pt)["sources"]);
            s.tags = strings((*pt)["tags"]);
            if (auto d = duration_of((*pt)["timeout"]))
                s.timeout = std::chrono::duration_cast<std::chrono::seconds>(*d);
            s.isolation = (*pt)["isolation"].value_or(std::string("process"));
            c.programs.push_back(std::move(s));
        }
    }
    if (auto d = t["discover"].as_table()) {
        known(*d, "discover", {"globs"}, c);
        c.discover = strings((*d)["globs"]);
    }
    if (auto h = t["host"].as_table()) {
        known(*h, "host", {"kind", "args"}, c);
        c.host = (*h)["kind"].value_or(std::string{});
        c.host_args = strings((*h)["args"]);
    }
    if (auto ss = t["sets"].as_table()) {
        for (const auto& [name, node] : *ss) {
            auto st = node.as_table();
            if (!st)
                return std::unexpected(std::format("sets.{} must be a table", name.str()));
            known(*st, std::format("sets.{}", name.str()),
                  {"tags", "kinds", "programs", "cases", "include", "exclude_tags", "budget",
                   "affected", "all", "regression"},
                  c);
            set_spec s;
            s.name = std::string(name.str());
            s.tags = strings((*st)["tags"]);
            s.kinds = strings((*st)["kinds"]);
            s.programs = strings((*st)["programs"]);
            s.cases = strings((*st)["cases"]);
            s.include = strings((*st)["include"]);
            s.exclude_tags = strings((*st)["exclude_tags"]);
            s.budget = duration_of((*st)["budget"]);
            s.affected = (*st)["affected"].value_or(false);
            s.all = (*st)["all"].value_or(false);
            s.regression = (*st)["regression"].value_or(false);
            c.sets[s.name] = std::move(s);
        }
    }
    if (auto b = t["policy"]["bench"].as_table()) {
        known(*b, "policy.bench",
              {"pairs", "spread", "limit", "repeat_invalid", "repeat_valid", "baseline"}, c);
        c.bench.pairs = static_cast<int>((*b)["pairs"].value_or(std::int64_t{1}));
        c.bench.spread = (*b)["spread"].value_or(0.03);
        c.bench.limit = (*b)["limit"].value_or(1.15);
        c.bench.repeat_invalid = (*b)["repeat_invalid"].value_or(true);
        c.bench.repeat_valid = (*b)["repeat_valid"].value_or(false);
        c.bench.baseline = (*b)["baseline"].value_or(std::string("last-pass"));
    }
    if (auto r = t["retry"].as_table()) {
        known(*r, "retry", {"flaky"}, c);
        c.retries = static_cast<int>((*r)["flaky"].value_or(std::int64_t{0}));
    }
    if (auto r = t["regression"].as_table()) {
        known(*r, "regression", {"days"}, c);
        c.regression_days = static_cast<int>((*r)["days"].value_or(std::int64_t{30}));
    }
    if (auto s = t["store"].as_table()) {
        known(*s, "store", {"keep_days", "keep_runs"}, c);
        c.keep_days = static_cast<int>((*s)["keep_days"].value_or(std::int64_t{30}));
        c.keep_runs = static_cast<int>((*s)["keep_runs"].value_or(std::int64_t{200}));
    }
    if (auto tr = t["trace"].as_table()) {
        for (const auto& [name, node] : *tr) {
            auto tt = node.as_table();
            if (!tt)
                return std::unexpected(std::format("trace.{} must be a table", name.str()));
            known(*tt, std::format("trace.{}", name.str()), {"run_under", "build", "env"}, c);
            trace_tool tool;
            tool.name = std::string(name.str());
            tool.run_under = strings((*tt)["run_under"]);
            tool.build = strings((*tt)["build"]);
            if (auto e = (*tt)["env"].as_table())
                for (const auto& [k, v] : *e)
                    if (auto sv = v.value<std::string>())
                        tool.env.emplace_back(std::string(k.str()), *sv);
            c.tools[tool.name] = std::move(tool);
        }
    }
    add_builtin_sets(c);
    return c;
}

}  // namespace mcpplibs::mtest::engine
