module mcpplibs.mtest.engine.dag;

import std;

namespace mcpplibs::mtest::engine {

namespace {

// A label mermaid reads as text: no quote, colon, hash or bracket of its own syntax.
std::string plain(std::string_view s) {
    std::string out;
    for (char c : s)
        out += (c == '"' || c == ':' || c == '#' || c == '[' || c == ']' || c == '{' || c == '}' ||
                c == ';' || c == '|' || c == '<' || c == '>')
                   ? ' '
                   : c;
    return out;
}

std::string seconds(double ms) {
    return ms >= 1000 ? std::format("{:.1f} s", ms / 1000) : std::format("{:.0f} ms", ms);
}

void add_edge(run_dag& g, std::size_t from, std::size_t to, std::string why) {
    if (from == to)
        return;
    for (const auto& e : g.edges)
        if (e.from == from && e.to == to)
            return;
    g.edges.push_back({from, to, std::move(why), false});
}

// The edges of the nodes in `lane`, ordered by their start: each waited for the one before it.
void chain(run_dag& g, std::vector<std::size_t> lane, const std::string& why) {
    std::ranges::sort(lane, {}, [&](std::size_t i) { return g.nodes[i].start_ms; });
    for (std::size_t i = 1; i < lane.size(); ++i)
        add_edge(g, lane[i - 1], lane[i], why);
}

// The last node of `lane` that ended before the machine's holder `m` began, and the first that
// began after it ended: the waits holding the machine imposes on one worker.
void around_machine(run_dag& g, std::size_t m, const std::vector<std::size_t>& lane) {
    std::optional<std::size_t> before, after;
    for (auto i : lane) {
        if (i == m)
            continue;
        if (g.nodes[i].end_ms <= g.nodes[m].start_ms + 1 &&
            (!before || g.nodes[i].end_ms > g.nodes[*before].end_ms))
            before = i;
        if (g.nodes[i].start_ms + 1 >= g.nodes[m].end_ms &&
            (!after || g.nodes[i].start_ms < g.nodes[*after].start_ms))
            after = i;
    }
    if (before)
        add_edge(g, *before, m, "machine");
    if (after)
        add_edge(g, m, *after, "machine");
}

}  // namespace

run_dag dag_of(const run_record& r) {
    run_dag g;
    g.wall_ms = r.ms;
    std::map<std::string, std::size_t> builds;
    std::map<std::string, std::size_t> fixture_start, fixture_end;
    for (const auto& f : r.fixtures) {
        fixture_start[f.name] = g.nodes.size();
        g.nodes.push_back({"fixture:" + f.name,
                           "fixture",
                           "serve " + f.name,
                           f.program,
                           f.started_ms,
                           std::max(f.ready_ms, f.started_ms),
                           -1,
                           {},
                           0,
                           false});
        fixture_end[f.name] = g.nodes.size();
        g.nodes.push_back({"fixture-end:" + f.name,
                           "fixture",
                           "end " + f.name,
                           f.program,
                           f.ended_ms,
                           f.ended_ms,
                           -1,
                           {},
                           0,
                           false});
    }
    std::map<int, std::vector<std::size_t>> by_worker;
    std::map<std::string, std::vector<std::size_t>> by_resource;
    std::map<std::string, int> seen;
    std::vector<std::size_t> machine;
    for (const auto& p : r.programs) {
        if (p.build_started_ms >= 0 && !builds.contains(p.name)) {
            builds[p.name] = g.nodes.size();
            g.nodes.push_back({"build:" + p.name,
                               "build",
                               "build " + p.name,
                               p.name,
                               p.build_started_ms,
                               p.build_started_ms + p.build_ms,
                               -1,
                               {},
                               0,
                               false});
        }
        const auto n = ++seen[p.name];
        const auto cases = std::ranges::count(r.cases, p.name, &case_result::program);
        const std::size_t here = g.nodes.size();
        dag_node node{std::format("run:{}#{}", p.name, n),
                      "run",
                      p.select.empty() ? p.name
                      : p.select.size() <= 2
                          ? std::format("{}: {}", p.name,
                                        [&] {
                                            std::string s;
                                            for (const auto& id : p.select)
                                                s += (s.empty() ? "" : ", ") + id;
                                            return s;
                                        }())
                          : std::format("{} ({} of its cases)", p.name, p.select.size()),
                      p.name,
                      p.started_ms,
                      p.started_ms + p.ms,
                      p.worker,
                      p.resources,
                      p.select.empty() ? std::size_t(cases) : p.select.size(),
                      false};
        g.nodes.push_back(std::move(node));
        if (auto b = builds.find(p.name); b != builds.end())
            add_edge(g, b->second, here, "build");
        for (const auto& f : p.fixtures) {
            if (auto s = fixture_start.find(f); s != fixture_start.end())
                add_edge(g, s->second, here, "fixture");
            if (auto e = fixture_end.find(f); e != fixture_end.end())
                add_edge(g, here, e->second, "fixture");
        }
        if (p.worker >= 0)
            by_worker[p.worker].push_back(here);
        for (const auto& res : p.resources) {
            if (res == "machine")
                machine.push_back(here);
            else if (res != "network")
                by_resource[res].push_back(here);
        }
    }
    for (auto& [w, lane] : by_worker)
        chain(g, lane, "worker");
    for (auto& [res, lane] : by_resource)
        chain(g, lane, "resource:" + res);
    // A process that holds the machine waited for the last process of each worker before it, and
    // the first process of each worker after it waited for it.
    for (auto m : machine)
        for (auto& [w, lane] : by_worker)
            around_machine(g, m, lane);

    // THE CRITICAL PATH OF THE EXECUTED SCHEDULE: from the node that ended last, each step back is
    // the predecessor that ended last before the node began -- the wait that bound it.
    if (g.nodes.empty())
        return g;
    std::size_t last = 0;
    for (std::size_t i = 1; i < g.nodes.size(); ++i)
        if (g.nodes[i].end_ms > g.nodes[last].end_ms)
            last = i;
    std::vector<std::vector<std::size_t>> preds(g.nodes.size());
    for (std::size_t e = 0; e < g.edges.size(); ++e)
        preds[g.edges[e].to].push_back(e);
    std::vector<std::size_t> path{last};
    std::set<std::size_t> visited{last};
    for (std::size_t at = last;;) {
        std::optional<std::size_t> bound;
        for (auto e : preds[at]) {
            const auto from = g.edges[e].from;
            if (visited.contains(from) || g.nodes[from].end_ms > g.nodes[at].start_ms + 1)
                continue;
            if (!bound || g.nodes[from].end_ms > g.nodes[g.edges[*bound].from].end_ms)
                bound = e;
        }
        if (!bound)
            break;
        g.edges[*bound].critical = true;
        at = g.edges[*bound].from;
        visited.insert(at);
        path.push_back(at);
    }
    std::ranges::reverse(path);
    for (auto i : path) {
        g.nodes[i].critical = true;
        g.path_ms += g.nodes[i].ms();
    }
    g.path = std::move(path);
    return g;
}

json critical_path(const run_record& r) {
    const auto g = dag_of(r);
    json steps = json::array();
    std::optional<std::size_t> previous;
    for (auto i : g.path) {
        const auto& n = g.nodes[i];
        std::string why;
        if (previous)
            for (const auto& e : g.edges)
                if (e.from == *previous && e.to == i)
                    why = e.why;
        steps.push_back({{"node", n.id},
                         {"label", n.label},
                         {"kind", n.kind},
                         {"start_ms", n.start_ms},
                         {"end_ms", n.end_ms},
                         {"ms", n.ms()},
                         {"waited_for", why},
                         {"worker", n.worker}});
        previous = i;
    }
    const double waits = g.path.empty() ? 0
                                        : g.nodes[g.path.back()].end_ms -
                                              g.nodes[g.path.front()].start_ms - g.path_ms;
    std::string conclusion;
    if (g.path.empty()) {
        conclusion = "the run has no work to draw";
    } else {
        std::size_t longest = g.path.front();
        for (auto i : g.path)
            if (g.nodes[i].ms() > g.nodes[longest].ms())
                longest = i;
        conclusion = std::format("the critical path has {} node(s) and {} of work over a wall "
                                 "clock of {}; its longest "
                                 "member is {} ({}); {} of it is waiting between its members",
                                 g.path.size(), seconds(g.path_ms), seconds(g.wall_ms),
                                 g.nodes[longest].label, seconds(g.nodes[longest].ms()),
                                 seconds(std::max(0.0, waits)));
    }
    return {{"run", r.id},
            {"unit", "ms"},
            {"wall", g.wall_ms},
            {"path_ms", g.path_ms},
            {"nodes", g.nodes.size()},
            {"edges", g.edges.size()},
            {"path", steps},
            {"conclusion", conclusion}};
}

namespace {

// The nodes a drawing keeps: the critical path's, then the longest others, up to `max_nodes`.
std::vector<bool> kept(const run_dag& g, int max_nodes) {
    std::vector<bool> keep(g.nodes.size(), false);
    int n = 0;
    for (auto i : g.path)
        if (!keep[i]) {
            keep[i] = true;
            ++n;
        }
    std::vector<std::size_t> order(g.nodes.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::sort(order, std::greater{}, [&](std::size_t i) { return g.nodes[i].ms(); });
    for (auto i : order) {
        if (n >= max_nodes)
            break;
        if (!keep[i]) {
            keep[i] = true;
            ++n;
        }
    }
    return keep;
}

}  // namespace

std::string mermaid_graph(const run_dag& g, int max_nodes) {
    const auto keep = kept(g, max_nodes);
    std::string out = "flowchart LR\n";
    for (std::size_t i = 0; i < g.nodes.size(); ++i)
        if (keep[i])
            out += std::format("    n{}[\"{}<br/>{}\"]\n", i, plain(g.nodes[i].label),
                               seconds(g.nodes[i].ms()));
    std::vector<std::size_t> critical_links;
    std::size_t link = 0;
    for (const auto& e : g.edges) {
        if (!keep[e.from] || !keep[e.to])
            continue;
        out += std::format("    n{} -->|{}| n{}\n", e.from, plain(e.why), e.to);
        if (e.critical)
            critical_links.push_back(link);
        ++link;
    }
    std::string crit;
    for (auto i : g.path)
        crit += std::format("{}n{}", crit.empty() ? "" : ",", i);
    out += "    classDef crit fill:#fdd,stroke:#c00,stroke-width:2px\n";
    if (!crit.empty())
        out += "    class " + crit + " crit\n";
    for (auto l : critical_links)
        out += std::format("    linkStyle {} stroke:#c00,stroke-width:3px\n", l);
    return out;
}

std::string mermaid_gantt(const run_dag& g, int max_nodes) {
    const auto keep = kept(g, max_nodes);
    std::string out = "gantt\n    dateFormat x\n    axisFormat %M:%S\n";
    std::map<int, std::vector<std::size_t>> lanes;
    for (std::size_t i = 0; i < g.nodes.size(); ++i)
        if (keep[i] && g.nodes[i].ms() > 0)
            lanes[g.nodes[i].kind == "run" ? g.nodes[i].worker : -1 - (g.nodes[i].kind == "build")]
                .push_back(i);
    for (auto& [lane, nodes] : lanes) {
        out += lane >= 0    ? std::format("    section worker {}\n", lane)
               : lane == -2 ? std::string("    section builds\n")
                            : std::string("    section fixtures\n");
        std::ranges::sort(nodes, {}, [&](std::size_t i) { return g.nodes[i].start_ms; });
        for (auto i : nodes) {
            const auto& n = g.nodes[i];
            out += std::format("    {} :{}n{}, {:.0f}, {:.0f}\n", plain(n.label),
                               n.critical ? "crit, " : "", i, n.start_ms, n.end_ms);
        }
    }
    return out;
}

}  // namespace mcpplibs::mtest::engine
