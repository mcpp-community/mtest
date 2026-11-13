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

// The keys a program states, in `[programs.*]` and `[discover.*]` alike.
constexpr std::initializer_list<std::string_view> kProgramKeys = {"command",   "cwd",
                                                                  "env",       "level",
                                                                  "resources", "sources",
                                                                  "tags",      "timeout",
                                                                  "isolation", "kind",
                                                                  "split",     "skip_exit"};

void read_program(const toml::table& t, program_spec& s) {
    if (auto v = t["cwd"].value<std::string>())
        s.cwd = *v;
    if (auto e = t["env"].as_table())
        for (const auto& [k, v] : *e)
            if (auto sv = v.value<std::string>())
                s.env.emplace_back(std::string(k.str()), *sv);
    if (auto l = t["level"].value<std::int64_t>())
        s.level = static_cast<int>(*l);
    if (t.contains("resources"))
        s.resources = strings(t["resources"]);
    if (t.contains("sources"))
        s.sources = strings(t["sources"]);
    if (t.contains("tags"))
        s.tags = strings(t["tags"]);
    if (auto d = duration_of(t["timeout"]))
        s.timeout = std::chrono::duration_cast<std::chrono::seconds>(*d);
    if (auto v = t["isolation"].value<std::string>())
        s.isolation = *v;
    if (auto v = t["split"].value<bool>())
        s.split = *v;
    // 1..255: 0 is the status of a pass, and a status outside a byte is none a program exits with.
    if (auto v = t["skip_exit"].value<std::int64_t>(); v && *v >= 1 && *v <= 255)
        s.skip_exit = static_cast<int>(*v);
    if (auto v = t["kind"].value<std::string>())
        s.kind = *v;
}

// `[cases."<glob>"]`: the attributes of the cases a glob matches.
std::expected<void, std::string> read_attributes(const toml::table& cases, std::string_view where,
                                                 config& c) {
    for (const auto& [glob, node] : cases) {
        auto t = node.as_table();
        if (!t)
            return std::unexpected(std::format("{}.\"{}\" must be a table", where, glob.str()));
        known(*t, std::format("{}.\"{}\"", where, glob.str()),
              {"tags", "clauses", "resources", "artifacts", "kind", "timeout", "isolation",
               "retries", "diagnose", "skip", "expect_fail", "at_most", "at_least", "uses"},
              c);
        case_attributes a;
        a.glob = std::string(glob.str());
        a.tags = strings((*t)["tags"]);
        a.clauses = strings((*t)["clauses"]);
        a.resources = strings((*t)["resources"]);
        a.artifacts = strings((*t)["artifacts"]);
        a.uses = strings((*t)["uses"]);
        a.kind = (*t)["kind"].value_or(std::string{});
        if (auto d = duration_of((*t)["timeout"]))
            a.timeout = std::chrono::duration_cast<std::chrono::seconds>(*d);
        a.isolated = (*t)["isolation"].value_or(std::string{}) == "case";
        a.retries = static_cast<int>((*t)["retries"].value_or(std::int64_t{0}));
        if (auto d = (*t)["diagnose"].as_table()) {
            std::vector<std::pair<std::string, std::string>> env;
            if (auto e = (*d)["env"].as_table())
                for (const auto& [k, v] : *e)
                    if (auto sv = v.value<std::string>())
                        env.emplace_back(std::string(k.str()), *sv);
            a.diagnose_env = std::move(env);
            if (auto dt = duration_of((*d)["timeout"]))
                a.diagnose_timeout = std::chrono::duration<double>(*dt).count();
        }
        // at_most = { ready = 15 }, at_least = { recall = 0.9 }: bounds on the metrics (P6.5).
        for (const auto& [side, key] : {std::pair{"max", "at_most"}, std::pair{"min", "at_least"}})
            if (auto b = (*t)[key].as_table())
                for (const auto& [metric, v] : *b) {
                    bound_info one{std::string(metric.str()), std::nullopt, std::nullopt};
                    (std::string_view(side) == "max" ? one.max : one.min) = v.value<double>();
                    a.bounds.push_back(std::move(one));
                }
        a.skip = (*t)["skip"].value_or(std::string{});
        a.expect_fail = (*t)["expect_fail"].value_or(std::string{});
        a.origin = std::format("{}.\"{}\"", where, a.glob);
        c.attributes.push_back(std::move(a));
    }
    return {};
}

