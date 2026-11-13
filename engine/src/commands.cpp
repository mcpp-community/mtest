module mcpplibs.mtest.engine.commands;

import std;
import mcpplibs.os;
import mcpplibs.mtest.engine.config;
import mcpplibs.mtest.engine.store;
import mcpplibs.mtest.engine.select;
import mcpplibs.mtest.engine.schedule;
import mcpplibs.mtest.engine.analysis;
import mcpplibs.mtest.engine.output;
import mcpplibs.mtest.engine.exec;
import mcpplibs.mtest.engine.coverage;
import mcpplibs.mtest.engine.capability;
import mcpplibs.mtest.engine.cicheck;
import mcpplibs.mtest.engine.dag;
import mcpplibs.mtest.engine.report;
import mcpplibs.mtest.manifest;

namespace mcpplibs::mtest::engine {

namespace manifest = mcpplibs::mtest::manifest;

namespace os = mcpplibs::os;

namespace {

answer usage(std::string message) {
    return {2, {{"error", message}, {"conclusion", message}}, "mtest: " + message + "\n"};
}
answer machine(std::string message) {
    return {3, {{"error", message}, {"conclusion", message}}, "mtest: " + message + "\n"};
}

std::vector<std::string> strings(const json& p, const char* key) {
    std::vector<std::string> out;
    if (auto it = p.find(key); it != p.end()) {
        if (it->is_string())
            out.push_back(it->get<std::string>());
        else if (it->is_array())
            for (const auto& v : *it)
                if (v.is_string())
                    out.push_back(v.get<std::string>());
    }
    return out;
}
// A string parameter; a list (the command line repeats `--case`) gives its first value.
std::string text_of(const json& p, const char* key, std::string fallback = {}) {
    auto it = p.find(key);
    if (it == p.end())
        return fallback;
    if (it->is_string())
        return it->get<std::string>();
    if (it->is_array() && !it->empty() && it->front().is_string())
        return it->front().get<std::string>();
    return fallback;
}

// The project: the nearest directory upward with mtest.toml, else `cwd`.
std::filesystem::path project_root(const std::filesystem::path& cwd) {
    for (auto d = cwd; !d.empty(); d = d.parent_path()) {
        if (std::filesystem::exists(d / "mtest.toml"))
            return d;
        if (d == d.parent_path())
            break;
    }
    return cwd;
}

struct context {
    config cfg;
    store st;
};

std::expected<context, answer> open(const std::filesystem::path& cwd) {
    const auto root = project_root(cwd);
    auto c = load_config(root);
    if (!c)
        return std::unexpected(usage(c.error()));
    auto dir = c->store.is_absolute() ? c->store : root / c->store;
    return context{std::move(*c), open_store(dir)};
}

// A command line given as one string, split by spaces outside quotes (a program and its arguments).
std::vector<std::string> split_command(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    char quote = 0;
    bool any = false;
    for (char ch : s) {
        if (quote) {
            if (ch == quote)
                quote = 0;
            else
                cur += ch;
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
            any = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (any || !cur.empty())
                out.push_back(cur);
            cur.clear();
            any = false;
            continue;
        }
        cur += ch;
    }
    if (any || !cur.empty())
        out.push_back(cur);
    return out;
}

std::expected<selection_request, answer> selection_of(const context& ctx, const json& p) {
    selection_request r;
    r.set = text_of(p, "set");
    r.cases = strings(p, "case");
    r.tags = strings(p, "tag");
    r.kinds = strings(p, "kind");
    r.programs = strings(p, "program");
    r.exclude_tags = strings(p, "exclude_tag");
    r.exclude_programs = strings(p, "exclude_program");
    r.failed_only = p.value("failed", false);
    r.run_under = strings(p, "run_under");
    if (auto e = p.find("env"); e != p.end() && e->is_object())
        for (auto& item_ : e->items())
            if (item_.value().is_string())
                r.env.emplace_back(item_.key(), item_.value().get<std::string>());
    if (auto since = text_of(p, "since"); !since.empty()) {
        auto paths = changed_since(ctx.cfg.root, since);
        if (!paths)
            return std::unexpected(machine(paths.error()));
        r.changed = *paths;
    } else if (p.contains("changed")) {
        r.changed = strings(p, "changed");
    }
    if (auto b = text_of(p, "budget"); !b.empty()) {
        auto d = parse_duration(b);
        if (!d)
            return std::unexpected(usage(std::format("budget {} is not a duration (60s, 5m)", b)));
        r.budget = d;
    }
    if (auto sh = text_of(p, "shard"); !sh.empty()) {
        auto parts = parse_shard(sh);
        if (!parts)
            return std::unexpected(usage(parts.error()));
        r.shard = *parts;
    }
    if (r.set == "pr" && !r.changed) {
        // The `pr` set is the change's: against the branch a pull request merges into, else main.
        const auto base = os::env::get("GITHUB_BASE_REF").value_or("");
        auto paths = changed_since(ctx.cfg.root, base.empty() ? "origin/main" : "origin/" + base);
        if (paths)
            r.changed = *paths;
        else
            r.unknown_change = paths.error();
    }
    return r;
}

std::expected<std::vector<program_spec>, answer> programs_of(const context& ctx, const json& p) {
    program_source src;
    for (const auto& cmd : strings(p, "programs"))
        src.commands.push_back(split_command(cmd));
    if (auto it = p.find("argv"); it != p.end() && it->is_array()) {
        std::vector<std::string> argv;
        for (const auto& v : *it)
            if (v.is_string())
                argv.push_back(v.get<std::string>());
        if (!argv.empty())
            src.commands.push_back(argv);
    }
    auto programs = discover(ctx.cfg, src);
    if (!programs)
        return std::unexpected(machine(programs.error()));
    if (programs->empty())
        return std::unexpected(usage("no program to run: name one (`mtest run ./test`), declare "
                                     "[programs.*] or [discover] in mtest.toml, "
                                     "or set [host] kind = \"mcpp\""));
    for (auto& s : *programs)
        if (auto t = text_of(p, "timeout"); !t.empty())
            if (auto d = parse_duration(t))
                s.timeout = std::chrono::duration_cast<std::chrono::seconds>(*d);
    if (p.value("isolate", false))
        for (auto& s : *programs)
            s.isolation = "case";
    return *programs;
}

std::string describe_selection(const json& p) {
    json q = p;
    q.erase("programs");
    q.erase("argv");
    q.erase("report");
    q.erase("monitor");
    q.erase("group");
    q.erase("sections");
    return dumped(q);
}

report_options report_options_of(const context& ctx, const json& p);

answer finish_run(const context& ctx, const run_record& r, const json& p, const plan& pl) {
    if (auto rep = p.find("report"); rep != p.end() && rep->is_object()) {
        if (auto f = text_of(*rep, "junit"); !f.empty())
            (void)os::fs::write(os::fs::from_utf8(f), junit_xml(r));
        if (auto f = text_of(*rep, "html"); !f.empty())
            (void)os::fs::write(os::fs::from_utf8(f), html_page(r));
        if (auto f = text_of(*rep, "md"); !f.empty())
            (void)os::fs::write(os::fs::from_utf8(f),
                                markdown_report(ctx.st, {r}, report_options_of(ctx, p)));
        if (auto f = text_of(*rep, "json"); !f.empty()) {
            json all = to_json(r);
            all["cases"] = json::array();
            for (const auto& c : r.cases)
                all["cases"].push_back(to_json(c));
            // The run's timeline: each program's start, duration, status and what it held.
            all["timeline"] = json::array();
            for (const auto& pr : r.programs)
                all["timeline"].push_back(to_json(pr));
            (void)os::fs::write(os::fs::from_utf8(f), dumped(all, 2) + "\n");
        }
    }
    json failing_cases = json::array();
    for (const auto& c : r.cases)
        if (failing(c.status) && !c.quarantined)
            failing_cases.push_back(
                {{"program", c.program},
                 {"case", c.id},
                 {"kind", c.kind},
                 {"status", c.status},
                 {"where", c.failures.empty()
                               ? std::string{}
                               : std::format("{}:{}", c.failures[0].file, c.failures[0].line)},
                 {"why", c.failures.empty() ? c.reason : c.failures[0].message},
                 {"again",
                  [&] {
                      for (const auto& pr : r.programs)
                          if (pr.name == c.program)
                              return reproduce_command(pr, c.id);
                      return reproduce_command(c.program, c.id);
                  }()},
                 {"diagnosis", c.diagnosis}});
    json data = to_json(r);
    data["failing"] = failing_cases;
    data["notes"] = pl.notes;
    data["conclusion"] =
        r.status == "pass"
            ? std::format("{} case(s) passed in {:.1f} s", r.cases.size(), r.ms / 1000)
        : r.status == "infra" ? std::string("no test failed, but a program could not be started: "
                                            "the machine's failure (mtest explain)")
        : r.status == "cancelled"
            ? std::string("the run was cancelled")
            : std::format("{} case(s) failed; the first: {}::{} -- mtest explain --case '{}'",
                          failing_cases.size(),
                          failing_cases.empty() ? ""
                                                : failing_cases[0]["program"].get<std::string>(),
                          failing_cases.empty() ? "" : failing_cases[0]["case"].get<std::string>(),
                          failing_cases.empty() ? "" : failing_cases[0]["case"].get<std::string>());
    std::string text;
    for (const auto& n : pl.notes)
        text += "note: " + n + "\n";
    text += summary_text(r, p.value("verbose", false));
    const int status = r.status == "pass" ? 0 : r.status == "infra" ? 3 : 1;
    return {status, data, text};
}

answer cmd_run(const std::filesystem::path& cwd, const json& p,
               const std::function<void(const json&)>& events) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    auto programs = programs_of(*ctx, p);
    if (!programs)
        return programs.error();
    auto sel = selection_of(*ctx, p);
    if (!sel)
        return sel.error();
    auto pl = make_plan(ctx->cfg, ctx->st, *programs, *sel);
    if (!pl)
        return machine(pl.error());
    // `--resume RUN` (0.7.0, M5): the cases RUN judged are not run again, and their results are
    // carried into this run, so that a long run (a bench over a matrix) that was interrupted ends
    // as one whole run. A case RUN could not start (infra) or never reached runs now.
    std::vector<case_result> carried;
    std::string resumed_id;
    if (auto ref = text_of(p, "resume"); !ref.empty()) {
        auto prev = load_run(ctx->st, ref);
        if (!prev)
            return usage(std::format("no stored run {} to resume", ref));
        resumed_id = prev->id;
        std::map<std::string, case_result> judged;
        for (auto& c : prev->cases)
            if (c.status != "infra")
                judged[c.program + "::" + c.id] = c;
        std::size_t kept = 0;
        for (auto& u : pl->units) {
            const auto before = u.cases.size();
            std::erase_if(u.cases, [&](const unit_case& uc) {
                auto it = judged.find(u.program.name + "::" + uc.info.id);
                if (it == judged.end())
                    return false;
                carried.push_back(it->second);
                return true;
            });
            if (u.cases.size() != before)
                u.all = false;
            kept += u.cases.size();
        }
        std::erase_if(pl->units, [](const unit& u) { return u.cases.empty(); });
        pl->selected = kept;
        pl->notes.push_back(std::format("resuming run {}: {} case(s) carried, {} to run",
                                        resumed_id, carried.size(), kept));
        if (pl->units.empty()) {
            json data = {{"status", "pass"},
                         {"resumed", resumed_id},
                         {"carried", carried.size()},
                         {"conclusion",
                          std::format("run {} has judged every selected case: nothing left to run",
                                      resumed_id)}};
            return {0, data, std::format("run {} has judged every selected case\n", resumed_id)};
        }
    }
    if (pl->units.empty()) {
        json data = {{"status", "pass"},
                     {"cases", 0},
                     {"notes", pl->notes},
                     {"conclusion", "the selection is empty: nothing ran"}};
        return {0, data,
                "nothing selected" +
                    std::string(pl->notes.empty() ? "" : ": " + pl->notes.front()) + "\n"};
    }
    execute_options o;
    o.jobs = p.value("jobs", 0);
    if (p.contains("seed") && p["seed"].is_number_unsigned())
        o.seed = p["seed"].get<std::uint64_t>();
    o.run_under = strings(p, "run_under");
    if (auto e = p.find("env"); e != p.end() && e->is_object())
        for (auto& item_ : e->items())
            if (auto& k = item_.key(); true)
                if (auto& v = item_.value(); true)
                    if (v.is_string())
                        o.env.emplace_back(k, v.get<std::string>());
    o.fail_fast = p.value("fail_fast", false);
    o.retries = p.value("retries", -1);
    o.set = sel->set;
    o.selection = describe_selection(p);
    o.monitor = text_of(p, "monitor");
    o.group = text_of(p, "group");
    o.on_event = events;
    // `--coverage`: the run measures the coverage map (docs/coverage.md).
    std::map<std::filesystem::path, std::vector<std::string>> coverage_cases;
    if (p.value("coverage", false)) {
        if (ctx->cfg.coverage.objects.empty())
            return usage("--coverage needs [coverage] objects and database in mtest.toml");
        o.coverage_dir = ctx->st.dir / "work" / "coverage";
        std::error_code ec;
        std::filesystem::remove_all(*o.coverage_dir, ec);
        o.coverage_strip = gcov_strip(ctx->cfg.root / os::fs::from_utf8(ctx->cfg.coverage.objects));
        o.coverage_cases = &coverage_cases;
    }
    auto r = execute(ctx->cfg, ctx->st, *pl, o);
    if (!resumed_id.empty()) {
        r.resumed = resumed_id;
        for (auto& c : carried)
            r.cases.push_back(std::move(c));
        r.counts = count(r.cases);
        if (r.status == "pass" && std::ranges::any_of(r.cases, [](const case_result& c) {
                return failing(c.status) && !c.quarantined;
            }))
            r.status = "fail";
        write_run(ctx->st, r);
    }
    if (o.coverage_dir) {
        auto m = collect_coverage(ctx->cfg, coverage_cases, r.commit);
        if (!m)
            pl->notes.push_back("coverage: " + m.error());
        else {
            write_coverage(coverage_file(ctx->st), *m);
            std::size_t files = 0;
            for (const auto& [_, f] : m->cases)
                files = std::max(files, f.size());
            pl->notes.push_back(std::format("coverage: {} case(s) mapped, at most {} file(s) each",
                                            m->cases.size(), files));
        }
        std::error_code ec;
        std::filesystem::remove_all(*o.coverage_dir, ec);
    }
    return finish_run(*ctx, r, p, *pl);
}

// `mtest coverage show|import`: the store's coverage map, or another taken in (a CI artifact).
answer cmd_coverage(const std::filesystem::path& cwd, const json& p) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    const auto action = text_of(p, "action");
    const auto file = coverage_file(ctx->st);
    if (action == "import") {
        const auto from = text_of(p, "from");
        if (from.empty())
            return usage("coverage import needs --from FILE");
        auto m = read_coverage(cwd / os::fs::from_utf8(from));
        if (!m)
            return usage(m.error());
        write_coverage(file, *m);
        return {0,
                {{"cases", m->cases.size()},
                 {"commit", m->commit},
                 {"conclusion", std::format("the coverage map of {} ({} case(s)) is the store's",
                                            m->commit.substr(0, 12), m->cases.size())}},
                std::format("coverage map of {} taken in: {} case(s)\n", m->commit.substr(0, 12),
                            m->cases.size())};
    }
    if (!action.empty() && action != "show")
        return usage(std::format("coverage {}: the actions are show and import", action));
    auto m = read_coverage(file);
    if (!m)
        return {0,
                {{"conclusion", "the store has no coverage map: affected selection reads "
                                "programs' sources and cases' artifacts only"}},
                "no coverage map\n"};
    const bool fresh = fresh_coverage(ctx->st, ctx->cfg.coverage.max_age).has_value();
    json cases = json::object();
    std::string text = std::format("coverage map of {} at {}{}\n", m->commit.substr(0, 12), m->time,
                                   fresh ? "" : " (older than [coverage] max_age: not read)");
    const auto only = strings(p, "case");
    for (const auto& [k, files] : m->cases) {
        if (!only.empty() &&
            !std::ranges::any_of(only, [&](const std::string& g) { return glob_match(g, k); }))
            continue;
        cases[k] = files;
        text += std::format("{}: {} file(s)\n", k, files.size());
    }
    return {0,
            {{"commit", m->commit},
             {"time", m->time},
             {"fresh", fresh},
             {"cases", cases},
             {"conclusion",
              std::format("{} case(s) mapped at {}{}", m->cases.size(), m->commit.substr(0, 12),
                          fresh ? "" : "; older than max_age")}},
            text};
}

