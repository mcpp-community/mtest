module mcpplibs.mtest.engine.cicheck;

import std;
import mcpplibs.os;
import mcpplibs.mtest.engine.capability;

namespace mcpplibs::mtest::engine {

namespace os = mcpplibs::os;

namespace {

const std::set<std::string, std::less<>> kRan = {"pass",  "fail",  "timeout", "xfail",
                                                 "xpass", "flaky", "invalid"};

struct row {
    std::string name;
    std::vector<std::string> cols;
};

// A tab-separated table; `#` begins a comment line.
std::expected<std::vector<row>, std::string> table(const config& c, const std::string& file) {
    std::vector<row> out;
    if (file.empty())
        return out;
    const auto path = c.root / os::fs::from_utf8(file);
    auto text = os::fs::read(path);
    if (!text)
        return std::unexpected(
            std::format("cannot read {}: {}", os::fs::utf8(path), text.error().message()));
    std::istringstream in(*text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.starts_with('#'))
            continue;
        row r;
        std::size_t at = 0;
        bool first = true;
        for (;;) {
            const auto tab = line.find('\t', at);
            auto cell = line.substr(at, tab == std::string::npos ? std::string::npos : tab - at);
            if (first)
                r.name = cell;
            else
                r.cols.push_back(cell);
            first = false;
            if (tab == std::string::npos)
                break;
            at = tab + 1;
        }
        out.push_back(std::move(r));
    }
    return out;
}

// The program a `program` or `program::id` entry names.
std::string program_of(std::string_view entry) {
    const auto sep = entry.find("::");
    return std::string(sep == std::string_view::npos ? entry : entry.substr(0, sep));
}

bool narrowed(const config& c, const run_record& r) {
    if (r.set.empty())
        return !r.selection.empty();
    auto it = c.sets.find(r.set);
    return it == c.sets.end() || !it->second.all;
}

std::vector<std::string> words_of(const config& c, const std::vector<std::string>& tags) {
    std::vector<std::string> out;
    for (const auto& t : tags)
        if (auto w = capability_of_tag(c, t))
            out.push_back(*w);
    return out;
}

// The architectures among the words of `only` and `except`; the rest are platforms.
bool is_architecture(std::string_view w) {
    return w == "x86_64" || w == "aarch64" || w == "riscv64" || w == "wasm32";
}

// Whether a run on `system`, `arch` is one of the places `only` names (0.8.0).
bool covers(const std::vector<std::string>& only, std::string_view system, std::string_view arch) {
    bool any_platform = false, any_arch = false, platform_ok = false, arch_ok = false;
    for (const auto& w : only) {
        if (is_architecture(w)) {
            any_arch = true;
            arch_ok = arch_ok || w == arch;
        } else {
            any_platform = true;
            platform_ok = platform_ok || w == system;
        }
    }
    return (!any_platform || platform_ok) && (!any_arch || arch_ok);
}

// A case stated to run only on some places must pass on each of them that a run covered, and on
// at least one: what [ci.coverage] hosts states of a program, stated by the case itself.
std::vector<std::string> placed_problems(const std::vector<run_record>& runs,
                                         const std::function<bool(const run_record&)>& narrowed) {
    std::map<std::string, std::vector<std::string>> places;  // program::case -> its `only`
    for (const auto& r : runs)
        for (const auto& cr : r.cases)
            if (!cr.only.empty())
                places.try_emplace(cr.program + "::" + cr.id, cr.only);
    std::vector<std::string> out;
    for (const auto& [key, only] : places) {
        std::map<std::string, bool> passed;  // "<system> <arch>" covered -> passed there
        for (const auto& r : runs) {
            if (!covers(only, r.system, r.arch))
                continue;
            const auto at = std::format("{} {}", r.system, r.arch);
            const auto result = std::ranges::find_if(r.cases, [&](const case_result& cr) {
                return cr.program + "::" + cr.id == key;
            });
            if (result == r.cases.end() && narrowed(r))
                continue;  // absent by the run's selection
            const bool ok =
                result != r.cases.end() && (result->status == "pass" || result->status == "xfail" ||
                                            result->status == "flaky");
            passed[at] = passed[at] || ok;
        }
        std::string words;
        for (const auto& w : only)
            words += (words.empty() ? "" : ", ") + w;
        if (passed.empty())
            out.push_back(std::format("{} runs only on {} and no run was there", key, words));
        for (const auto& [at, ok] : passed)
            if (!ok)
                out.push_back(
                    std::format("{} runs only on {} and did not pass on {}", key, words, at));
    }
    return out;
}

}  // namespace

