module mcpplibs.mtest.engine.select;

import std;
import mcpplibs.os;
import mcpplibs.mtest.engine.exec;
import mcpplibs.mtest.engine.coverage;

namespace mcpplibs::mtest::engine {

namespace os = mcpplibs::os;

namespace {

// `[abc]`, `[a-z]` and `[!x]` at `p[pi]`: whether `c` is in the class, and where the class ends;
// none when `p[pi]` opens no class.
std::optional<std::pair<bool, std::size_t>> char_class(std::string_view p, std::size_t pi, char c) {
    if (pi >= p.size() || p[pi] != '[')
        return std::nullopt;
    std::size_t i = pi + 1;
    const bool negated = i < p.size() && (p[i] == '!' || p[i] == '^');
    if (negated)
        ++i;
    bool in = false;
    for (bool first = true; i < p.size() && (first || p[i] != ']'); first = false, ++i) {
        if (i + 2 < p.size() && p[i + 1] == '-' && p[i + 2] != ']') {
            in = in || (p[i] <= c && c <= p[i + 2]);
            i += 2;
        } else
            in = in || p[i] == c;
    }
    if (i >= p.size())
        return std::nullopt;  // no `]`: a literal `[`
    return std::pair{in != negated, i + 1};
}

}  // namespace

bool glob_match(std::string_view p, std::string_view t) {
    std::size_t pi = 0, ti = 0, star = std::string_view::npos, mark = 0;
    while (ti < t.size()) {
        if (auto cls = char_class(p, pi, t[ti])) {
            if (cls->first) {
                pi = cls->second;
                ++ti;
                continue;
            }
            if (star == std::string_view::npos)
                return false;
            pi = star + 1;
            ti = ++mark;
        } else if (pi < p.size() && (p[pi] == '?' || p[pi] == t[ti])) {
            ++pi;
            ++ti;
        } else if (pi < p.size() && p[pi] == '*') {
            star = pi++;
            mark = ti;
        } else if (star != std::string_view::npos) {
            pi = star + 1;
            ti = ++mark;
        } else
            return false;
    }
    while (pi < p.size() && p[pi] == '*')
        ++pi;
    return pi == p.size();
}

std::expected<std::vector<std::string>, std::string>
changed_since(const std::filesystem::path& root, std::string_view base) {
    os::process::spawn_options o;
    o.program = "git";
    o.args = {"diff", "--name-only", std::format("{}...HEAD", base)};
    o.cwd = root;
    o.in = os::process::stdio::null;
    auto r = os::process::run(std::move(o));
    if (!r || !r->status.success())
        return std::unexpected(std::format("git diff --name-only {}...HEAD did not answer{}", base,
                                           r ? ": " + r->err : std::string{}));
    std::vector<std::string> paths;
    std::string_view rest = r->out;
    while (!rest.empty()) {
        auto nl = rest.find('\n');
        auto line = rest.substr(0, nl);
        rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
        if (!line.empty())
            paths.emplace_back(line);
    }
    return paths;
}

namespace {

// mcpp's tests, by mcpp's own description of each launch (mcpp 2026.11.8.1+: `mcpp test --no-run
// --message-format json` writes one `test` record per built test with its `launch`).
std::expected<std::vector<program_spec>, std::string> mcpp_programs(const config& c) {
    os::process::spawn_options o;
    o.program = "mcpp";
    o.args = {"test", "--no-run", "--message-format", "json"};
    o.args.insert(o.args.end(), c.host_args.begin(), c.host_args.end());
    o.cwd = c.root;
    o.in = os::process::stdio::null;
    os::process::bounds limits;
    limits.hard = std::chrono::minutes{60};
    auto r = os::process::run(std::move(o), limits);
    if (!r)
        return std::unexpected("mcpp did not start: " + r.error().message());
    if (!r->status.success())
        return std::unexpected("mcpp test --no-run failed:\n" +
                               r->err.substr(r->err.size() > 4000 ? r->err.size() - 4000 : 0));
    std::vector<program_spec> out;
    std::string_view rest = r->out;
    while (!rest.empty()) {
        auto nl = rest.find('\n');
        auto line = rest.substr(0, nl);
        rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
        auto j = json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("launch"))
            continue;
        const auto& l = j["launch"];
        program_spec s;
        s.name = j.value("member", std::string{}).empty()
                     ? j.value("test", "")
                     : j.value("member", "") + "/" + j.value("test", "");
        if (l.contains("argv"))
            for (const auto& a : l["argv"])
                if (a.is_string())
                    s.argv.push_back(a.get<std::string>());
        s.cwd = l.value("cwd", "");
        if (l.contains("env") && l["env"].is_object())
            for (auto& item_ : l["env"].items())
                if (auto& k = item_.key(); true)
                    if (auto& v = item_.value(); true)
                        if (v.is_string())
                            s.env.emplace_back(k, v.get<std::string>());
        if (j.contains("protocol") && j["protocol"].is_number_integer())
            s.level = j["protocol"].get<int>() >= 1 ? 3 : 0;
        if (j.contains("sources"))
            for (const auto& a : j["sources"])
                if (a.is_string())
                    s.sources.push_back(a.get<std::string>());
        s.origin = "host";
        if (!s.argv.empty())
            out.push_back(std::move(s));
    }
    if (out.empty())
        return std::unexpected(
            "mcpp described no test launch: the host `mcpp` needs mcpp 2026.11.8.1 or later "
            "(`mcpp test --no-run --message-format json` with `launch`)");
    return out;
}

bool executable_file(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec))
        return false;
    // The system the engine runs upon, which an image learns when it is loaded (P4 of the ecosystem
    // design).
    if (os::host() == os::platform::windows)
        return p.extension() == ".exe";
    const auto perms = std::filesystem::status(p, ec).permissions();
    return (perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
}

