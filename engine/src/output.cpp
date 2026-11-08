module mcpplibs.mtest.engine.output;

import std;
import mcpplibs.mtest.engine.select;
import mcpplibs.os;

namespace mcpplibs::mtest::engine {

namespace {

std::string xml(std::string_view t) {
    std::string out;
    for (char ch : t) {
        switch (ch) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20 && ch != '\n' && ch != '\t')
                    out += ' ';
                else
                    out += ch;
        }
    }
    return out;
}

std::string shell_quote(std::string_view t) {
    if (!t.empty() && t.find_first_of(" \t'\"\\$`*?[]{}()<>|&;!#~") == std::string_view::npos)
        return std::string(t);
    std::string out = "'";
    for (char ch : t)
        out += ch == '\'' ? std::string("'\\''") : std::string(1, ch);
    return out + "'";
}

}  // namespace

std::string reproduce_command(std::string_view program, std::string_view id) {
    return std::format("mtest run --program {} --case {}", shell_quote(program), shell_quote(id));
}

std::string reproduce_command(const program_result& program, std::string_view id) {
    if (program.origin != "given")
        return reproduce_command(program.name, id);
    std::string cmd = program.level >= 1 && id != program.name
                          ? std::format("mtest run --case {} --", shell_quote(id))
                          : std::string("mtest run --");
    for (const auto& a : program.argv)
        cmd += " " + shell_quote(a);
    return cmd;
}

std::string case_label(std::string_view program, std::string_view id) {
    return program == id ? std::string(program) : std::format("{}::{}", program, id);
}

std::string summary_text(const run_record& r, bool verbose) {
    std::string out;
    auto again = [&](const case_result& c) {
        for (const auto& p : r.programs)
            if (p.name == c.program)
                return reproduce_command(p, c.id);
        return reproduce_command(c.program, c.id);
    };
    auto n = [&](const char* k) {
        auto it = r.counts.find(k);
        return it == r.counts.end() ? 0 : it->second;
    };
    for (const auto& c : r.cases) {
        if (!failing(c.status) && c.status != "infra" && !verbose)
            continue;
        out += std::format("{} {} ({:.0f} ms){}{}\n",
                           failing(c.status)     ? (c.quarantined ? "QUAR" : "FAIL")
                           : c.status == "infra" ? "INFR"
                                                 : "  ok",
                           case_label(c.program, c.id), c.ms, c.reason.empty() ? "" : " -- ",
                           c.reason);
        for (const auto& f : c.failures) {
            out += std::format("    {}:{}: {}\n", f.file, f.line, f.message);
            if (!f.expected.empty() || !f.actual.empty())
                out += std::format("      expected {}\n      actual   {}\n", f.expected, f.actual);
        }
        if (failing(c.status))
            out += std::format("    again: {}\n", again(c));
    }
    out +=
        std::format("run {} {}: {} passed, {} failed, {} skipped{}{}{} in {:.1f} s ({} program{}, "
                    "{} job{})\n",
                    r.id, r.status, n("pass") + n("xfail"), n("fail") + n("xpass") + n("timeout"),
                    n("skip"), n("flaky") ? std::format(", {} flaky", n("flaky")) : "",
                    n("infra") ? std::format(", {} not run (the machine's)", n("infra")) : "",
                    n("invalid") ? std::format(", {} invalid", n("invalid")) : "", r.ms / 1000,
                    r.programs.size(), r.programs.size() == 1 ? "" : "s", r.jobs,
                    r.jobs == 1 ? "" : "s");
    return out;
}

std::string junit_xml(const run_record& r) {
    std::map<std::string, std::vector<const case_result*>> by_program;
    for (const auto& c : r.cases)
        by_program[c.program].push_back(&c);
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out += std::format("<testsuites name=\"{}\" tests=\"{}\" time=\"{:.3f}\">\n", xml(r.id),
                       r.cases.size(), r.ms / 1000);
    for (const auto& [program, cases] : by_program) {
        int failures = 0, skipped = 0;
        double t = 0;
        for (auto* c : cases) {
            failures += failing(c->status);
            skipped += c->status == "skip";
            t += c->ms;
        }
        out += std::format("  <testsuite name=\"{}\" tests=\"{}\" failures=\"{}\" skipped=\"{}\" "
                           "time=\"{:.3f}\">\n",
                           xml(program), cases.size(), failures, skipped, t / 1000);
        for (auto* c : cases) {
            out += std::format("    <testcase classname=\"{}\" name=\"{}\" time=\"{:.3f}\"",
                               xml(program), xml(c->id), c->ms / 1000);
            if (failing(c->status)) {
                std::string body;
                for (const auto& f : c->failures)
                    body += std::format("{}:{}: {}\n", f.file, f.line, f.message);
                out +=
                    std::format(">\n      <failure message=\"{}\">{}</failure>\n    </testcase>\n",
                                xml(c->reason.empty() && !c->failures.empty()
                                        ? c->failures.front().message
                                        : c->reason),
                                xml(body));
            } else if (c->status == "skip") {
                out += std::format(">\n      <skipped message=\"{}\"/>\n    </testcase>\n",
                                   xml(c->reason));
            } else {
                out += "/>\n";
            }
        }
        out += "  </testsuite>\n";
    }
    return out + "</testsuites>\n";
}