// A host's own statement of its cases: `[package.metadata.mtest]` of mcpp.toml (mcpp keeps the
// table for the tool it names).
std::expected<void, std::string> read_host_metadata(config& c) {
    const auto file = c.root / "mcpp.toml";
    std::error_code ec;
    if (!std::filesystem::exists(file, ec))
        return {};
    toml::table t;
    try {
        t = toml::parse_file(os::fs::utf8(file));
    } catch (const toml::parse_error& e) {
        return std::unexpected(std::format("{}: {}", os::fs::utf8(file), e.description()));
    }
    if (c.static_globs.empty()) {
        c.static_globs = strings(t["test"]["discover"]);
        if (c.static_globs.empty() && !t["test"]["discover"].as_array())
            c.static_globs = {"tests/**/*.cpp"};
    }
    auto meta = t["package"]["metadata"]["mtest"].as_table();
    if (!meta)
        return {};
    known(*meta, "mcpp.toml [package.metadata.mtest]", {"cases"}, c);
    if (auto cases = (*meta)["cases"].as_table())
        return read_attributes(*cases, "[package.metadata.mtest.cases]", c);
    return {};
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

std::optional<double> parse_size(std::string_view text) {
    double v = 0;
    auto [p, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
    if (ec != std::errc{} || v < 0)
        return std::nullopt;
    const std::string_view unit = text.substr(std::size_t(p - text.data()));
    if (unit == "GiB")
        return v * 1024 * 1024 * 1024;
    if (unit == "MiB")
        return v * 1024 * 1024;
    if (unit == "KiB")
        return v * 1024;
    if (unit == "GB")
        return v * 1e9;
    if (unit == "MB")
        return v * 1e6;
    if (unit == "B" || unit.empty())
        return v;
    return std::nullopt;
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
           "store", "trace", "cases", "coverage", "static", "run", "monitor", "report", "fixtures",
           "capabilities", "ci"},
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
            known(*pt, std::format("programs.{}", name.str()), kProgramKeys, c);
            program_spec s;
            s.name = std::string(name.str());
            s.argv = strings((*pt)["command"]);
            if (s.argv.empty())
                return std::unexpected(
                    std::format("programs.{}.command names no program", name.str()));
            read_program(*pt, s);
            c.programs.push_back(std::move(s));
        }
    }
    if (auto d = t["discover"].as_table()) {
        c.discover = strings((*d)["globs"]);
        for (const auto& [name, node] : *d) {
            if (name.str() == "globs")
                continue;
            auto gt = node.as_table();
            if (!gt) {
                c.warnings.push_back(
                    std::format("discover.{} is not a key this engine knows (ignored)",
                                name.str()));
                continue;
            }
            std::vector<std::string_view> keys(kProgramKeys);
            keys.insert(keys.end(), {"globs", "tags_from", "resources_from"});
            for (const auto& [k, v] : *gt)
                if (std::ranges::find(keys, k.str()) == keys.end())
                    c.warnings.push_back(
                        std::format("discover.{}.{} is not a key this engine knows (ignored)",
                                    name.str(), k.str()));
            discover_group g;
            g.name = std::string(name.str());
            g.globs = strings((*gt)["globs"]);
            g.command = strings((*gt)["command"]);
            read_program(*gt, g.base);
            if (auto tf = (*gt)["tags_from"].as_table())
                for (const auto& [prefix, tag] : *tf)
                    g.tags_from.emplace_back(std::string(prefix.str()),
                                             tag.value_or(std::string{}));
            g.resources_from = strings((*gt)["resources_from"]);
            if (g.globs.empty())
                return std::unexpected(std::format("discover.{}.globs names no file", g.name));
            c.groups.push_back(std::move(g));
        }
    }
    if (auto cs = t["cases"].as_table())
        if (auto e = read_attributes(*cs, "cases", c); !e)
            return std::unexpected(e.error());
    if (auto st = t["static"].as_table()) {
        known(*st, "static", {"globs"}, c);
        c.static_globs = strings((*st)["globs"]);
    }
    if (auto cv = t["coverage"].as_table()) {
        known(*cv, "coverage", {"objects", "database", "max_age"}, c);
        c.coverage.objects = (*cv)["objects"].value_or(std::string{});
        c.coverage.database = (*cv)["database"].value_or(std::string{});
        if (auto d = duration_of((*cv)["max_age"]))
            c.coverage.max_age = *d;
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
    if (auto r = t["run"].as_table()) {
        known(*r, "run", {"split", "memory"}, c);
        c.run.split = (*r)["split"].value_or(std::string("auto"));
        if (c.run.split != "auto" && c.run.split != "off")
            return std::unexpected("run.split is \"auto\" or \"off\"");
        c.run.memory = (*r)["memory"].value_or(std::string("auto"));
        if (c.run.memory != "auto" && c.run.memory != "off" && !parse_size(c.run.memory))
            return std::unexpected("run.memory is \"auto\", \"off\" or a size such as \"16GiB\"");
    }
    if (auto m = t["monitor"].as_table()) {
        known(*m, "monitor", {"level", "sample"}, c);
        c.monitor.level = (*m)["level"].value_or(std::string("basic"));
        if (auto d = duration_of((*m)["sample"]))
            c.monitor.sample_ms = int(std::max<long long>(1, d->count()));
        if (c.monitor.level != "off" && c.monitor.level != "basic" &&
            c.monitor.level != "detailed" && c.monitor.level != "trace")
            return std::unexpected("monitor.level is off, basic, detailed or trace");
    }
    if (auto r = t["report"].as_table()) {
        known(*r, "report", {"sections", "scope", "top", "max_nodes"}, c);
        if (r->contains("sections"))
            c.report.sections = strings((*r)["sections"]);
        c.report.scope = (*r)["scope"].value_or(std::string("run"));
        c.report.top = static_cast<int>((*r)["top"].value_or(std::int64_t{10}));
        c.report.max_nodes = static_cast<int>((*r)["max_nodes"].value_or(std::int64_t{60}));
    }
    if (auto caps = t["capabilities"].as_table()) {
        for (const auto& [name, v] : *caps) {
            if (name.str() == "tag") {
                if (auto s = v.value<std::string>())
                    c.capabilities.tag = *s;
                continue;
            }
            auto ct = v.as_table();
            if (!ct)
                return std::unexpected(std::format("capabilities.{} is not a table of tests (os, "
                                                   "arch, program, command, path, env, all, none)",
                                                   name.str()));
            known(*ct, std::format("capabilities.{}", name.str()),
                  {"os", "arch", "program", "command", "path", "env", "all", "none"}, c);
            capability_spec cs;
            cs.name = std::string(name.str());
            cs.systems = strings((*ct)["os"]);
            cs.archs = strings((*ct)["arch"]);
            cs.programs = strings((*ct)["program"]);
            cs.command = strings((*ct)["command"]);
            cs.paths = strings((*ct)["path"]);
            if (auto e = (*ct)["env"].as_table())
                for (const auto& [k, ev] : *e)
                    if (auto s = ev.value<std::string>())
                        cs.env.emplace_back(std::string(k.str()), *s);
            cs.all = strings((*ct)["all"]);
            cs.none = strings((*ct)["none"]);
            c.capabilities.words[cs.name] = std::move(cs);
        }
        for (const auto& [name, cs] : c.capabilities.words)
            for (const auto* list : {&cs.all, &cs.none})
                for (const auto& w : *list)
                    if (!c.capabilities.words.contains(w))
                        return std::unexpected(std::format("capabilities.{} names `{}`, which "
                                                           "[capabilities] does not declare",
                                                           name, w));
    }
    if (auto ci = t["ci"].as_table()) {
        known(*ci, "ci", {"coverage", "plan"}, c);
        if (auto cv = (*ci)["coverage"].as_table()) {
            known(*cv, "ci.coverage", {"exceptions", "debt", "hosts"}, c);
            c.ci_coverage.exceptions = (*cv)["exceptions"].value_or(std::string{});
            c.ci_coverage.debt = (*cv)["debt"].value_or(std::string{});
            c.ci_coverage.hosts = (*cv)["hosts"].value_or(std::string{});
        }
        if (auto pl = (*ci)["plan"].as_table()) {
            known(*pl, "ci.plan", {"hosts", "shard", "max_shards", "unknown"}, c);
            c.ci_plan.hosts = strings((*pl)["hosts"]);
            if (auto d = duration_of((*pl)["shard"]))
                c.ci_plan.shard = *d;
            if (auto m = (*pl)["max_shards"].value<std::int64_t>())
                c.ci_plan.max_shards = std::max<int>(1, int(*m));
            if (auto d = duration_of((*pl)["unknown"]))
                c.ci_plan.unknown = *d;
        }
    }
    if (auto fs = t["fixtures"].as_table())
        for (const auto& [name, node] : *fs) {
            auto ft = node.as_table();
            if (!ft)
                return std::unexpected(std::format("fixtures.{} must be a table", name.str()));
            known(*ft, std::format("fixtures.{}", name.str()), {"command"}, c);
            fixture_spec f;
            f.name = std::string(name.str());
            f.command = strings((*ft)["command"]);
            if (f.command.empty())
                return std::unexpected(std::format("fixtures.{}.command names no program", f.name));
            c.fixtures.push_back(std::move(f));
        }
    if (c.host == "mcpp")
        if (auto e = read_host_metadata(c); !e)
            return std::unexpected(e.error());
    add_builtin_sets(c);
    return c;
}

}  // namespace mcpplibs::mtest::engine