answer cmd_plan(const std::filesystem::path& cwd, const json& p) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    auto programs = programs_of(*ctx, p);
    if (!programs)
        return programs.error();
    auto sel = selection_of(*ctx, p);
    if (!sel)
        return sel.error();
    auto pl = make_plan(ctx->cfg, ctx->st, *programs, *sel);
    if (!pl)
        return machine(pl.error());
    json units = json::array();
    std::string text;
    double expected = 0;
    for (const auto& u : pl->units) {
        json cases = json::array();
        for (const auto& uc : u.cases)
            cases.push_back({{"case", uc.info.id}, {"why", uc.why}, {"kind", uc.info.kind}});
        units.push_back({{"program", u.program.name},
                         {"level", u.program.level},
                         {"all", u.all},
                         {"expected_ms", u.expected_ms},
                         {"cases", cases}});
        expected += u.expected_ms;
        text += std::format("{} ({} case(s), about {:.1f} s)\n", u.program.name, u.cases.size(),
                            u.expected_ms / 1000);
        for (const auto& uc : u.cases)
            text += std::format("  {}  -- {}\n", uc.info.id, uc.why);
    }
    for (const auto& n : pl->notes)
        text += "note: " + n + "\n";
    text += std::format("{} of {} case(s) selected\n", pl->selected, pl->considered);
    return {0,
            {{"units", units},
             {"selected", pl->selected},
             {"considered", pl->considered},
             {"notes", pl->notes},
             {"expected_ms", expected},
             {"conclusion",
              std::format("{} of {} case(s) in {} program(s), about {:.1f} s of work", pl->selected,
                          pl->considered, pl->units.size(), expected / 1000)}},
            text};
}