// Each word after `prefix` on the file's first lines.
std::vector<std::string> header_words(const std::filesystem::path& file, std::string_view prefix) {
    std::vector<std::string> out;
    std::ifstream in(file, std::ios::binary);
    std::string line;
    for (int n = 0; n < 64 && std::getline(in, line); ++n) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.starts_with(prefix))
            continue;
        std::istringstream words(line.substr(prefix.size()));
        for (std::string w; words >> w;)
            out.push_back(w);
    }
    return out;
}

void add_unique(std::vector<std::string>& to, const std::vector<std::string>& from) {
    for (const auto& x : from)
        if (std::ranges::find(to, x) == to.end())
            to.push_back(x);
}

std::string substitute(std::string word, std::string_view token, const std::string& value) {
    for (auto at = word.find(token); at != std::string::npos;
         at = word.find(token, at + value.size()))
        word.replace(at, token.size(), value);
    return word;
}

// The programs of a `[discover.<name>]` group: each file its globs match, by its command.
void discover_group_programs(const config& c, const discover_group& g,
                             std::vector<program_spec>& out) {
    std::error_code ec;
    std::vector<std::pair<std::string, std::filesystem::path>> files;
    for (std::filesystem::recursive_directory_iterator it(c.root, ec), end; it != end;
         it.increment(ec)) {
        const auto rel = os::fs::generic_utf8(std::filesystem::relative(it->path(), c.root, ec));
        const auto top = rel.substr(0, rel.find('/'));
        if (top == ".git" || top == ".mtest" || top == "target") {
            if (it->is_directory())
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec) && std::ranges::any_of(g.globs, [&](const std::string& p) {
                return path_glob_match(p, rel);
            }))
            files.emplace_back(rel, it->path());
    }
    std::ranges::sort(files);
    for (const auto& [rel, path] : files) {
        program_spec s = g.base;
        s.name = rel;
        const auto absolute = os::fs::generic_utf8(std::filesystem::absolute(path, ec));
        if (g.command.empty())
            s.argv = {absolute};
        for (const auto& word : g.command)
            s.argv.push_back(substitute(substitute(word, "{path}", absolute), "{name}", rel));
        for (const auto& [prefix, tag] : g.tags_from) {
            std::vector<std::string> tags;
            for (const auto& w : header_words(path, prefix))
                tags.push_back(tag + w);
            add_unique(s.tags, tags);
        }
        for (const auto& prefix : g.resources_from)
            add_unique(s.resources, header_words(path, prefix));
        s.origin = "discover." + g.name;
        out.push_back(std::move(s));
    }
}

}  // namespace

bool path_glob_match(std::string_view pattern, std::string_view path) {
    if (glob_match(pattern, path))
        return true;
    const auto dirs = pattern.find("**/");
    if (dirs == std::string_view::npos)
        return false;
    std::string none(pattern.substr(0, dirs));
    none += pattern.substr(dirs + 3);
    return path_glob_match(none, path);
}

