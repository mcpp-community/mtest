module mcpplibs.mtest.engine.exec;

import std;
import mcpplibs.os;

namespace mcpplibs::mtest::engine {

namespace os = mcpplibs::os;

namespace {

std::vector<std::string> strings_of(const json& e, const char* key) {
    std::vector<std::string> out;
    if (auto it = e.find(key); it != e.end() && it->is_array())
        for (const auto& v : *it)
            if (v.is_string())
                out.push_back(v.get<std::string>());
    return out;
}

std::string tail(std::string_view text, std::size_t keep = 4096) {
    return std::string(text.size() > keep ? text.substr(text.size() - keep) : text);
}

bool has_marker(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec))
        return false;
    std::ifstream in(file, std::ios::binary);
    constexpr std::string_view needle = "mtest-protocol=1";
    std::string carry;
    std::vector<char> buffer(1 << 20);
    while (in) {
        in.read(buffer.data(), std::streamsize(buffer.size()));
        const auto got = std::size_t(in.gcount());
        if (got == 0)
            break;
        std::string chunk = carry + std::string(buffer.data(), got);
        for (auto at = chunk.find(needle); at != std::string::npos;
             at = chunk.find(needle, at + 1)) {
            const auto next = at + needle.size();
            if (next < chunk.size() && !std::isdigit(static_cast<unsigned char>(chunk[next])))
                return true;
            if (next >= chunk.size() && !in)
                return true;
        }
        carry = chunk.size() > needle.size() ? chunk.substr(chunk.size() - needle.size()) : chunk;
    }
    return false;
}

// An ok image (`.oke`) is started by a loader: the engine's own when it is an image, else `okrun`.
std::vector<std::string> with_loader(std::vector<std::string> argv) {
    if (!argv.empty() && argv.front().ends_with(".oke")) {
        auto loader = os::env::loader();
        if (!loader)
            loader = os::env::find_executable("okrun");
        if (loader)
            argv.insert(argv.begin(), os::fs::utf8(*loader));
    }
    return argv;
}

std::filesystem::path resolve_program(const std::string& name) {
    std::filesystem::path p = os::fs::from_utf8(name);
    if (p.is_absolute() || name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos)
        return p;
    if (auto found = os::env::find_executable(name))
        return *found;
    return p;
}

}  // namespace

int detect_level(const program_spec& spec) {
    if (spec.level >= 0)
        return spec.level;
    // The program, or, for a runner and an image (`okrun app.oke`), the image: the first two words
    // of the command.
    for (std::size_t i = 0; i < spec.argv.size() && i < 2; ++i)
        if (has_marker(resolve_program(spec.argv[i])))
            return 3;
    return 0;
}

report_reading read_report(std::string_view text, std::string_view program) {
    report_reading r;
    std::map<std::string, std::size_t> index;
    auto at = [&](const std::string& id) -> case_result& {
        auto it = index.find(id);
        if (it != index.end())
            return r.cases[it->second];
        case_result c;
        c.program = std::string(program);
        c.id = id;
        c.kind.clear();  // the listing's, else its begin's, else the program's
        c.status = "";
        index[id] = r.cases.size();
        r.cases.push_back(std::move(c));
        return r.cases.back();
    };
    while (!text.empty()) {
        const auto nl = text.find('\n');
        std::string_view line = text.substr(0, nl);
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
        if (line.empty())
            continue;
        auto e = json::parse(line, nullptr, false);
        if (e.is_discarded() || !e.is_object()) {
            r.problems.push_back(std::format("not an event: {}", line.substr(0, 120)));
            continue;
        }
        const std::string kind = e.value("e", "");
        const std::string id = e.value("id", "");
        if (kind == "program") {
            r.level = e.value("level", 1);
        } else if (kind == "case") {
            r.listed.push_back(case_info_from(e));
        } else if (kind == "begin") {
            // A case not listed (P3.4) states what it is when it begins.
            auto& c = at(id);
            if (auto k = e.value("kind", std::string{}); !k.empty())
                c.kind = k;
            if (e.contains("tags"))
                c.tags = strings_of(e, "tags");
            if (e.contains("clauses"))
                c.clauses = strings_of(e, "clauses");
        } else if (kind == "failure") {
            at(id).failures.push_back({e.value("file", ""), e.value("message", ""),
                                       e.value("expected", ""), e.value("actual", ""),
                                       e.value("line", 0), e.value("fatal", false)});
        } else if (kind == "metric") {
            metric_info m;
            m.name = e.value("name", "");
            m.unit = e.value("unit", "");
            m.value = e.value("value", 0.0);
            m.better = e.value("better", std::string("lower"));
            if (auto s = e.find("samples"); s != e.end() && s->is_array())
                for (const auto& x : *s)
                    if (x.is_number())
                        m.samples.push_back(x.get<double>());
            at(id).metrics.push_back(std::move(m));
        } else if (kind == "artifact") {
            at(id).artifacts.push_back(e.value("path", ""));
        } else if (kind == "fixture") {
            at(id).fixtures.push_back({e.value("name", ""), e.value("ms", 0.0)});
        } else if (kind == "end") {
            auto& c = at(id);
            c.status = e.value("status", "fail");
            c.ms = e.value("ms", 0.0);
            c.reason = e.value("reason", "");
        } else if (kind == "missing") {
            auto& c = at(id);
            c.status = "fail";
            c.reason = "the program has no such case";
        } else if (kind == "done") {
            r.done = true;
        }
        // P5.4: any other kind is ignored.
    }
    return r;
}

