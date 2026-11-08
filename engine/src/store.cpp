module mcpplibs.mtest.engine.store;

import std;
import mcpplibs.os;

namespace mcpplibs::mtest::engine {

namespace os = mcpplibs::os;

namespace {

std::string read_text(const std::filesystem::path& file) {
    auto r = os::fs::read(file);
    return r ? *r : std::string{};
}

std::vector<json> read_lines(const std::filesystem::path& file) {
    std::vector<json> out;
    std::string text = read_text(file);
    std::string_view rest = text;
    while (!rest.empty()) {
        const auto nl = rest.find('\n');
        auto line = rest.substr(0, nl);
        rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
        if (line.empty())
            continue;
        auto v = json::parse(line, nullptr, false);
        if (!v.is_discarded())
            out.push_back(std::move(v));
    }
    return out;
}

std::string today() {
    const auto d = std::chrono::year_month_day(
        std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()));
    return std::format("{:%Y-%m-%d}", std::chrono::sys_days(d));
}

std::uintmax_t tree_bytes(const std::filesystem::path& dir) {
    std::uintmax_t total = 0;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; it != end;
         it.increment(ec))
        if (it->is_regular_file(ec))
            total += it->file_size(ec);
    return total;
}

}  // namespace

store open_store(const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir / "runs");
    if (!std::filesystem::exists(dir / ".gitignore"))
        (void)os::fs::write(dir / ".gitignore", "# mtest's store of runs: the machine's results, "
                                                "never the project's sources.\n*\n");
    return {dir};
}

std::filesystem::path run_dir(const store& s, std::string_view id) {
    return s.dir / "runs" / std::string(id);
}

void write_run(const store& s, const run_record& r) {
    const auto dir = run_dir(s, r.id);
    std::filesystem::create_directories(dir);
    const auto meta = dumped(to_json(r));
    (void)os::fs::write_atomic(dir / "run.json", meta + "\n");
    std::string cases, programs;
    for (const auto& c : r.cases)
        cases += dumped(to_json(c)) + "\n";
    for (const auto& p : r.programs)
        programs += dumped(to_json(p)) + "\n";
    (void)os::fs::write_atomic(dir / "cases.ndjson", cases);
    (void)os::fs::write_atomic(dir / "programs.ndjson", programs);
    if (!r.fixtures.empty()) {
        std::string fixtures;
        for (const auto& f : r.fixtures)
            fixtures += dumped(to_json(f)) + "\n";
        (void)os::fs::write_atomic(dir / "fixtures.ndjson", fixtures);
    }
    (void)os::fs::append(s.dir / "index.ndjson", meta + "\n");
}

void write_failure(const store& s, std::string_view id, std::size_t n, const program_result& p,
                   std::string_view out, std::string_view err) {
    const auto dir = run_dir(s, id) / "failures" / std::to_string(n);
    std::filesystem::create_directories(dir);
    (void)os::fs::write(dir / "out.txt", out);
    (void)os::fs::write(dir / "err.txt", err);
    (void)os::fs::write(dir / "program.json", dumped(to_json(p), 2) + "\n");
}

std::vector<run_record> runs(const store& s) {
    std::vector<run_record> out;
    for (const auto& j : read_lines(s.dir / "index.ndjson"))
        out.push_back(run_record_from(j));
    return out;
}

std::optional<run_record> load_run(const store& s, std::string_view id) {
    std::string wanted(id);
    if (id == "last" || id.starts_with("last-")) {
        auto all = runs(s);
        std::size_t back = 0;
        if (id.starts_with("last-")) {
            auto [p, ec] = std::from_chars(id.data() + 5, id.data() + id.size(), back);
            if (ec != std::errc{})
                return std::nullopt;
        }
        if (all.size() <= back)
            return std::nullopt;
        wanted = all[all.size() - 1 - back].id;
    }
    const auto dir = run_dir(s, wanted);
    auto meta = json::parse(read_text(dir / "run.json"), nullptr, false);
    if (meta.is_discarded())
        return std::nullopt;
    auto r = run_record_from(meta);
    for (const auto& j : read_lines(dir / "cases.ndjson"))
        r.cases.push_back(case_result_from(j));
    for (const auto& j : read_lines(dir / "programs.ndjson"))
        r.programs.push_back(program_result_from(j));
    for (const auto& j : read_lines(dir / "fixtures.ndjson"))
        r.fixtures.push_back(fixture_run_from(j));
    return r;
}

std::optional<std::pair<std::string, std::string>>
failure_outputs(const store& s, std::string_view id, std::string_view program) {
    std::error_code ec;
    const auto dir = run_dir(s, id) / "failures";
    for (std::filesystem::directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
        auto p = json::parse(read_text(it->path() / "program.json"), nullptr, false);
        if (p.is_discarded() || p.value("name", "") != program)
            continue;
        return std::pair{read_text(it->path() / "out.txt"), read_text(it->path() / "err.txt")};
    }
    return std::nullopt;
}

void write_progress(const store& s, std::string_view id, const json& progress) {
    std::filesystem::create_directories(run_dir(s, id));
    (void)os::fs::write_atomic(run_dir(s, id) / "progress.json", dumped(progress) + "\n");
}