std::expected<std::pair<int, int>, std::string> parse_shard(std::string_view text) {
    const auto slash = text.find('/');
    int i = 0, n = 0;
    if (slash != std::string_view::npos) {
        std::from_chars(text.data(), text.data() + slash, i);
        std::from_chars(text.data() + slash + 1, text.data() + text.size(), n);
    }
    if (n < 1 || i < 1 || i > n)
        return std::unexpected(std::format("--shard {} is not I/N with 1 <= I <= N", text));
    return std::pair{i, n};
}

namespace {

// The units of one part: each unit, the longest first, goes to the part with the least expected
// time (the lowest-numbered of equals), so the parts end close together.
void keep_shard(plan& out, int index, int count) {
    std::vector<std::size_t> order(out.units.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) {
        if (out.units[a].expected_ms != out.units[b].expected_ms)
            return out.units[a].expected_ms > out.units[b].expected_ms;
        return out.units[a].program.name < out.units[b].program.name;
    });
    std::vector<double> load(static_cast<std::size_t>(count), 0.0);
    std::vector<bool> mine(out.units.size(), false);
    for (auto u : order) {
        const auto part = static_cast<std::size_t>(std::ranges::min_element(load) - load.begin());
        load[part] += out.units[u].expected_ms;
        mine[u] = part + 1 == static_cast<std::size_t>(index);
    }
    std::vector<unit> kept;
    std::size_t cases = 0;
    for (std::size_t u = 0; u < out.units.size(); ++u)
        if (mine[u]) {
            cases += out.units[u].cases.size();
            kept.push_back(std::move(out.units[u]));
        }
    out.notes.push_back(std::format("shard {}/{}: {} of {} programs, {} case(s), about {:.0f} s",
                                    index, count, kept.size(), out.units.size(), cases,
                                    load[static_cast<std::size_t>(index - 1)] / 1000));
    out.selected = cases;
    out.units = std::move(kept);
}

}  // namespace

// A case's kind and time limit are the program's statement when it makes one (docs/config.md
// "Case attributes"); tags and resources add up.
void apply_attributes(const config& c, const program_spec& program, case_info& ci) {
    for (const auto& a : c.attributes) {
        if (!glob_match(a.glob, ci.id) && !glob_match(a.glob, program.name + "::" + ci.id))
            continue;
        add_unique(ci.tags, a.tags);
        add_unique(ci.resources, a.resources);
        if (!a.kind.empty() && ci.kind.empty())
            ci.kind = a.kind;
        if (a.timeout && ci.timeout <= 0)
            ci.timeout = double(a.timeout->count());
        add_unique(ci.clauses, a.clauses);
        ci.bounds.insert(ci.bounds.end(), a.bounds.begin(), a.bounds.end());
        ci.stated_bounds.insert(ci.stated_bounds.end(), a.bounds.begin(), a.bounds.end());
        add_unique(ci.artifacts, a.artifacts);
        ci.isolated = ci.isolated || a.isolated;
        if (a.retries > 0 && ci.retries == 0)
            ci.retries = a.retries;
        if (a.diagnose_env && !ci.diagnose_env) {
            ci.diagnose_env = a.diagnose_env;
            ci.diagnose_timeout = a.diagnose_timeout;
        }
        // The program's own verdict comes first: a case it lists as skipped or failing keeps it.
        if (!a.skip.empty() && ci.expect.empty() && ci.skip.empty())
            ci.skip = a.skip;
        if (!a.expect_fail.empty() && ci.expect.empty() && ci.expect_fail.empty())
            ci.expect_fail = a.expect_fail;
    }
    if (ci.kind.empty())
        ci.kind = program.kind.empty() ? "unit" : program.kind;
}

std::string layout_kind(std::string_view path) {
    std::string_view previous;
    for (const auto part : std::views::split(path, '/')) {
        const std::string_view word(part.begin(), part.end());
        if (previous == "tests" &&
            (word == "unit" || word == "e2e" || word == "cts" || word == "bench"))
            return std::string(word);
        previous = word;
    }
    return {};
}

void apply_attributes(const config& c, program_spec& p) {
    for (const auto& a : c.attributes) {
        if (!glob_match(a.glob, p.name) && !glob_match(a.glob, p.name + "::" + p.name))
            continue;
        add_unique(p.tags, a.tags);
        add_unique(p.resources, a.resources);
        if (!a.kind.empty())
            p.kind = a.kind;
        if (a.timeout)
            p.timeout = *a.timeout;
    }
}