ci_answer ci_coverage(const config& c, const std::vector<program_spec>& programs,
                      const std::vector<run_record>& runs) {
    ci_answer a;
    auto exceptions = table(c, c.ci_coverage.exceptions);
    auto debt = table(c, c.ci_coverage.debt);
    auto hosts = table(c, c.ci_coverage.hosts);
    for (const auto* t : {&exceptions, &debt, &hosts})
        if (!*t)
            return {2, {{"conclusion", t->error()}}, t->error() + "\n"};

    std::set<std::string> known;
    for (const auto& p : programs)
        known.insert(p.name);
    std::map<std::string, std::set<std::string>> ran_on, passed_on, seen_on, skipped_why;
    bool any_narrowed = false;
    std::map<std::string, bool> word_somewhere;
    for (const auto& r : runs) {
        any_narrowed = any_narrowed || narrowed(c, r);
        const auto host = r.host.empty() ? r.system : r.host;
        for (const auto& [w, present] : r.capabilities)
            word_somewhere[w] = word_somewhere[w] || present;
        for (const auto& cr : r.cases) {
            seen_on[cr.program].insert(host);
            if (kRan.contains(cr.status))
                ran_on[cr.program].insert(host);
            if (cr.status == "pass" || cr.status == "xfail" || cr.status == "flaky")
                passed_on[cr.program].insert(host);
            if (cr.status == "skip" && !cr.reason.empty())
                skipped_why[cr.program].insert(cr.reason);
        }
    }
    std::map<std::string, std::string> excused, owed;
    std::vector<std::string> problems;
    for (const auto& e : *exceptions)
        excused[program_of(e.name)] = e.cols.empty() ? "" : e.cols.front();
    for (const auto& e : *debt)
        owed[program_of(e.name)] = e.cols.empty() ? "" : e.cols.front();
    for (const auto* list : {&excused, &owed})
        for (const auto& [name, _] : *list) {
            if (!known.contains(name))
                problems.push_back(
                    std::format("{} is listed in [ci.coverage] {} but no program has that name",
                                name, list == &excused ? "exceptions" : "debt"));
            else if (ran_on.contains(name))
                problems.push_back(
                    std::format("{} is listed in [ci.coverage] {} but ran on {}: remove the entry",
                                name, list == &excused ? "exceptions" : "debt",
                                *ran_on[name].begin()));
        }

    json judged = json::object();
    std::map<std::string, int> tally;
    std::vector<std::string> uncovered;
    for (const auto& p : programs) {
        std::string verdict;
        if (ran_on.contains(p.name))
            verdict = "ran";
        else if (excused.contains(p.name))
            verdict = "excused";
        else if (owed.contains(p.name))
            verdict = "owed";
        else if (!seen_on.contains(p.name) && any_narrowed)
            verdict = "unaffected";
        else {
            verdict = "uncovered";
            std::string why;
            if (auto s = skipped_why.find(p.name); s != skipped_why.end())
                why = *s->second.begin();
            else
                why = "no run selected it";
            uncovered.push_back(std::format("{} -- {}", p.name, why));
        }
        ++tally[verdict];
        judged[p.name] = verdict;
    }
    for (const auto& h : *hosts) {
        const auto name = program_of(h.name);
        if (!known.contains(name)) {
            problems.push_back(
                std::format("{} is listed in [ci.coverage] hosts but no program has that name",
                            name));
            continue;
        }
        std::istringstream words(h.cols.empty() ? std::string() : h.cols.front());
        for (std::string host; words >> host;) {
            if (passed_on[name].contains(host))
                continue;
            bool absent_by_selection = false;
            for (const auto& r : runs)
                if ((r.host.empty() ? r.system : r.host) == host && narrowed(c, r) &&
                    !seen_on[name].contains(host))
                    absent_by_selection = true;
            if (!absent_by_selection)
                problems.push_back(
                    std::format("{} must pass on {} ([ci.coverage] hosts) and did not", name,
                                host));
        }
    }
    for (auto& pr : placed_problems(runs, [&](const run_record& r) { return narrowed(c, r); }))
        problems.push_back(std::move(pr));
    std::vector<std::string> unprovided;
    for (const auto& [w, _] : c.capabilities.words)
        if (!word_somewhere[w])
            unprovided.push_back(w);

    a.status = uncovered.empty() && problems.empty() ? 0 : 1;
    a.data = {{"programs", programs.size()},
              {"runs", runs.size()},
              {"tally", tally},
              {"cases", judged},
              {"uncovered", uncovered},
              {"problems", problems},
              {"capabilities_no_run_provided", unprovided}};
    std::string t = std::format("{} program(s) over {} run(s):", programs.size(), runs.size());
    for (const auto& [k, n] : tally)
        t += std::format(" {} {}", n, k);
    t += "\n";
    for (const auto& u : uncovered)
        t += "UNCOVERED " + u + "\n";
    for (const auto& pr : problems)
        t += "PROBLEM " + pr + "\n";
    if (!unprovided.empty()) {
        t += "capabilities no run's machine provided:";
        for (const auto& w : unprovided)
            t += " " + w;
        t += "\n";
    }
    a.data["conclusion"] =
        a.status == 0
            ? std::string("every program ran somewhere or is accounted for")
            : std::format("{} uncovered, {} problem(s)", uncovered.size(), problems.size());
    a.text = t;
    return a;
}