std::vector<std::pair<std::string, json>> active_runs(const store& s) {
    std::vector<std::pair<std::string, json>> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(s.dir / "runs", ec), end; it != end;
         it.increment(ec)) {
        const auto f = it->path() / "progress.json";
        if (!std::filesystem::exists(f))
            continue;
        auto j = json::parse(read_text(f), nullptr, false);
        if (!j.is_discarded())
            out.emplace_back(os::fs::utf8(it->path().filename()), std::move(j));
    }
    std::ranges::sort(out, {}, &std::pair<std::string, json>::first);
    return out;
}

void request_cancel(const store& s, std::string_view id) {
    (void)os::fs::write(run_dir(s, id) / "cancel", "cancel\n");
}
bool cancel_requested(const store& s, std::string_view id) {
    return std::filesystem::exists(run_dir(s, id) / "cancel");
}
void finish_progress(const store& s, std::string_view id) {
    std::error_code ec;
    std::filesystem::remove(run_dir(s, id) / "progress.json", ec);
    std::filesystem::remove(run_dir(s, id) / "cancel", ec);
}

std::vector<quarantined> quarantine(const store& s) {
    std::vector<quarantined> out;
    auto j = json::parse(read_text(s.dir / "quarantine.json"), nullptr, false);
    if (j.is_discarded() || !j.is_array())
        return out;
    for (const auto& q : j)
        out.push_back({q.value("program", ""), q.value("id", ""), q.value("reason", ""),
                       q.value("until", "")});
    return out;
}

void set_quarantine(const store& s, const std::vector<quarantined>& list) {
    json j = json::array();
    for (const auto& q : list)
        j.push_back(
            {{"program", q.program}, {"id", q.id}, {"reason", q.reason}, {"until", q.until}});
    (void)os::fs::write_atomic(s.dir / "quarantine.json", dumped(j, 2) + "\n");
}

bool is_quarantined(const std::vector<quarantined>& list, std::string_view program,
                    std::string_view id) {
    const auto now = today();
    return std::ranges::any_of(list, [&](const quarantined& q) {
        return (q.program.empty() || q.program == program) && q.id == id &&
               (q.until.empty() || now <= q.until);
    });
}

gc_report collect(const store& s, int keep_days, int keep_runs) {
    gc_report g;
    auto all = runs(s);
    const auto cutoff = std::chrono::system_clock::now() - std::chrono::days{keep_days};
    const auto cutoff_id =
        std::format("{:%Y%m%dT%H%M%S}", std::chrono::floor<std::chrono::seconds>(cutoff));
    std::string kept_index;
    for (std::size_t i = 0; i < all.size(); ++i) {
        const bool among_newest = all.size() - i <= std::size_t(std::max(keep_runs, 0));
        // `keep_days` 0 keeps no run for its age.
        const bool recent = keep_days > 0 && all[i].id.substr(0, 15) >= cutoff_id;
        if (among_newest || recent) {
            ++g.kept;
            kept_index += dumped(to_json(all[i])) + "\n";
            continue;
        }
        const auto dir = run_dir(s, all[i].id);
        g.bytes += tree_bytes(dir);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        ++g.removed;
    }
    (void)os::fs::write_atomic(s.dir / "index.ndjson", kept_index);
    return g;
}

std::vector<std::string> verify(const store& s) {
    std::vector<std::string> problems;
    std::set<std::string> indexed;
    for (const auto& r : runs(s)) {
        indexed.insert(r.id);
        if (!load_run(s, r.id))
            problems.push_back(
                std::format("the index names {}, whose run.json is missing or does not parse",
                            r.id));
    }
    std::error_code ec;
    for (std::filesystem::directory_iterator it(s.dir / "runs", ec), end; it != end;
         it.increment(ec)) {
        const auto id = os::fs::utf8(it->path().filename());
        if (!indexed.contains(id) && !std::filesystem::exists(it->path() / "progress.json"))
            problems.push_back(std::format("runs/{} is in no line of the index", id));
    }
    return problems;
}

std::size_t import_store(const store& s, const std::filesystem::path& other) {
    store from{other};
    std::set<std::string> have;
    for (const auto& r : runs(s))
        have.insert(r.id);
    std::size_t taken = 0;
    for (const auto& r : runs(from)) {
        if (have.contains(r.id))
            continue;
        std::error_code ec;
        std::filesystem::copy(run_dir(from, r.id), run_dir(s, r.id),
                              std::filesystem::copy_options::recursive, ec);
        if (ec)
            continue;
        (void)os::fs::append(s.dir / "index.ndjson", dumped(to_json(r)) + "\n");
        ++taken;
    }
    // The coverage map, when the other store's was measured later (docs/coverage.md).
    auto epoch_of = [](const std::filesystem::path& f) {
        std::ifstream in(f, std::ios::binary);
        auto j = json::parse(in, nullptr, false);
        return j.is_object() ? j.value("epoch", std::int64_t{-1}) : std::int64_t{-1};
    };
    if (const auto theirs = epoch_of(from.dir / "coverage.json");
        theirs > epoch_of(s.dir / "coverage.json")) {
        std::error_code ec;
        std::filesystem::copy_file(from.dir / "coverage.json", s.dir / "coverage.json",
                                   std::filesystem::copy_options::overwrite_existing, ec);
    }
    return taken;
}

}  // namespace mcpplibs::mtest::engine