std::expected<std::vector<program_spec>, std::string> discover(const config& c,
                                                               const program_source& given) {
    std::vector<program_spec> out;
    for (const auto& cmd : given.commands) {
        if (cmd.empty())
            continue;
        program_spec s;
        // A program given with its arguments is named by the whole command: two commands of one
        // program are two.
        s.name = os::fs::generic_utf8(os::fs::from_utf8(cmd.front()));
        for (std::size_t i = 1; i < cmd.size(); ++i)
            s.name += " " + cmd[i];
        s.argv = cmd;
        s.origin = "given";
        out.push_back(std::move(s));
    }
    if (!given.commands.empty())
        return out;
    out.insert(out.end(), c.programs.begin(), c.programs.end());
    for (const auto& g : c.discover) {
        std::error_code ec;
        for (std::filesystem::recursive_directory_iterator it(c.root, ec), end; it != end;
             it.increment(ec)) {
            const auto rel =
                os::fs::generic_utf8(std::filesystem::relative(it->path(), c.root, ec));
            if (rel.starts_with(".git/") || rel.starts_with(".mtest/")) {
                if (it->is_directory())
                    it.disable_recursion_pending();
                continue;
            }
            if (!path_glob_match(g, rel) || !executable_file(it->path()))
                continue;
            program_spec s;
            s.name = rel;
            s.argv = {os::fs::utf8(it->path())};
            s.origin = "discover";
            out.push_back(std::move(s));
        }
    }
    for (const auto& g : c.groups)
        discover_group_programs(c, g, out);
    if (given.host && c.host == "mcpp") {
        auto host = mcpp_programs(c);
        if (!host)
            return std::unexpected(host.error());
        out.insert(out.end(), host->begin(), host->end());
    } else if (given.host && !c.host.empty()) {
        return std::unexpected(
            std::format("host.kind = \"{}\" is not a host this engine knows (mcpp)", c.host));
    }
    for (auto& p : out) {
        // The place first: a kind mtest.toml states is applied over it.
        if (p.kind.empty()) {
            std::vector<std::string> places = p.sources;
            places.push_back(p.name);
            if (p.origin == "host")
                places.push_back("tests/" + p.name);
            for (auto place : places) {
                std::ranges::replace(place, '\\', '/');
                if (p.kind = layout_kind(place); !p.kind.empty())
                    break;
            }
        }
        apply_attributes(c, p);
        if (p.kind.empty())
            p.kind = "unit";
    }
    return out;
}

namespace {

struct history {
    std::map<std::string, double> ms;       // program::id -> last duration
    std::set<std::string> failed_last;      // program::id that failed in the last run
    std::set<std::string> failed_recently;  // within regression_days
};

history read_history(const config& c, const store& s) {
    history h;
    const auto all = runs(s);
    const auto cutoff =
        std::format("{:%Y%m%d}",
                    std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now() -
                                                          std::chrono::days{c.regression_days}));
    for (std::size_t i = all.size(); i-- > 0;) {
        if (all[i].id.substr(0, 8) < cutoff && i + 1 < all.size())
            break;
        auto r = load_run(s, all[i].id);
        if (!r)
            continue;
        const bool last = i + 1 == all.size();
        for (const auto& cr : r->cases) {
            const auto key = cr.program + "::" + cr.id;
            if (!h.ms.contains(key))
                h.ms[key] = cr.ms;
            if (failing(cr.status)) {
                h.failed_recently.insert(key);
                if (last)
                    h.failed_last.insert(key);
            }
        }
    }
    return h;
}

bool any_glob(const std::vector<std::string>& patterns, std::string_view text) {
    return std::ranges::any_of(patterns, [&](const std::string& p) { return glob_match(p, text); });
}

bool has_any(const std::vector<std::string>& have, const std::vector<std::string>& want) {
    return std::ranges::any_of(want, [&](const std::string& w) {
        return std::ranges::find(have, w) != have.end();
    });
}