// `mtest list --static`: the cases the test files declare, read from their text (the static case
// manifest, lib/src/manifest.cppm), with the attributes mtest.toml and the host state. Nothing is
// built or run; a built program's own listing is the authority.
answer cmd_list_static(const std::filesystem::path& cwd) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    const auto& globs = ctx->cfg.static_globs;
    if (globs.empty())
        return usage(
            "--static reads [static] globs in mtest.toml, or a host's tests; none is stated");
    auto literal_prefix = [](std::string_view g) {
        const auto wild = g.find_first_of("*?[");
        const auto cut = g.substr(0, wild).rfind('/');
        return cut == std::string_view::npos ? std::string{} : std::string(g.substr(0, cut + 1));
    };
    auto first_match = [&](const std::string& rel) -> std::optional<std::string> {
        for (const auto& g : globs)
            if (g.starts_with('!') && path_glob_match(std::string_view(g).substr(1), rel))
                return std::nullopt;
        for (const auto& g : globs)
            if (!g.starts_with('!') && path_glob_match(g, rel))
                return literal_prefix(g);
        return std::nullopt;
    };
    json programs = json::array();
    std::size_t total = 0;
    std::string text;
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator it(ctx->cfg.root, ec), end; it != end;
         it.increment(ec)) {
        const auto rel =
            os::fs::generic_utf8(std::filesystem::relative(it->path(), ctx->cfg.root, ec));
        const auto top = rel.substr(0, rel.find('/'));
        if (top == ".git" || top == ".mtest" || top == "target") {
            if (it->is_directory())
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file(ec))
            files.push_back(it->path());
    }
    std::ranges::sort(files);
    auto read_file = [](const std::filesystem::path& file) {
        std::ifstream in(file, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), {});
    };
    // The suite types the project's C++ sources declare: a type its tests share is usually
    // written in a module or a header beside them, not in a file the globs name.
    std::vector<std::string> sources;
    for (const auto& file : files) {
        static const std::set<std::string> cxx{".cpp", ".cc", ".cxx", ".cppm", ".ixx",
                                               ".mpp", ".h",  ".hh",  ".hpp",  ".hxx"};
        if (cxx.contains(file.extension().string()))
            sources.push_back(read_file(file));
    }
    const auto types = manifest::types_of(sources);
    for (const auto& file : files) {
        const auto rel = os::fs::generic_utf8(std::filesystem::relative(file, ctx->cfg.root, ec));
        const auto base = first_match(rel);
        if (!base)
            continue;
        const auto source = read_file(file);
        auto name = rel.substr(base->size());
        if (const auto dot = name.rfind('.');
            dot != std::string::npos && name.find('/', dot) == std::string::npos)
            name.erase(dot);
        json cases = json::array();
        program_spec program;
        program.name = name;
        program.kind = layout_kind(rel);
        for (const auto& sc : manifest::scan(source, types)) {
            case_info ci;
            ci.id = sc.id;
            ci.name = sc.name;
            ci.file = rel;
            ci.line = sc.line;
            ci.kind = sc.kind;
            ci.tags = sc.tags;
            ci.resources = sc.resources;
            ci.clauses = sc.clauses;
            ci.artifacts = sc.artifacts;
            apply_attributes(ctx->cfg, program, ci);
            cases.push_back({{"case", ci.id},
                             {"name", ci.name},
                             {"file", ci.file},
                             {"line", ci.line},
                             {"kind", ci.kind},
                             {"tags", ci.tags},
                             {"resources", ci.resources},
                             {"clauses", ci.clauses},
                             {"artifacts", ci.artifacts}});
            text += std::format("{}::{}  {}:{}\n", name, ci.id, rel, ci.line);
        }
        total += cases.size();
        programs.push_back({{"program", name}, {"file", rel}, {"cases", cases}});
    }
    return {0,
            {{"programs", programs},
             {"cases", total},
             {"static", true},
             {"conclusion",
              std::format("{} case(s) declared in {} test file(s), read without building", total,
                          programs.size())}},
            text +
                std::format("{} case(s) in {} file(s), read statically\n", total, programs.size())};
}

answer cmd_list(const std::filesystem::path& cwd, const json& p) {
    if (p.value("static", false))
        return cmd_list_static(cwd);
    json q = p;
    if (!q.contains("set"))
        q.erase("set");
    return cmd_plan(cwd, q);
}

std::expected<run_record, answer> run_named(const context& ctx, const json& p,
                                            const char* key = "run") {
    const auto id = text_of(p, key, "last");
    auto r = load_run(ctx.st, id);
    if (!r)
        return std::unexpected(usage(
            std::format("no stored run {}{}", id, id == "last" ? " (run `mtest run` first)" : "")));
    return *r;
}

answer cmd_status(const std::filesystem::path& cwd, const json&) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    json active = json::array();
    std::string text;
    for (auto& [id, prog] : active_runs(ctx->st)) {
        active.push_back(prog);
        text += std::format("running {}: {} of {} program(s) done, {} failed\n", id,
                            prog.value("done", 0), prog.value("total", 0), prog.value("failed", 0));
    }
    json last = nullptr;
    if (auto r = load_run(ctx->st, "last")) {
        last = to_json(*r);
        text += std::format("last run {}: {} ({} case(s), {:.1f} s)\n", r->id, r->status,
                            r->cases.size(), r->ms / 1000);
    }
    return {0,
            {{"active", active},
             {"last", last},
             {"conclusion", active.empty()
                                ? (last.is_null() ? std::string("no run is stored")
                                                  : std::format("nothing runs; the last run {}",
                                                                last["status"].get<std::string>()))
                                : std::format("{} run(s) in progress", active.size())}},
            text};
}

answer cmd_cancel(const std::filesystem::path& cwd, const json& p) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    auto active = active_runs(ctx->st);
    auto id = text_of(p, "run");
    if (id.empty()) {
        if (active.empty())
            return usage("no run is in progress");
        id = active.back().first;
    }
    request_cancel(ctx->st, id);
    return {0,
            {{"run", id},
             {"conclusion", std::format("run {} stops after the programs now running", id)}},
            std::format("cancelling {}: it stops after the programs now running\n", id)};
}

// The words of a list given as an array or as one comma-separated string.
std::vector<std::string> words_of(const json& p, const char* key) {
    std::vector<std::string> out;
    for (const auto& w : strings(p, key))
        for (auto part : std::views::split(std::string_view(w), ','))
            if (auto s = std::string(part.begin(), part.end()); !s.empty())
                out.push_back(s);
    if (out.empty())
        if (auto t = text_of(p, key); !t.empty())
            for (auto part : std::views::split(std::string_view(t), ','))
                out.emplace_back(part.begin(), part.end());
    return out;
}

report_options report_options_of(const context& ctx, const json& p) {
    report_options o;
    o.sections = words_of(p, "sections");
    if (o.sections.empty())
        o.sections = ctx.cfg.report.sections;
    o.scope = text_of(p, "scope", ctx.cfg.report.scope);
    o.top = p.value("top", ctx.cfg.report.top);
    o.max_nodes = p.value("max_nodes", ctx.cfg.report.max_nodes);
    o.title = text_of(p, "title");
    return o;
}

// The runs a report is of: a group's (every stored run of it), a list, or one.
std::expected<std::vector<run_record>, answer> runs_of(const context& ctx, const json& p) {
    std::vector<run_record> out;
    if (auto g = text_of(p, "group"); !g.empty()) {
        for (const auto& r : runs(ctx.st))
            if (r.group == g)
                if (auto full = load_run(ctx.st, r.id))
                    out.push_back(std::move(*full));
        if (out.empty())
            return std::unexpected(usage(std::format("no stored run of group {}", g)));
        return out;
    }
    if (auto list = words_of(p, "runs"); !list.empty()) {
        for (const auto& id : list) {
            auto r = load_run(ctx.st, id);
            if (!r)
                return std::unexpected(usage(std::format("no stored run {}", id)));
            out.push_back(std::move(*r));
        }
        return out;
    }
    auto r = run_named(ctx, p);
    if (!r)
        return std::unexpected(r.error());
    out.push_back(std::move(*r));
    return out;
}

answer cmd_report(const std::filesystem::path& cwd, const json& p) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    const auto format = text_of(p, "format", "text");
    if (format == "md") {
        auto all = runs_of(*ctx, p);
        if (!all)
            return all.error();
        auto body = markdown_report(ctx->st, *all, report_options_of(*ctx, p));
        json data = {{"runs", all->size()},
                     {"conclusion", std::format("a report of {} run(s)", all->size())}};
        if (auto out = text_of(p, "out"); !out.empty()) {
            (void)os::fs::write(os::fs::from_utf8(out), body);
            body = std::format("wrote {}\n", out);
        }
        return {0, data, body};
    }
    auto r = run_named(*ctx, p);
    if (!r)
        return r.error();
    std::string body = format == "junit"  ? junit_xml(*r)
                       : format == "html" ? html_page(*r)
                       : format == "json" ? std::string{}
                                          : summary_text(*r, true);
    json data = to_json(*r);
    data["cases"] = json::array();
    for (const auto& c : r->cases)
        data["cases"].push_back(to_json(c));
    data["conclusion"] = std::format("run {} {}", r->id, r->status);
    if (format == "json")
        body = dumped(data, 2) + "\n";
    if (auto out = text_of(p, "out"); !out.empty()) {
        (void)os::fs::write(os::fs::from_utf8(out), body);
        body = std::format("wrote {}\n", out);
    }
    return {0, data, body};
}

answer cmd_diff(const std::filesystem::path& cwd, const json& p) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    json q = p;
    if (!q.contains("before"))
        q["before"] = "last-1";
    if (!q.contains("after"))
        q["after"] = "last";
    auto a = run_named(*ctx, q, "before");
    if (!a)
        return a.error();
    auto b = run_named(*ctx, q, "after");
    if (!b)
        return b.error();
    auto d = diff(*a, *b, p.value("ratio", 1.25));
    std::string text = d["conclusion"].get<std::string>() + "\n";
    for (const auto& x : d["newly_failing"])
        text += std::format("  newly failing {}::{}\n", x["program"].get<std::string>(),
                            x["case"].get<std::string>());
    for (const auto& x : d["fixed"])
        text += std::format("  fixed         {}::{}\n", x["program"].get<std::string>(),
                            x["case"].get<std::string>());
    return {0, d, text};
}