ci_answer ci_plan(const config& c, const plan& selected, const std::vector<run_record>& runs,
                  const std::vector<std::string>& hosts_in, std::string_view format) {
    ci_answer a;
    const auto hosts = hosts_in.empty() ? c.ci_plan.hosts : hosts_in;
    if (hosts.empty())
        return {2,
                {{"conclusion", "ci plan needs --hosts or [ci.plan] hosts"}},
                "ci plan needs --hosts or [ci.plan] hosts\n"};
    // Each host's newest answers and the newest measured time of each case there (runs are oldest
    // first in the store's order; a later one overwrites).
    std::map<std::string, std::map<std::string, bool>> answers;
    std::map<std::string, std::map<std::string, double>> ms;
    for (const auto& r : runs) {
        const auto host = r.host.empty() ? r.system : r.host;
        if (!r.capabilities.empty())
            answers[host] = r.capabilities;
        for (const auto& cr : r.cases)
            if (kRan.contains(cr.status) || (cr.status == "skip" && cr.ms > 0))
                ms[host][cr.program + "::" + cr.id] = cr.ms;
    }
    json out = json::array();
    std::string text;
    std::string github;
    for (const auto& host : hosts) {
        std::size_t cases = 0, unmeasured = 0;
        double total = 0;
        const auto& ans = answers[host];
        for (const auto& u : selected.units)
            for (const auto& uc : u.cases) {
                auto tags = uc.info.tags;
                tags.insert(tags.end(), u.program.tags.begin(), u.program.tags.end());
                bool runs_here = true;
                if (!ans.empty())
                    for (const auto& w : words_of(c, tags))
                        if (auto it = ans.find(w); it == ans.end() || !it->second)
                            runs_here = false;
                if (!runs_here)
                    continue;
                ++cases;
                const auto key = u.program.name + "::" + uc.info.id;
                if (auto m = ms[host].find(key); m != ms[host].end())
                    total += m->second;
                else {
                    total += double(c.ci_plan.unknown.count());
                    ++unmeasured;
                }
            }
        const int shards = cases == 0
                               ? 0
                               : std::clamp(int(std::ceil(total / double(c.ci_plan.shard.count()))),
                                            1, c.ci_plan.max_shards);
        json indices = json::array();
        for (int i = 1; i <= shards; ++i)
            indices.push_back(i);
        out.push_back({{"host", host},
                       {"work", cases > 0},
                       {"cases", cases},
                       {"unmeasured", unmeasured},
                       {"expected_s", total / 1000},
                       {"shards", shards},
                       {"indices", indices},
                       {"history", !ans.empty()}});
        text += std::format("{}: {} case(s), {:.0f} s expected ({} unmeasured), {} shard(s){}\n",
                            host, cases, total / 1000, unmeasured, shards,
                            ans.empty() ? " -- no history: every case counted" : "");
        github += std::format("{}={}\n{}_work={}\n", host, indices.dump(), host,
                              cases > 0 ? "true" : "false");
    }
    if (format == "github") {
        if (auto f = os::env::get("GITHUB_OUTPUT"); f && !f->empty())
            (void)os::fs::append(os::fs::from_utf8(*f), github);
        text += github;
    }
    a.data = {{"hosts", out},
              {"conclusion", std::format("{} host(s) planned for {} selected case(s)", hosts.size(),
                                         selected.selected)}};
    a.text = text;
    return a;
}

}  // namespace mcpplibs::mtest::engine