// Whether a change of `paths` affects a case: a file its last measured run executed (the coverage
// map), a `file:` artifact under a changed path, or a source of its program; a case whose program
// states nothing it reads, and the map does not know, is affected (nothing is known against it).
std::optional<std::string> affected(const program_spec& p, const case_info& c,
                                    const std::vector<std::string>& paths,
                                    const std::optional<coverage_map>& coverage) {
    bool stated = !p.sources.empty();
    if (coverage)
        if (auto ran = coverage->cases.find(p.name + "::" + c.id); ran != coverage->cases.end()) {
            stated = true;
            for (const auto& path : paths)
                if (std::ranges::binary_search(ran->second, path))
                    return std::format("{} ran {} (coverage {})", c.id, path,
                                       coverage->commit.substr(0, 12));
        }
    for (const auto& a : c.artifacts) {
        if (!a.starts_with("file:"))
            continue;
        stated = true;
        const auto f = a.substr(5);
        for (const auto& path : paths)
            if (path == f || path.starts_with(f.ends_with('/') ? f : f + "/") ||
                glob_match(f, path))
                return std::format("{} reads {}", c.id, path);
    }
    for (const auto& src : p.sources)
        for (const auto& path : paths)
            if (glob_match(src, path) || path.starts_with(src.ends_with('/') ? src : src + "/"))
                return std::format("{} is built from {}", p.name, path);
    if (!stated)
        return std::format("{} states nothing it reads", p.name);
    return std::nullopt;
}

}  // namespace