answer cmd_explain(const std::filesystem::path& cwd, const json& p) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    auto r = run_named(*ctx, p);
    if (!r)
        return r.error();
    auto ref = text_of(p, "case");
    if (ref.empty()) {
        for (const auto& c : r->cases)
            if (failing(c.status)) {
                ref = c.program + "::" + c.id;
                break;
            }
        if (ref.empty())
            return {0,
                    {{"run", r->id},
                     {"cases", json::array()},
                     {"conclusion", std::format("nothing failed in run {}", r->id)}},
                    std::format("nothing failed in run {}\n", r->id)};
    }
    auto e = explain(ctx->st, *r, ref);
    if (!e)
        return usage(e.error());
    std::string text;
    for (const auto& c : (*e)["cases"]) {
        text += std::format("{}::{} {} ({})\n", c["program"].get<std::string>(),
                            c["case"].get<std::string>(), c["status"].get<std::string>(),
                            c["kind_of_failure"].get<std::string>());
        for (std::size_t i = 0; i < c["why"].size(); ++i) {
            text +=
                std::format("  {}{}\n",
                            i < c["where"].size() ? c["where"][i].get<std::string>() + ": " : "",
                            c["why"][i]["message"].get<std::string>());
            if (c["why"][i].contains("expected"))
                text += std::format("    expected {}\n    actual   {}\n",
                                    c["why"][i]["expected"].get<std::string>(),
                                    c["why"][i]["actual"].get<std::string>());
        }
        if (c.contains("output") && !c["output"]["err_tail"].get<std::string>().empty())
            text += "  error output (end):\n" + c["output"]["err_tail"].get<std::string>() + "\n";
        if (c.contains("diagnosis"))
            text += "  diagnosis: " + c["diagnosis"].get<std::string>() + "\n";
        text += "  again: " + c["again"].get<std::string>() + "\n";
    }
    return {0, *e, text};
}

answer cmd_history(const std::filesystem::path& cwd, const json& p) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    auto id = text_of(p, "case");
    if (id.empty())
        return usage("history needs --case <id>");
    std::string program;
    if (auto at = id.find("::"); at != std::string::npos) {
        program = id.substr(0, at);
        id = id.substr(at + 2);
    }
    if (auto pr = text_of(p, "program"); !pr.empty())
        program = pr;
    auto h = history(ctx->st, program, id, std::size_t(p.value("limit", 20)));
    std::string text = h["conclusion"].get<std::string>() + "\n";
    for (const auto& x : h["items"])
        text += std::format("  {} {} {:.0f} ms\n", x["run"].get<std::string>(),
                            x["status"].get<std::string>(), x["ms"].get<double>());
    return {0, h, text};
}

// A declarative query of the stored cases: each parameter narrows the rows.
answer cmd_query(const std::filesystem::path& cwd, const json& p) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    const auto statuses = strings(p, "status");
    const auto kinds = strings(p, "kind");
    const auto programs = strings(p, "program");
    const auto cases = strings(p, "case");
    const auto tags = strings(p, "tag");
    const int days = p.value("days", 0);
    const std::size_t limit = std::size_t(p.value("limit", 200));
    const std::size_t last_runs = std::size_t(p.value("runs", 0));
    const auto cutoff = days > 0 ? std::format("{:%Y%m%d}", std::chrono::floor<std::chrono::days>(
                                                                std::chrono::system_clock::now() -
                                                                std::chrono::days{days}))
                                 : std::string{};
    auto all = runs(ctx->st);
    json rows = json::array();
    std::size_t scanned = 0;
    for (std::size_t i = all.size(); i-- > 0 && rows.size() < limit;) {
        if (!cutoff.empty() && all[i].id.substr(0, 8) < cutoff)
            break;
        if (last_runs && scanned++ >= last_runs)
            break;
        auto r = load_run(ctx->st, all[i].id);
        if (!r)
            continue;
        for (const auto& c : r->cases) {
            if (!statuses.empty() && std::ranges::find(statuses, c.status) == statuses.end())
                continue;
            if (!kinds.empty() && std::ranges::find(kinds, c.kind) == kinds.end())
                continue;
            if (!programs.empty() && !std::ranges::any_of(programs, [&](const std::string& g) {
                    return glob_match(g, c.program);
                }))
                continue;
            if (!cases.empty() && !std::ranges::any_of(cases, [&](const std::string& g) {
                    return glob_match(g, c.id);
                }))
                continue;
            if (!tags.empty() && !std::ranges::any_of(tags, [&](const std::string& t) {
                    return std::ranges::find(c.tags, t) != c.tags.end();
                }))
                continue;
            json row = to_json(c);
            row["run"] = r->id;
            row["commit"] = r->commit;
            rows.push_back(std::move(row));
            if (rows.size() >= limit)
                break;
        }
    }
    std::string text;
    for (const auto& row : rows)
        text += std::format("{} {}::{} {} {:.0f} ms\n", row["run"].get<std::string>(),
                            row["program"].get<std::string>(), row["id"].get<std::string>(),
                            row["status"].get<std::string>(), row["ms"].get<double>());
    return {0, {{"rows", rows}, {"conclusion", std::format("{} row(s)", rows.size())}}, text};
}

answer cmd_analyze(const std::filesystem::path& cwd, const json& p) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    const auto metric = text_of(p, "metric", "durations");
    json data;
    if (metric == "flaky") {
        data = flaky(ctx->st, std::size_t(p.value("runs", 30)));
    } else if (metric == "trend") {
        data = trend(ctx->st, std::size_t(p.value("runs", 20)), strings(p, "case"));
    } else {
        auto r = run_named(*ctx, p);
        if (!r)
            return r.error();
        if (metric == "hot")
            data = hot(*r, std::size_t(p.value("top", 10)));
        else if (metric == "durations")
            data = durations(*r);
        else if (metric == "path")
            data = critical_path(*r);
        else if (metric == "failures")
            data = failures(*r);
        else if (metric == "bench") {
            json items = json::array();
            for (const auto& v :
                 judge_bench(*r, bench_baseline(ctx->st, *r, ctx->cfg.bench), ctx->cfg.bench))
                items.push_back({{"program", v.program},
                                 {"case", v.id},
                                 {"metric", v.metric},
                                 {"verdict", v.verdict},
                                 {"ratio", v.ratio},
                                 {"spread", v.spread},
                                 {"reason", v.reason},
                                 {"repeatable", v.repeatable}});
            const auto bad =
                std::ranges::count_if(items, [](const json& x) { return x["verdict"] != "pass"; });
            data = {{"run", r->id},
                    {"items", items},
                    {"conclusion",
                     std::format("{} of {} measurement(s) do not pass", bad, items.size())}};
        } else if (metric == "cost") {
            double cpu = 0;
            for (const auto& pr : r->programs)
                cpu += pr.ms;
            data = {{"run", r->id},
                    {"program_ms", cpu},
                    {"wall_ms", r->ms},
                    {"jobs", r->jobs},
                    {"conclusion", std::format("{:.1f} program-minutes over {:.1f} wall-minutes",
                                               cpu / 60000, r->ms / 60000)}};
        } else {
            return usage(std::format("no metric {}: hot, durations, path, failures, flaky, bench, "
                                     "trend, cost",
                                     metric));
        }
    }
    std::string text = data["conclusion"].get<std::string>() + "\n";
    if (data.contains("items"))
        for (const auto& x : data["items"])
            text += "  " + dumped(x) + "\n";
    if (data.contains("groups"))
        for (const auto& g : data["groups"])
            text += std::format("  {} x {}\n", g["count"].get<std::size_t>(),
                                g["message"].get<std::string>());
    return {0, data, text};
}

// The graph of a stored run (`mtest graph --run <id>`): its work and what each waited for, the
// critical path marked.
answer run_graph(const context& ctx, const json& p) {
    auto r = run_named(ctx, p);
    if (!r)
        return r.error();
    const auto g = dag_of(*r);
    const auto format = text_of(p, "format", "json");
    const int max_nodes = p.value("max_nodes", ctx.cfg.report.max_nodes);
    json nodes = json::array(), edges = json::array();
    for (const auto& n : g.nodes)
        nodes.push_back({{"id", n.id},
                         {"kind", n.kind},
                         {"label", n.label},
                         {"start_ms", n.start_ms},
                         {"end_ms", n.end_ms},
                         {"worker", n.worker},
                         {"critical", n.critical}});
    for (const auto& e : g.edges)
        edges.push_back({{"from", g.nodes[e.from].id},
                         {"to", g.nodes[e.to].id},
                         {"why", e.why},
                         {"critical", e.critical}});
    json data = {{"run", r->id},
                 {"nodes", nodes},
                 {"edges", edges},
                 {"path_ms", g.path_ms},
                 {"conclusion", critical_path(*r)["conclusion"]}};
    std::string text;
    if (format == "mermaid")
        text = mermaid_graph(g, max_nodes);
    else if (format == "gantt")
        text = mermaid_gantt(g, max_nodes);
    else if (format == "dot") {
        text = "digraph run {\n  rankdir=LR;\n";
        for (std::size_t i = 0; i < g.nodes.size(); ++i)
            text += std::format("  n{} [label=\"{}\"{}];\n", i, g.nodes[i].label,
                                g.nodes[i].critical ? ", color=red" : "");
        for (const auto& e : g.edges)
            text += std::format("  n{} -> n{} [label=\"{}\"{}];\n", e.from, e.to, e.why,
                                e.critical ? ", color=red" : "");
        text += "}\n";
    } else
        text = dumped(data, 2) + "\n";
    return {0, data, text};
}