std::expected<std::vector<case_info>, std::string> list_program(const program_spec& spec,
                                                                const std::filesystem::path& work,
                                                                const std::filesystem::path& root) {
    if (detect_level(spec) < 1) {
        case_info c;
        c.id = spec.name;
        c.name = spec.name;
        c.kind = spec.kind;
        c.tags = spec.tags;
        c.resources = spec.resources;
        return std::vector<case_info>{c};
    }
    std::filesystem::create_directories(work);
    const auto report = work / "list.ndjson";
    std::error_code ec;
    std::filesystem::remove(report, ec);
    const auto argv = with_loader(spec.argv);
    os::process::spawn_options o;
    o.program = resolve_program(argv.front());
    o.args.assign(argv.begin() + 1, argv.end());
    o.cwd = spec.cwd.empty() ? root : (root / os::fs::from_utf8(spec.cwd));
    o.in = os::process::stdio::null;
    for (const auto& [k, v] : spec.env)
        o.env.emplace_back(k, v);
    o.env.emplace_back("MTEST_PROTOCOL", "1");
    o.env.emplace_back("MTEST_MODE", "list");
    o.env.emplace_back("MTEST_REPORT", os::fs::utf8(report));
    o.env.emplace_back("MTEST_SELECT", std::nullopt);
    os::process::bounds limits;
    limits.hard = std::chrono::seconds{60};
    auto done = os::process::run(std::move(o), limits);
    if (!done)
        return std::unexpected(
            std::format("{} did not start: {}", spec.argv.front(), done.error().message()));
    auto text = os::fs::read(report);
    auto r = read_report(text ? std::string_view(*text) : std::string_view{}, spec.name);
    if (r.level == 0) {
        // P2.4: no `program` event -- an L0 program, which has just run its tests once.
        case_info c;
        c.id = spec.name;
        c.name = spec.name;
        return std::vector<case_info>{c};
    }
    return r.listed;
}