std::expected<plan, std::string> make_plan(const config& c, const store& s,
                                           const std::vector<program_spec>& programs,
                                           const selection_request& r) {
    plan out;
    const auto h = read_history(c, s);
    const auto coverage = r.changed ? fresh_coverage(s, c.coverage.max_age) : std::nullopt;
    // A case with no history is expected to take what the median known case takes.
    const double unknown_ms = [&] {
        std::vector<double> known;
        for (const auto& [_, ms] : h.ms)
            known.push_back(ms);
        if (known.empty())
            return 1000.0;
        std::ranges::nth_element(known, known.begin() + known.size() / 2);
        return known[known.size() / 2];
    }();
    std::vector<set_spec> sets;
    std::function<std::expected<void, std::string>(const std::string&, int)> add_set =
        [&](const std::string& name, int depth) -> std::expected<void, std::string> {
        if (depth > 8)
            return std::unexpected(std::format("sets include each other without end at {}", name));
        auto it = c.sets.find(name);
        if (it == c.sets.end())
            return std::unexpected(std::format("no set named {}", name));
        sets.push_back(it->second);
        for (const auto& inc : it->second.include)
            if (auto e = add_set(inc, depth + 1); !e)
                return e;
        return {};
    };
    if (!r.set.empty())
        if (auto e = add_set(r.set, 0); !e)
            return std::unexpected(e.error());

    std::vector<program_spec> pool;
    for (const auto& p : programs)
        if ((r.programs.empty() || any_glob(r.programs, p.name)) &&
            !any_glob(r.exclude_programs, p.name))
            pool.push_back(p);
    for (const auto& p : pool) {
        auto listed =
            list_program(p,
                         s.dir / "work" / "list" / std::to_string(std::hash<std::string>{}(p.name)),
                         c.root);
        if (!listed)
            return std::unexpected(listed.error());
        unit u;
        u.program = p;
        u.program.level = detect_level(p);
        for (auto ci : *listed) {
            apply_attributes(c, u.program, ci);
            ++out.considered;
            const auto key = p.name + "::" + ci.id;
            auto pick = [&]() -> std::optional<std::string> {
                if (r.failed_only)
                    return h.failed_last.contains(key)
                               ? std::optional<std::string>("failed in the last run")
                               : std::nullopt;
                if (!r.cases.empty() && !any_glob(r.cases, ci.id) && !any_glob(r.cases, key))
                    return std::nullopt;
                if (!r.tags.empty() && !has_any(ci.tags, r.tags) && !has_any(p.tags, r.tags))
                    return std::nullopt;
                if (!r.kinds.empty() && std::ranges::find(r.kinds, ci.kind) == r.kinds.end())
                    return std::nullopt;
                if (has_any(ci.tags, r.exclude_tags))
                    return std::nullopt;
                if (r.changed) {
                    if (auto why = affected(p, ci, *r.changed, coverage))
                        return why;
                    if (sets.empty())
                        return std::nullopt;
                }
                if (sets.empty())
                    return std::string(r.cases.empty() ? "every case" : "its identifier");
                for (const auto& st : sets) {
                    if (has_any(ci.tags, st.exclude_tags))
                        continue;
                    if (st.all)
                        return std::format("set {}", st.name);
                    if (st.regression) {
                        const bool guards = std::ranges::any_of(ci.tags, [](const std::string& t) {
                            return t == "regression" || t.starts_with("regression:");
                        });
                        if (guards)
                            return std::format("set {}: it guards a defect", st.name);
                        if (h.failed_recently.contains(key))
                            return std::format("set {}: it failed within {} days", st.name,
                                               c.regression_days);
                        continue;
                    }
                    if (st.affected) {
                        if (!r.unknown_change.empty())
                            return std::format("set {}: the change is not known", st.name);
                        if (!r.changed)
                            continue;
                        if (auto why = affected(p, ci, *r.changed, coverage))
                            return std::format("set {}: {}", st.name, *why);
                        continue;
                    }
                    const bool by_tag =
                        !st.tags.empty() && (has_any(ci.tags, st.tags) || has_any(p.tags, st.tags));
                    const bool by_kind =
                        !st.kinds.empty() && std::ranges::find(st.kinds, ci.kind) != st.kinds.end();
                    const bool by_case =
                        !st.cases.empty() && (any_glob(st.cases, ci.id) || any_glob(st.cases, key));
                    const bool by_program = !st.programs.empty() && any_glob(st.programs, p.name);
                    if (by_tag || by_kind || by_case || by_program)
                        return std::format("set {}", st.name);
                }
                return std::nullopt;
            }();
            if (!pick)
                continue;
            u.cases.push_back({ci, *pick});
            auto ms = h.ms.find(key);
            u.expected_ms += ms == h.ms.end() ? unknown_ms : ms->second;
        }
        if (u.cases.empty())
            continue;
        u.all = u.cases.size() == listed->size() && r.cases.empty() && !r.failed_only && !r.changed;
        out.selected += u.cases.size();
        out.units.push_back(std::move(u));
    }
    if (r.changed && r.changed->empty())
        out.notes.push_back("the change touches no path: nothing is affected");
    if (!r.unknown_change.empty())
        out.notes.push_back(std::format("the change could not be read ({}): every case is affected",
                                        r.unknown_change));

    // A budget keeps the most useful cases that fit: the ones that failed lately first, then the
    // quickest.
    std::optional<std::chrono::milliseconds> budget = r.budget;
    for (const auto& st : sets)
        if (!budget && st.budget && r.set == st.name)
            budget = st.budget;
    if (budget && r.budget) {
        struct ref {
            std::size_t u, c;
            double ms;
            bool failed;
        };
        std::vector<ref> all;
        for (std::size_t ui = 0; ui < out.units.size(); ++ui)
            for (std::size_t ci = 0; ci < out.units[ui].cases.size(); ++ci) {
                const auto key =
                    out.units[ui].program.name + "::" + out.units[ui].cases[ci].info.id;
                auto ms = h.ms.find(key);
                all.push_back({ui, ci, ms == h.ms.end() ? 1000.0 : ms->second,
                               h.failed_recently.contains(key)});
            }
        std::ranges::sort(all, [](const ref& a, const ref& b) {
            return a.failed != b.failed ? a.failed : a.ms < b.ms;
        });
        double spent = 0;
        std::set<std::pair<std::size_t, std::size_t>> keep;
        for (const auto& x : all) {
            if (spent + x.ms > double(budget->count()) && !keep.empty())
                continue;
            spent += x.ms;
            keep.insert({x.u, x.c});
        }
        std::size_t dropped = 0;
        for (std::size_t ui = 0; ui < out.units.size(); ++ui) {
            std::vector<unit_case> kept;
            for (std::size_t ci = 0; ci < out.units[ui].cases.size(); ++ci)
                if (keep.contains({ui, ci}))
                    kept.push_back(out.units[ui].cases[ci]);
                else
                    ++dropped;
            if (kept.size() != out.units[ui].cases.size())
                out.units[ui].all = false;
            out.units[ui].cases = std::move(kept);
        }
        std::erase_if(out.units, [](const unit& u) { return u.cases.empty(); });
        out.selected -= dropped;
        if (dropped)
            out.notes.push_back(
                std::format("the budget of {} leaves out {} case(s)", *budget, dropped));
    }
    if (r.shard)
        keep_shard(out, r.shard->first, r.shard->second);
    return out;
}

}  // namespace mcpplibs::mtest::engine