answer cmd_graph(const std::filesystem::path& cwd, const json& p) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    if (p.contains("run"))
        return run_graph(*ctx, p);
    auto programs = programs_of(*ctx, p);
    if (!programs)
        return programs.error();
    selection_request all;
    auto pl = make_plan(ctx->cfg, ctx->st, *programs, all);
    if (!pl)
        return machine(pl.error());
    json nodes = json::array(), edges = json::array();
    std::set<std::string> seen;
    auto node = [&](const std::string& id, const std::string& kind) {
        if (seen.insert(id).second)
            nodes.push_back({{"id", id}, {"kind", kind}});
    };
    for (const auto& u : pl->units) {
        const auto pid = "program:" + u.program.name;
        node(pid, "program");
        for (const auto& src : u.program.sources) {
            node("file:" + src, "source");
            edges.push_back({{"from", "file:" + src}, {"to", pid}, {"kind", "builds"}});
        }
        for (const auto& uc : u.cases) {
            const auto cid = "case:" + u.program.name + "::" + uc.info.id;
            node(cid, "case");
            edges.push_back({{"from", pid}, {"to", cid}, {"kind", "contains"}});
            for (const auto& r : uc.info.resources) {
                node("resource:" + r, "resource");
                edges.push_back({{"from", cid}, {"to", "resource:" + r}, {"kind", "holds"}});
            }
            for (const auto& a : uc.info.artifacts) {
                node(a, "artifact");
                edges.push_back({{"from", a}, {"to", cid}, {"kind", "reads"}});
            }
        }
    }
    json data = {{"schema", "mtest.graph/1"},
                 {"nodes", nodes},
                 {"edges", edges},
                 {"conclusion", std::format("{} node(s), {} edge(s)", nodes.size(), edges.size())}};
    const auto format = text_of(p, "format", "json");
    std::string text;
    auto label = [](const std::string& s) {
        std::string o;
        for (char ch : s)
            o += ch == '"' ? '\'' : ch;
        return o;
    };
    if (format == "dot") {
        text = "digraph mtest {\n  rankdir=LR;\n";
        for (const auto& n : nodes)
            text += std::format("  \"{}\" [shape={}];\n", label(n["id"]),
                                n["kind"] == "case"      ? "box"
                                : n["kind"] == "program" ? "folder"
                                                         : "ellipse");
        for (const auto& e : edges)
            text += std::format("  \"{}\" -> \"{}\" [label=\"{}\"];\n", label(e["from"]),
                                label(e["to"]), e["kind"].get<std::string>());
        text += "}\n";
    } else if (format == "mermaid") {
        text = "graph LR\n";
        std::map<std::string, std::string> ids;
        for (const auto& n : nodes) {
            const auto k = std::format("n{}", ids.size());
            ids[n["id"]] = k;
            text += std::format("  {}[\"{}\"]\n", k, label(n["id"]));
        }
        for (const auto& e : edges)
            text += std::format("  {} -->|{}| {}\n", ids[e["from"]], e["kind"].get<std::string>(),
                                ids[e["to"]]);
    } else {
        text = dumped(data, 2) + "\n";
    }
    return {0, data, text};
}

answer cmd_gate(const std::filesystem::path& cwd, const json& p) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    auto r = run_named(*ctx, p);
    if (!r)
        return r.error();
    const auto set = text_of(p, "set", r->set.empty() ? "full" : r->set);
    auto g = gate(ctx->cfg, ctx->st, *r, set);
    std::string text = g["conclusion"].get<std::string>() + "\n";
    for (const auto& x : g["reasons"])
        text += "  " + x.get<std::string>() + "\n";
    return {g["pass"].get<bool>() ? 0 : 1, g, text};
}

answer cmd_quarantine(const std::filesystem::path& cwd, const json& p) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    auto list = quarantine(ctx->st);
    const auto action = text_of(p, "action", "list");
    auto id = text_of(p, "case");
    std::string program = text_of(p, "program");
    if (auto at = id.find("::"); at != std::string::npos) {
        program = id.substr(0, at);
        id = id.substr(at + 2);
    }
    if (action == "add") {
        if (id.empty())
            return usage("quarantine add needs --case");
        auto until = text_of(p, "until");
        if (until.empty()) {
            const auto d = std::chrono::year_month_day(std::chrono::floor<std::chrono::days>(
                std::chrono::system_clock::now() + std::chrono::days{14}));
            until = std::format("{:%Y-%m-%d}", std::chrono::sys_days(d));
        }
        std::erase_if(list,
                      [&](const quarantined& q) { return q.id == id && q.program == program; });
        list.push_back({program, id, text_of(p, "reason", "unstable"), until});
    } else if (action == "remove") {
        std::erase_if(list, [&](const quarantined& q) {
            return q.id == id && (program.empty() || q.program == program);
        });
    } else if (action != "list") {
        return usage("quarantine add, remove or list");
    }
    if (action != "list")
        set_quarantine(ctx->st, list);
    json items = json::array();
    std::string text;
    for (const auto& q : list) {
        items.push_back(
            {{"program", q.program}, {"case", q.id}, {"reason", q.reason}, {"until", q.until}});
        text += std::format("{}::{} until {} -- {}\n", q.program, q.id, q.until, q.reason);
    }
    return {0,
            {{"items", items}, {"conclusion", std::format("{} case(s) quarantined", items.size())}},
            text};
}

answer cmd_store(const std::filesystem::path& cwd, const json& p) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    const auto action = text_of(p, "action", "verify");
    if (action == "gc") {
        auto g = collect(ctx->st, p.value("keep_days", ctx->cfg.keep_days),
                         p.value("keep_runs", ctx->cfg.keep_runs));
        auto msg = std::format("removed {} run(s) ({} bytes), kept {}", g.removed, g.bytes, g.kept);
        return {0,
                {{"removed", g.removed}, {"bytes", g.bytes}, {"kept", g.kept}, {"conclusion", msg}},
                msg + "\n"};
    }
    if (action == "verify") {
        auto problems = verify(ctx->st);
        std::string text;
        for (const auto& x : problems)
            text += x + "\n";
        auto msg = problems.empty() ? std::string("the store is sound")
                                    : std::format("{} problem(s)", problems.size());
        return {problems.empty() ? 0 : 1,
                {{"problems", problems}, {"conclusion", msg}},
                text + msg + "\n"};
    }
    if (action == "import") {
        auto from = text_of(p, "from");
        if (from.empty())
            return usage("store import needs --from <store directory>");
        auto n = import_store(ctx->st, os::fs::from_utf8(from));
        auto msg = std::format("took {} run(s) from {}", n, from);
        return {0, {{"imported", n}, {"conclusion", msg}}, msg + "\n"};
    }
    return usage("store gc, verify or import");
}

// The tools `trace --with` knows without configuration; a project adds or replaces one under
// [trace.<name>].
std::optional<trace_tool> builtin_tool(std::string_view name, const std::filesystem::path& out) {
    const auto o = os::fs::utf8(out);
    if (name == "valgrind")
        return trace_tool{"valgrind",
                          {"valgrind", "--error-exitcode=99", "--leak-check=full",
                           "--log-file=" + o},
                          {},
                          {}};
    if (name == "strace")
        return trace_tool{"strace", {"strace", "-f", "-o", o}, {}, {}};
    if (name == "ltrace")
        return trace_tool{"ltrace", {"ltrace", "-f", "-o", o}, {}, {}};
    if (name == "perf")
        return trace_tool{"perf", {"perf", "record", "-g", "-o", o, "--"}, {}, {}};
    if (name == "rr")
        return trace_tool{"rr", {"rr", "record", "-o", o}, {}, {}};
    if (name == "time")
        return trace_tool{"time", {"/usr/bin/time", "-v", "-o", o}, {}, {}};
    if (name == "okrun-prof")
        return trace_tool{"okrun-prof", {}, {}, {{"OKRUN_PROF", o}}};
    return std::nullopt;
}

answer cmd_trace(const std::filesystem::path& cwd, const json& p,
                 const std::function<void(const json&)>& events) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    const auto with = text_of(p, "with");
    if (with.empty())
        return usage("trace needs --with <tool>: valgrind, strace, ltrace, perf, rr, time, "
                     "okrun-prof, or one of [trace.*]");
    const auto id = new_run_id();
    const auto out = run_dir(ctx->st, id) / std::format("trace-{}.out", with);
    std::filesystem::create_directories(out.parent_path());
    std::optional<trace_tool> tool;
    if (auto it = ctx->cfg.tools.find(with); it != ctx->cfg.tools.end())
        tool = it->second;
    else
        tool = builtin_tool(with, out);
    if (!tool)
        return usage(std::format("no tool {}: declare [trace.{}] run_under = [...] (and build = "
                                 "[...] for a sanitizer) in mtest.toml",
                                 with, with));
    if (!tool->build.empty()) {
        os::process::spawn_options b;
        b.program = tool->build.front();
        b.args.assign(tool->build.begin() + 1, tool->build.end());
        b.cwd = ctx->cfg.root;
        b.in = os::process::stdio::null;
        auto built = os::process::run(std::move(b));
        if (!built || !built->status.success())
            return machine(
                std::format("the build of trace.{} failed: {}", with,
                            built ? built->err.substr(0, 2000) : built.error().message()));
    }
    json q = p;
    q["run_under"] = tool->run_under;
    json env = p.value("env", json::object());
    for (const auto& [k, v] : tool->env)
        env[k] = v;
    q["env"] = env;
    q.erase("with");
    auto a = cmd_run(cwd, q, events);
    a.data["trace"] = {{"tool", with},
                       {"output", os::fs::generic_utf8(out)},
                       {"exists", std::filesystem::exists(out)}};
    a.text +=
        std::format("trace: {} wrote {}\n", with,
                    std::filesystem::exists(out) ? os::fs::utf8(out) : std::string("nothing"));
    return a;
}