std::string html_page(const run_record& r) {
    std::string rows;
    for (const auto& c : r.cases) {
        std::string detail;
        for (const auto& f : c.failures)
            detail += std::format("{}:{}: {}<br>", xml(f.file), f.line, xml(f.message));
        rows +=
            std::format("<tr "
                        "class=\"{}\"><td>{}</td><td>{}</td><td>{}</td><td>{:.0f}</td><td>{}{}</"
                        "td></tr>\n",
                        failing(c.status) ? "fail" : "ok", xml(c.program), xml(c.id), xml(c.status),
                        c.ms, xml(c.reason), detail.empty() ? "" : "<br>" + detail);
    }
    return std::format(R"(<!doctype html>
<html><head><meta charset="utf-8"><title>mtest run {0}</title>
<style>body{{font:14px system-ui,sans-serif;margin:2em}}table{{border-collapse:collapse;width:100%}}td,th{{border-bottom:1px solid #ddd;padding:4px 8px;text-align:left;vertical-align:top}}tr.fail td{{background:#fde8e8}}</style>
</head><body>
<h1>mtest run {0}: {1}</h1>
<p>{2} cases in {3:.1f} s on {4} ({5}), commit {6}</p>
<table><tr><th>program</th><th>case</th><th>status</th><th>ms</th><th>detail</th></tr>
{7}</table></body></html>
)",
                       xml(r.id), xml(r.status), r.cases.size(), r.ms / 1000, xml(r.system),
                       xml(r.arch), xml(r.commit), rows);
}

std::expected<json, std::string> explain(const store& s, const run_record& r,
                                         std::string_view case_ref) {
    std::vector<const case_result*> matches;
    for (const auto& c : r.cases) {
        const auto k = c.program + "::" + c.id;
        if (k == case_ref || c.id == case_ref || glob_match(case_ref, k) ||
            glob_match(case_ref, c.id))
            matches.push_back(&c);
    }
    if (matches.empty())
        return std::unexpected(std::format("run {} has no case {}", r.id, case_ref));
    json items = json::array();
    for (const auto* c : matches) {
        json where = json::array(), why = json::array();
        for (const auto& f : c->failures) {
            where.push_back(std::format("{}:{}", f.file, f.line));
            json w = {{"message", f.message}};
            if (!f.expected.empty() || !f.actual.empty()) {
                w["expected"] = f.expected;
                w["actual"] = f.actual;
            }
            why.push_back(std::move(w));
        }
        if (why.empty() && !c->reason.empty())
            why.push_back({{"message", c->reason}});
        std::string again = reproduce_command(c->program, c->id);
        for (const auto& p : r.programs)
            if (p.name == c->program)
                again = reproduce_command(p, c->id);
        json item = {{"program", c->program}, {"case", c->id},          {"status", c->status},
                     {"ms", c->ms},           {"where", where},         {"why", why},
                     {"again", again},        {"attempts", c->attempts}};
        if (auto outputs = failure_outputs(s, r.id, c->program)) {
            auto tail = [](const std::string& t) {
                return t.size() > 2000 ? t.substr(t.size() - 2000) : t;
            };
            item["output"] = {{"out_tail", tail(outputs->first)},
                              {"err_tail", tail(outputs->second)},
                              {"kept",
                               mcpplibs::os::fs::generic_utf8(run_dir(s, r.id) / "failures")}};
        }
        for (const auto& p : r.programs)
            if (p.name == c->program)
                item["launch"] = {{"argv", p.argv},
                                  {"cwd", p.cwd},
                                  {"level", p.level},
                                  {"exit", p.exit}};
        // The case's recent history: a new failure, one that keeps failing, or one that changes.
        json recent = json::array();
        auto all = runs(s);
        int seen = 0, fails = 0;
        for (std::size_t i = all.size(); i-- > 0 && seen < 10;) {
            if (all[i].id == r.id)
                continue;
            auto other = load_run(s, all[i].id);
            if (!other)
                continue;
            for (const auto& oc : other->cases)
                if (oc.program == c->program && oc.id == c->id) {
                    ++seen;
                    fails += failing(oc.status);
                    recent.push_back({{"run", other->id}, {"status", oc.status}});
                }
        }
        item["recent"] = recent;
        if (!c->diagnosis.empty())
            item["diagnosis"] = c->diagnosis;  // the output of its diagnostic run
        if (!c->fixtures.empty())
            item["fixtures"] = to_json(*c)["fixtures"];
        item["kind_of_failure"] = !failing(c->status) ? "not failing"
                                  : seen == 0         ? "first seen"
                                  : fails == 0        ? "new"
                                  : fails == seen     ? "keeps failing"
                                                      : "intermittent";
        items.push_back(std::move(item));
    }
    const auto& first = items.front();
    std::string conclusion = std::format("{} {}",
                                         case_label(first["program"].get<std::string>(),
                                                    first["case"].get<std::string>()),
                                         first["status"].get<std::string>());
    if (!first["where"].empty())
        conclusion += " at " + first["where"][0].get<std::string>();
    if (!first["why"].empty())
        conclusion += ": " + first["why"][0]["message"].get<std::string>();
    conclusion += std::format(" ({}); again: {}", first["kind_of_failure"].get<std::string>(),
                              first["again"].get<std::string>());
    return json{{"run", r.id}, {"cases", items}, {"conclusion", conclusion}};
}

}  // namespace mcpplibs::mtest::engine