run_outcome run_program(const program_spec& spec, const run_request& request) {
    run_outcome out;
    auto& p = out.program;
    p.name = spec.name;
    p.argv = spec.argv;
    p.cwd = spec.cwd;
    p.origin = spec.origin;
    p.level = detect_level(spec);
    std::filesystem::create_directories(request.work);
    const auto report = request.work / "report.ndjson";
    std::error_code ec;
    std::filesystem::remove(report, ec);

    std::vector<std::string> command = request.run_under;
    const auto argv = with_loader(spec.argv);
    command.insert(command.end(), argv.begin(), argv.end());
    os::process::spawn_options o;
    o.program = resolve_program(command.front());
    o.args.assign(command.begin() + 1, command.end());
    o.cwd = spec.cwd.empty() ? request.root : (request.root / os::fs::from_utf8(spec.cwd));
    o.in = os::process::stdio::null;
    for (const auto& [k, v] : spec.env)
        o.env.emplace_back(k, v);
    for (const auto& [k, v] : request.env)
        o.env.emplace_back(k, v);
    if (p.level >= 1) {
        o.env.emplace_back("MTEST_PROTOCOL", "1");
        o.env.emplace_back("MTEST_MODE", "run");
        o.env.emplace_back("MTEST_REPORT", os::fs::utf8(report));
        o.env.emplace_back("MTEST_ISOLATION", spec.isolation);
        if (!request.select.empty()) {
            std::string lines;
            for (const auto& id : request.select)
                lines += id + "\n";
            (void)os::fs::write(request.work / "select.txt", lines);
            o.env.emplace_back("MTEST_SELECT", os::fs::utf8(request.work / "select.txt"));
        } else {
            o.env.emplace_back("MTEST_SELECT", std::nullopt);
        }
    }
    std::filesystem::create_directories(request.work / "tmp");
    o.env.emplace_back("MTEST_TMPDIR", os::fs::utf8(request.work / "tmp"));
    if (request.seed)
        o.env.emplace_back("MTEST_SEED", std::to_string(*request.seed));
    os::process::bounds limits;
    limits.hard = std::chrono::duration_cast<std::chrono::milliseconds>(spec.timeout);
    const auto began = std::chrono::steady_clock::now();
    auto done = os::process::run(std::move(o), limits);
    p.ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    std::filesystem::remove_all(request.work / "tmp", ec);
    if (!done) {
        // The program did not start: the machine's failure, not the test's.
        p.status = "infra";
        p.error = std::format("{} did not start: {}", command.front(), done.error().message());
        case_result c;
        c.program = spec.name;
        c.id = spec.name;
        c.status = "infra";
        c.reason = p.error;
        out.cases.push_back(std::move(c));
        return out;
    }
    p.exit = done->status.shell_code();
    p.timed_out = done->timed_out;
    p.out_tail = tail(done->out);
    p.err_tail = tail(done->err);
    out.out = std::move(done->out);
    out.err = std::move(done->err);

    if (p.level < 1) {
        case_result c;
        c.program = spec.name;
        c.id = spec.name;
        c.ms = p.ms;
        c.tags = spec.tags;
        c.status = p.timed_out ? "timeout" : p.exit == 0 ? "pass" : "fail";
        if (c.status != "pass")
            c.reason = p.timed_out ? std::format("still running after {}", spec.timeout)
                                   : std::format("exit {}", p.exit);
        out.cases.push_back(std::move(c));
    } else {
        auto text = os::fs::read(report);
        auto r = read_report(text ? std::string_view(*text) : std::string_view{}, spec.name);
        if (r.level == 0) {
            // It was taken for a protocol program and did not speak it: judged by its exit status
            // (P1.1).
            p.level = 0;
            case_result c;
            c.program = spec.name;
            c.id = spec.name;
            c.ms = p.ms;
            c.status = p.timed_out ? "timeout" : p.exit == 0 ? "pass" : "fail";
            if (c.status != "pass")
                c.reason = std::format("exit {}", p.exit);
            out.cases.push_back(std::move(c));
        } else {
            for (auto& c : r.cases) {
                if (c.status.empty()) {
                    // P5.4: begun and never ended -- the program ended (or was ended) first.
                    c.status = p.timed_out ? "timeout" : "fail";
                    c.reason = p.timed_out
                                   ? "the program was still running at its timeout"
                                   : std::format("the program ended (exit {}) before the case did",
                                                 p.exit);
                }
            }
            out.cases = std::move(r.cases);
            const bool case_failed = std::ranges::any_of(out.cases, [](const case_result& c) {
                return failing(c.status);
            });
            if ((p.exit != 0 || p.timed_out) && !case_failed) {
                // P5.5: a status no case explains is a failure of its own.
                case_result c;
                c.program = spec.name;
                c.id = spec.name;
                c.ms = p.ms;
                c.status = p.timed_out ? "timeout" : "fail";
                c.reason = p.timed_out ? "the program was still running at its timeout"
                                       : std::format("exit {} with no failing case{}", p.exit,
                                                     r.done ? "" : ", and no `done' event");
                out.cases.push_back(std::move(c));
            }
        }
    }
    const bool any_failing =
        std::ranges::any_of(out.cases, [](const case_result& c) { return failing(c.status); });
    p.status = p.timed_out ? "timeout" : any_failing ? "fail" : "pass";
    return out;
}

}  // namespace mcpplibs::mtest::engine