answer cmd_bisect(const std::filesystem::path& cwd, const json& p) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    const auto good = text_of(p, "good"), bad = text_of(p, "bad", "HEAD"), id = text_of(p, "case");
    if (good.empty() || id.empty())
        return usage(
            "bisect needs --case <id> --good <commit> [--bad <commit>] [--build '<command>']");
    auto git = [&](std::vector<std::string> args) {
        os::process::spawn_options o;
        o.program = "git";
        o.args = std::move(args);
        o.cwd = ctx->cfg.root;
        o.in = os::process::stdio::null;
        return os::process::run(std::move(o));
    };
    // Each step: build (when asked), then run the one case; 0 good, 1 bad, 125 not testable (it did
    // not build or start).
    auto self = os::env::self_command();
    std::vector<std::string> step = {"bisect", "run"};
    step.insert(step.end(), self.begin(), self.end());
    step.push_back("bisect-step");
    step.push_back("--case");
    step.push_back(id);
    for (const auto& pr : strings(p, "program")) {
        step.push_back("--program");
        step.push_back(pr);
    }
    if (auto b = text_of(p, "build"); !b.empty()) {
        step.push_back("--build");
        step.push_back(b);
    }
    if (auto s = git({"bisect", "start", bad, good}); !s || !s->status.success())
        return machine("git bisect start failed: " + (s ? s->err : s.error().message()));
    auto r = git(step);
    auto log = git({"bisect", "log"});
    (void)git({"bisect", "reset"});
    if (!r)
        return machine("git bisect run did not start: " + r.error().message());
    std::string first;
    std::string_view rest = r->out;
    if (auto at = rest.find(" is the first bad commit"); at != std::string_view::npos) {
        auto start = rest.rfind('\n', at);
        first = std::string(rest.substr(start == std::string_view::npos ? 0 : start + 1,
                                        at - (start == std::string_view::npos ? 0 : start + 1)));
    }
    json data = {{"case", id},
                 {"good", good},
                 {"bad", bad},
                 {"first_bad", first},
                 {"log", log ? log->out : std::string{}},
                 {"conclusion", first.empty() ? std::string("git bisect found no first bad commit")
                                              : std::format("{} first fails at {}", id, first)}};
    return {first.empty() ? 1 : 0, data, r->out + data["conclusion"].get<std::string>() + "\n"};
}

answer cmd_bisect_step(const std::filesystem::path& cwd, const json& p) {
    if (auto b = text_of(p, "build"); !b.empty()) {
        auto argv = split_command(b);
        os::process::spawn_options o;
        o.program = argv.front();
        o.args.assign(argv.begin() + 1, argv.end());
        o.cwd = project_root(cwd);
        o.in = os::process::stdio::null;
        auto built = os::process::run(std::move(o));
        if (!built || !built->status.success())
            return {125, {{"conclusion", "did not build"}}, "the step did not build: skipped\n"};
    }
    json q = p;
    q.erase("build");
    auto a = cmd_run(cwd, q, {});
    if (a.status == 2 || a.status == 3)
        a.status = 125;
    return a;
}

// THE PIPELINE AS A GRAPH (`mtest ci timeline --repo OWNER/REPO --run-id N`): the jobs of a GitHub
// Actions run, by the GitHub CLI, each a node from its start to its end; a job's predecessor is the
// job that ended last before it began, which is what it waited for when it waited (its `needs` or
// a runner). The critical path runs back from the job that ended last.
answer ci_timeline(const json& p) {
    const auto repo = text_of(p, "repo");
    const auto id = text_of(p, "run_id");
    if (repo.empty() || id.empty())
        return usage("ci timeline needs --repo OWNER/REPO and --run-id N");
    os::process::spawn_options o;
    o.program = "gh";
    o.args = {"run", "view", id, "-R", repo, "--json", "jobs,createdAt,displayTitle"};
    o.in = os::process::stdio::null;
    auto r = os::process::run(std::move(o));
    if (!r)
        return machine("gh did not start: " + r.error().message());
    auto j = json::parse(r->out, nullptr, false);
    if (j.is_discarded() || !j.contains("jobs"))
        return machine("gh gave no jobs: " + r->err);
    // `YYYY-MM-DDTHH:MM:SSZ`, read field by field: not every standard library
    // has `std::chrono::parse`.
    auto epoch = [](const std::string& iso) -> double {
        int f[6]{};
        constexpr std::size_t at[6] = {0, 5, 8, 11, 14, 17}, width[6] = {4, 2, 2, 2, 2, 2};
        if (iso.size() < 20 || iso[19] != 'Z')
            return -1.0;
        for (int i = 0; i < 6; ++i) {
            const char* b = iso.data() + at[i];
            if (std::from_chars(b, b + width[i], f[i]).ptr != b + width[i])
                return -1.0;
        }
        const std::chrono::year_month_day day{std::chrono::year{f[0]},
                                              std::chrono::month(unsigned(f[1])),
                                              std::chrono::day(unsigned(f[2]))};
        if (!day.ok())
            return -1.0;
        const auto t = std::chrono::sys_days{day} + std::chrono::hours{f[3]} +
                       std::chrono::minutes{f[4]} + std::chrono::seconds{f[5]};
        return double(
                   std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count()) *
               1000;
    };
    const double t0 = epoch(j.value("createdAt", ""));
    run_record run;
    run.id = std::format("{}#{}", repo, id);
    for (const auto& job : j["jobs"]) {
        const double a = epoch(job.value("startedAt", "")), b = epoch(job.value("completedAt", ""));
        if (a < 0 || b < a)
            continue;
        program_result pr;
        pr.name = job.value("name", "");
        pr.status = job.value("conclusion", "");
        pr.started_ms = a - t0;
        pr.ms = b - a;
        pr.worker = int(run.programs.size());  // each job its own runner
        run.programs.push_back(std::move(pr));
        run.ms = std::max(run.ms, b - t0);
    }
    // Each job waited for the job that ended last before it began.
    auto g = dag_of(run);
    for (std::size_t i = 0; i < g.nodes.size(); ++i) {
        std::optional<std::size_t> before;
        for (std::size_t k = 0; k < g.nodes.size(); ++k)
            if (k != i && g.nodes[k].end_ms <= g.nodes[i].start_ms + 1000 &&
                (!before || g.nodes[k].end_ms > g.nodes[*before].end_ms))
                before = k;
        if (before)
            g.edges.push_back({*before, i, "after", false});
    }
    // The critical path again, over these edges.
    std::size_t last = 0;
    for (std::size_t i = 1; i < g.nodes.size(); ++i)
        if (g.nodes[i].end_ms > g.nodes[last].end_ms)
            last = i;
    g.path.clear();
    g.path_ms = 0;
    for (std::optional<std::size_t> at = g.nodes.empty() ? std::nullopt : std::optional(last);
         at;) {
        g.path.insert(g.path.begin(), *at);
        g.nodes[*at].critical = true;
        g.path_ms += g.nodes[*at].ms();
        std::optional<std::size_t> next;
        for (auto& e : g.edges)
            if (e.to == *at) {
                e.critical = true;
                next = e.from;
            }
        at = next;
    }
    std::string md = std::format("# The jobs of {} ({})\n\n", run.id, j.value("displayTitle", ""));
    md += std::
        format("Wall clock {:.1f} min; the critical path is {} job(s), {:.1f} min of work.\n\n",
               run.ms / 60000, g.path.size(), g.path_ms / 60000);
    md += "| job | start | time |\n|---|---:|---:|\n";
    for (auto i : g.path)
        md += std::format("| {} | {:.1f} min | {:.1f} min |\n", g.nodes[i].label,
                          g.nodes[i].start_ms / 60000, g.nodes[i].ms() / 60000);
    md += "\n```mermaid\n" + mermaid_gantt(g, p.value("max_nodes", 80)) + "```\n";
    if (auto summary = os::env::get("GITHUB_STEP_SUMMARY");
        summary && !summary->empty() && p.value("summary", false))
        (void)os::fs::append(os::fs::from_utf8(*summary), md);
    if (auto out = text_of(p, "out"); !out.empty())
        (void)os::fs::write(os::fs::from_utf8(out), md);
    json path = json::array();
    for (auto i : g.path)
        path.push_back({{"job", g.nodes[i].label},
                        {"start_ms", g.nodes[i].start_ms},
                        {"ms", g.nodes[i].ms()}});
    return {0,
            {{"run", run.id},
             {"wall", run.ms},
             {"path", path},
             {"conclusion", std::format("the critical path is {} job(s) of a {:.1f} min run",
                                        g.path.size(), run.ms / 60000)}},
            md};
}

// `mtest capabilities`: how this machine answers each word of [capabilities] (0.7.0, M1).
answer cmd_capabilities(const std::filesystem::path& cwd, const json&) {
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    json words = json::object();
    std::string text = std::format("host {}\n", host_name());
    std::size_t present = 0;
    for (const auto& [w, a] : machine_capabilities(ctx->cfg)) {
        words[w] = {{"present", a.present}, {"why", a.why}};
        text += std::format("  {} {} -- {}\n", a.present ? "+" : "-", w, a.why);
        present += a.present;
    }
    return {0,
            {{"host", host_name()},
             {"capabilities", words},
             {"conclusion", std::format("{} of {} capabilities present on {}", present,
                                        ctx->cfg.capabilities.words.size(), host_name())}},
            text};
}

