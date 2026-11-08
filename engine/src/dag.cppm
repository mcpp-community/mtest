// A run as a directed acyclic graph (mtest design 2026-11-09 §17.4): its nodes are the work of the
// run -- a program's build, a process of a program running its cases, a run fixture's serving --
// and its edges what made one wait for another: a build before the program it builds, a worker's
// previous process before its next, the previous holder of an exclusive resource, a fixture's
// serving before its users and after them its end. Weights are the measured times. The critical
// path is the graph's longest weighted path ending at the node that ended last: the chain of work
// no schedule of these nodes on these workers shortens without shortening one of its members.
export module mcpplibs.mtest.engine.dag;

import std;
export import mcpplibs.mtest.engine.model;

export namespace mcpplibs::mtest::engine {

struct dag_node {
    std::string id;     // "build:<program>", "run:<program>#<n>", "fixture:<name>"
    std::string kind;   // build, run, fixture
    std::string label;  // what a reader is shown
    std::string program;
    double start_ms = 0, end_ms = 0;
    int worker = -1;
    std::vector<std::string> resources;
    std::size_t cases = 0;
    bool critical = false;
    double ms() const {
        return end_ms - start_ms;
    }
};

struct dag_edge {
    std::size_t from = 0, to = 0;
    std::string why;  // build, worker, resource:<name>, machine, fixture
    bool critical = false;
};

struct run_dag {
    std::vector<dag_node> nodes;
    std::vector<dag_edge> edges;
    std::vector<std::size_t> path;  // the critical path, first to last
    double path_ms = 0;             // its work, without the waits between its nodes
    double wall_ms = 0;
};

// The graph of a run, with its critical path marked.
run_dag dag_of(const run_record& r);
// The critical path as data with a conclusion (`mtest analyze path`).
json critical_path(const run_record& r);
// mermaid: the graph (`flowchart LR`, the critical path marked, at most `max_nodes` nodes: the
// critical path's and the longest others), and the schedule (`gantt`, a section per worker).
std::string mermaid_graph(const run_dag& g, int max_nodes);
std::string mermaid_gantt(const run_dag& g, int max_nodes);

}  // namespace mcpplibs::mtest::engine