answer cmd_ci(const std::filesystem::path& cwd, const json& p) {
    const auto action = text_of(p, "action", "status");
    if (action == "status") {
        // The checks of each pull request, by the GitHub CLI and the token it has (the
        // environment's or the system's).
        json items = json::array();
        std::string text;
        int failing_prs = 0, pending_prs = 0;
        for (const auto& pr : strings(p, "prs")) {
            const auto hash = pr.find('#');
            if (hash == std::string::npos)
                return usage(std::format("{} is not owner/repo#number", pr));
            os::process::spawn_options o;
            o.program = "gh";
            o.args = {"pr",
                      "checks",
                      pr.substr(hash + 1),
                      "-R",
                      pr.substr(0, hash),
                      "--json",
                      "name,state,link,bucket"};
            o.in = os::process::stdio::null;
            auto r = os::process::run(std::move(o));
            if (!r)
                return machine("gh did not start: " + r.error().message());
            auto checks = json::parse(r->out, nullptr, false);
            if (checks.is_discarded())
                checks = json::array();
            std::map<std::string, int> buckets;
            json failed = json::array();
            for (const auto& c : checks) {
                const auto b = c.value("bucket", c.value("state", ""));
                buckets[b]++;
                if (b == "fail")
                    failed.push_back(
                        {{"name", c.value("name", "")}, {"link", c.value("link", "")}});
            }
            failing_prs += buckets["fail"] > 0;
            pending_prs += buckets["pending"] > 0;
            items.push_back({{"pr", pr}, {"buckets", buckets}, {"failing", failed}});
            text += std::format("{}: {} pass, {} fail, {} pending\n", pr, buckets["pass"],
                                buckets["fail"], buckets["pending"]);
            for (const auto& f : failed)
                text += std::format("  FAIL {}  {}\n", f["name"].get<std::string>(),
                                    f["link"].get<std::string>());
        }
        return {failing_prs ? 1 : 0,
                {{"items", items},
                 {"conclusion", std::format("{} pull request(s): {} failing, {} pending",
                                            items.size(), failing_prs, pending_prs)}},
                text};
    }
    auto ctx = open(cwd);
    if (!ctx)
        return ctx.error();
    if (action == "annotate") {
        // GitHub's annotations of each failure, and the summary of the job.
        auto r = run_named(*ctx, p);
        if (!r)
            return r.error();
        std::string text;
        for (const auto& c : r->cases) {
            if (!failing(c.status) || c.quarantined)
                continue;
            const auto& f = c.failures.empty() ? failure_info{} : c.failures.front();
            text += std::format("::error{}::{}::{} {}\n",
                                f.file.empty() ? std::string{}
                                               : std::format(" file={},line={}", f.file, f.line),
                                c.program, c.id, f.message.empty() ? c.reason : f.message);
        }
        if (auto summary = os::env::get("GITHUB_STEP_SUMMARY"); summary && !summary->empty())
            (void)os::fs::append(os::fs::from_utf8(*summary),
                                 markdown_report(ctx->st, {*r}, report_options_of(*ctx, p)));
        return {0, {{"conclusion", std::format("annotated run {}", r->id)}}, text};
    }
    if (action == "report") {
        // The runs of one group (the jobs of a CI run, collected) as one report, for the summary
        // of the job that collects them.
        json q = p;
        if (!q.contains("group"))
            if (auto id = os::env::get("GITHUB_RUN_ID"))
                q["group"] = *id;
        auto all = runs_of(*ctx, q);
        if (!all)
            return all.error();
        auto o = report_options_of(*ctx, q);
        if (o.title.empty())
            o.title = std::format("mtest report of {} run(s)", all->size());
        const auto body = markdown_report(ctx->st, *all, o);
        if (auto summary = os::env::get("GITHUB_STEP_SUMMARY"); summary && !summary->empty())
            (void)os::fs::append(os::fs::from_utf8(*summary), body);
        if (auto out = text_of(p, "out"); !out.empty())
            (void)os::fs::write(os::fs::from_utf8(out), body);
        return {0,
                {{"runs", all->size()},
                 {"conclusion", std::format("reported {} run(s)", all->size())}},
                body};
    }
    if (action == "timeline")
        return ci_timeline(p);
    if (action == "collect") {
        auto from = text_of(p, "from");
        if (from.empty())
            return usage("ci collect needs --from <a downloaded store>");
        auto n = import_store(ctx->st, os::fs::from_utf8(from));
        return {0,
                {{"imported", n}, {"conclusion", std::format("took {} run(s)", n)}},
                std::format("took {} run(s)\n", n)};
    }
    if (action == "coverage" || action == "plan") {
        // The runs judged: a group's (default: GITHUB_RUN_ID's) or, with none, every stored run.
        std::vector<run_record> all;
        std::string group = text_of(p, "group");
        if (group.empty() && action == "coverage")
            group = os::env::get("GITHUB_RUN_ID").value_or("");
        for (const auto& r : runs(ctx->st))
            if (group.empty() || r.group == group)
                if (auto full = load_run(ctx->st, r.id))
                    all.push_back(std::move(*full));
        if (action == "coverage" && all.empty())
            return usage(group.empty()
                             ? std::string("ci coverage: the store holds no run (ci collect first)")
                             : std::format("ci coverage: no stored run of group {}", group));
        auto programs = programs_of(*ctx, p);
        if (!programs)
            return programs.error();
        if (action == "coverage") {
            auto c = ci_coverage(ctx->cfg, *programs, all);
            return {c.status, c.data, c.text};
        }
        auto sel = selection_of(*ctx, p);
        if (!sel)
            return sel.error();
        auto pl = make_plan(ctx->cfg, ctx->st, *programs, *sel);
        if (!pl)
            return machine(pl.error());
        auto c = ci_plan(ctx->cfg, *pl, all, strings(p, "hosts"), text_of(p, "format"));
        return {c.status, c.data, c.text};
    }
    return usage("ci status, annotate, collect, report, timeline, coverage or plan");
}

}  // namespace

answer dispatch(const std::filesystem::path& cwd, std::string_view command, const json& params,
                const std::function<void(const json&)>& events) {
    const json& p = params.is_object() ? params : json::object();
    forget_capabilities();
    if (command == "run")
        return cmd_run(cwd, p, events);
    if (command == "rerun") {
        json q = p;
        q["failed"] = true;
        return cmd_run(cwd, q, events);
    }
    if (command == "smoke") {
        json q = p;
        q["set"] = "smoke";
        return cmd_run(cwd, q, events);
    }
    if (command == "golden") {
        json q = p;
        json env = q.value("env", json::object());
        env["MTEST_GOLDEN"] = "update";
        q["env"] = env;
        return cmd_run(cwd, q, events);
    }
    if (command == "plan" || command == "affected")
        return cmd_plan(cwd, p);
    if (command == "coverage")
        return cmd_coverage(cwd, p);
    if (command == "list")
        return cmd_list(cwd, p);
    if (command == "status")
        return cmd_status(cwd, p);
    if (command == "cancel")
        return cmd_cancel(cwd, p);
    if (command == "report")
        return cmd_report(cwd, p);
    if (command == "diff")
        return cmd_diff(cwd, p);
    if (command == "explain")
        return cmd_explain(cwd, p);
    if (command == "history")
        return cmd_history(cwd, p);
    if (command == "query")
        return cmd_query(cwd, p);
    if (command == "analyze")
        return cmd_analyze(cwd, p);
    if (command == "hot") {
        json q = p;
        q["metric"] = "hot";
        return cmd_analyze(cwd, q);
    }
    if (command == "path") {
        json q = p;
        q["metric"] = "path";
        return cmd_analyze(cwd, q);
    }
    if (command == "graph")
        return cmd_graph(cwd, p);
    if (command == "gate")
        return cmd_gate(cwd, p);
    if (command == "quarantine")
        return cmd_quarantine(cwd, p);
    if (command == "store")
        return cmd_store(cwd, p);
    if (command == "trace")
        return cmd_trace(cwd, p, events);
    if (command == "bisect")
        return cmd_bisect(cwd, p);
    if (command == "bisect-step")
        return cmd_bisect_step(cwd, p);
    if (command == "measure") {
        std::vector<std::string> argv;
        if (auto it = p.find("argv"); it != p.end() && it->is_array())
            for (const auto& v : *it)
                if (v.is_string())
                    argv.push_back(v.get<std::string>());
        const int code = measure(os::fs::from_utf8(text_of(p, "out")), argv);
        return {code, {{"status", code}, {"conclusion", std::format("ended with {}", code)}}, ""};
    }
    if (command == "capabilities")
        return cmd_capabilities(cwd, p);
    if (command == "ci")
        return cmd_ci(cwd, p);
    return usage(std::format("no command {} (mtest help)", command));
}

json catalog() {
    const json selection = {
        {"programs",
         {{"type", "array"},
          {"items", {{"type", "string"}}},
          {"description", "commands to run, each a program and its arguments"}}},
        {"set",
         {{"type", "string"},
          {"description", "smoke, regression, pr, full, release, or a declared set"}}},
        {"case",
         {{"type", "array"},
          {"items", {{"type", "string"}}},
          {"description", "case identifier globs, `program::id` or `id`"}}},
        {"tag",
         {{"type", "array"},
          {"items", {{"type", "string"}}},
          {"description", "case tags, one of them; `a&b` names a case with both"}}},
        {"exclude_tag",
         {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "tags left out"}}},
        {"kind",
         {{"type", "array"},
          {"items", {{"enum", {"unit", "e2e", "cts", "bench"}}}},
          {"description", "unit, e2e, cts or bench"}}},
        {"program",
         {{"type", "array"},
          {"items", {{"type", "string"}}},
          {"description", "program name globs"}}},
        {"since",
         {{"type", "string"}, {"description", "a git commit: only the cases its change affects"}}},
        {"changed",
         {{"type", "array"},
          {"items", {{"type", "string"}}},
          {"description", "changed paths: only the cases they affect"}}},
        {"budget",
         {{"type", "string"}, {"description", "a duration: the most useful cases that fit"}}},
        {"exclude_program",
         {{"type", "array"},
          {"items", {{"type", "string"}}},
          {"description", "programs whose name a glob matches are left out"}}},
        {"shard",
         {{"type", "string"},
          {"description", "I/N: the I-th of N parts by expected time, the same on every machine"}}},
        {"failed",
         {{"type", "boolean"}, {"description", "only the cases that failed in the last run"}}},
        {"argv",
         {{"type", "array"},
          {"items", {{"type", "string"}}},
          {"description", "one program and its arguments (on the command line, after `--`)"}}},
    };
    auto with = [&](json extra) {
        json props = selection;
        for (auto it = extra.begin(); it != extra.end(); ++it)
            props[it.key()] = it.value();
        return json{{"type", "object"}, {"properties", props}};
    };
    const json run_id = {
        {"run", {{"type", "string"}, {"description", "a run identifier, `last` or `last-<n>`"}}}};
    auto plain = [](json props) { return json{{"type", "object"}, {"properties", props}}; };
    json tools = json::array();
    auto add = [&](const char* name, const char* description, json schema, bool effects = false) {
        tools.push_back({{"name", name},
                         {"description", description},
                         {"inputSchema", schema},
                         {"annotations", {{"readOnlyHint", !effects}}}});
    };
    add("run",
        "Run tests: select by set, case, tag, kind, program, change or budget; returns the run's "
        "record, its failures and a conclusion.",
        with({{"jobs", {{"type", "integer"}, {"description", "programs at once"}}},
              {"seed", {{"type", "integer"}, {"description", "the seed of anything random"}}},
              {"fail_fast", {{"type", "boolean"}, {"description", "stop at the first failure"}}},
              {"resume",
               {{"type", "string"},
                {"description", "a run whose judged cases are carried and not run again"}}},
              {"retries", {{"type", "integer"}, {"description", "runs again of a failed case"}}},
              {"isolate", {{"type", "boolean"}, {"description", "each case in its own process"}}},
              {"timeout", {{"type", "string"}, {"description", "a duration for each program"}}},
              {"env", {{"type", "object"}, {"description", "NAME=VALUE for the programs"}}},
              {"run_under",
               {{"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "a command the programs run under"}}},
              {"verbose", {{"type", "boolean"}, {"description", "every case, not only failures"}}},
              {"coverage",
               {{"type", "boolean"},
                {"description", "measure the coverage map ([coverage] in mtest.toml)"}}},
              {"report",
               {{"type", "object"},
                {"properties",
                 {{"junit", {{"type", "string"}, {"description", "a JUnit XML file of the run"}}},
                  {"html", {{"type", "string"}, {"description", "an HTML page of the run"}}},
                  {"json", {{"type", "string"}, {"description", "the run's record as JSON"}}},
                  {"md",
                   {{"type", "string"},
                    {"description", "a Markdown report of the run ([report])"}}}}}}},
              {"monitor",
               {{"enum", {"off", "basic", "detailed", "trace"}},
                {"description", "how closely use is measured ([monitor] level)"}}},
              {"group",
               {{"type", "string"},
                {"description", "runs of one group are reported together (MTEST_GROUP)"}}},
              {"sections",
               {{"type", "string"}, {"description", "the report's sections, comma-separated"}}}}),
        true);
    add("rerun", "Run again the cases that failed in the last run.", with({}), true);
    add("smoke", "Run the smoke set (within its budget).", with({}), true);
    add("golden", "Run with MTEST_GOLDEN=update: golden files are written instead of compared.",
        with({}), true);
    add("plan", "What a selection would run, and why each case is in it; nothing runs.", with({}));
    add("affected", "The cases a change affects (since or changed), as plan.", with({}));
    add("list",
        "The programs and the cases each lists; --static: the cases the test files declare, read "
        "without building.",
        with({{"static",
               {{"type", "boolean"},
                {"description", "read the test files' text; build and run nothing"}}}}));
    add("status", "The runs in progress, and the last run.", plain({}));
    add("cancel", "Stop a run in progress after the programs now running.", plain(run_id), true);
    add("report",
        "A stored run as text, JSON, JUnit XML, HTML, or Markdown with its graph and critical "
        "path.",
        plain({{"run", run_id["run"]},
               {"format", {{"enum", {"text", "json", "junit", "html", "md"}}}},
               {"out", {{"type", "string"}}},
               {"group", {{"type", "string"}, {"description", "md: every stored run of a group"}}},
               {"runs", {{"type", "string"}, {"description", "md: runs, comma-separated"}}},
               {"sections",
                {{"type", "string"},
                 {"description", "md: summary, kinds, failures, slowest, metrics, resources, "
                                 "flaky, trend, path, graph, gantt"}}},
               {"scope",
                {{"type", "string"}, {"description", "md: run, program:<glob>, kind:<kind>"}}},
               {"top", {{"type", "integer"}}},
               {"max_nodes", {{"type", "integer"}}},
               {"title", {{"type", "string"}}}}));
    add("diff", "What changed between two runs: newly failing, fixed, slower, faster.",
        plain({{"before", run_id["run"]},
               {"after", run_id["run"]},
               {"ratio", {{"type", "number"}}}}));
    add("explain",
        "Where a case failed, why, its output, its history and the command that repeats it.",
        plain({{"run", run_id["run"]}, {"case", {{"type", "string"}}}}));
    add("history", "One case across the stored runs.",
        plain({{"case", {{"type", "string"}}},
               {"program", {{"type", "string"}}},
               {"limit", {{"type", "integer"}}}}));
    add("query", "Stored case results narrowed by status, kind, program, case, tag, days or runs.",
        plain({{"status", {{"type", "array"}}},
               {"kind", {{"type", "array"}}},
               {"program", {{"type", "array"}}},
               {"case", {{"type", "array"}}},
               {"tag", {{"type", "array"}}},
               {"days", {{"type", "integer"}}},
               {"runs", {{"type", "integer"}}},
               {"limit", {{"type", "integer"}}}}));
    add("analyze",
        "A metric of a run: hot, durations, path, failures, bench, cost; or across runs: flaky, "
        "and "
        "trend (each case's metrics beside their bounds).",
        plain({{"metric",
                {{"enum",
                  {"hot", "durations", "path", "failures", "flaky", "bench", "trend", "cost"}}}},
               {"run", run_id["run"]},
               {"top", {{"type", "integer"}}},
               {"runs", {{"type", "integer"}}},
               {"case", {{"type", "array"}, {"items", {{"type", "string"}}}}}}));
    add("graph",
        "The topology: sources, programs, cases, resources and artifacts; or, with --run, a run's "
        "graph of work with its critical path (JSON, DOT, Mermaid, gantt).",
        with({{"format", {{"enum", {"json", "dot", "mermaid", "gantt"}}}},
              {"run", run_id["run"]},
              {"max_nodes", {{"type", "integer"}}}}));
    add("gate", "A set's gate on a run: failures, budget, bench verdicts.",
        plain({{"run", run_id["run"]}, {"set", {{"type", "string"}}}}));
    add("quarantine",
        "List, add or remove quarantined cases (their failures do not fail a run until a date).",
        plain({{"action", {{"enum", {"list", "add", "remove"}}}},
               {"case", {{"type", "string"}}},
               {"program", {{"type", "string"}}},
               {"reason", {{"type", "string"}}},
               {"until", {{"type", "string"}}}}),
        true);
    add("coverage",
        "The coverage map affected selection reads: show it, or import one (a CI artifact).",
        plain({{"action", {{"enum", {"show", "import"}}}},
               {"from", {{"type", "string"}, {"description", "import: a coverage map file"}}},
               {"case", {{"type", "array"}, {"items", {{"type", "string"}}}}}}));
    add("store", "The store: gc, verify, or import another store.",
        plain({{"action", {{"enum", {"gc", "verify", "import"}}}},
               {"from", {{"type", "string"}, {"description", "a store to import"}}},
               {"keep_days", {{"type", "integer"}, {"description", "gc: days a run is kept"}}},
               {"keep_runs", {{"type", "integer"}, {"description", "gc: runs kept"}}}}),
        true);
    add("trace",
        "Run a selection under a tool (valgrind, strace, ltrace, perf, rr, time, okrun-prof, or "
        "[trace.*]) and keep its output.",
        with({{"with", {{"type", "string"}}}}), true);
    add("bisect", "The first commit at which a case fails, between a good and a bad one.",
        plain({{"case", {{"type", "string"}}},
               {"good", {{"type", "string"}}},
               {"bad", {{"type", "string"}}},
               {"build", {{"type", "string"}}},
               {"program", {{"type", "array"}}}}),
        true);
    add("ci",
        "Across pull requests: their checks (status); a run's failures as annotations and its "
        "report as the job's summary (annotate); a CI store taken in (collect); the runs of a "
        "group as one report (report); a workflow run's jobs as a graph with its critical path "
        "(timeline); whether every program ran on some host (coverage); a selection divided among "
        "the hosts by their history (plan).",
        with({{"action",
               {{"enum",
                 {"status", "annotate", "collect", "report", "timeline", "coverage", "plan"}}}},
              {"prs", {{"type", "array"}, {"items", {{"type", "string"}}}}},
              {"run", run_id["run"]},
              {"from", {{"type", "string"}}},
              {"group", {{"type", "string"}}},
              {"sections", {{"type", "string"}}},
              {"out", {{"type", "string"}}},
              {"repo", {{"type", "string"}, {"description", "timeline: OWNER/REPO"}}},
              {"run_id", {{"type", "string"}, {"description", "timeline: the workflow run"}}},
              {"summary",
               {{"type", "boolean"}, {"description", "timeline: also the job's summary"}}},
              {"hosts",
               {{"type", "array"},
                {"items", {{"type", "string"}}},
                {"description", "plan: the hosts ([ci.plan] hosts)"}}},
              {"format",
               {{"enum", {"text", "github"}},
                {"description", "plan: github writes GITHUB_OUTPUT"}}}}));
    add("capabilities", "How this machine answers each capability word of [capabilities].",
        plain({}));
    add("measure", "Run a program and write what it used (its children's account) to --out.",
        plain({{"out", {{"type", "string"}}},
               {"argv", {{"type", "array"}, {"items", {{"type", "string"}}}}}}),
        true);
    return {{"schema", "mtest.catalog/1"}, {"version", "0.8.2"}, {"tools", tools}};
}

}  // namespace mcpplibs::mtest::engine
