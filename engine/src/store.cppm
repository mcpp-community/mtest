// The store of runs (mtest design M5): one directory per run holding NDJSON, and an index of runs.
// Local and CI stores have one format, so a CI run's store is an artifact that `mtest store import`
// takes in.
//
//   <store>/index.ndjson                 one line per run: its run.json
//   <store>/runs/<id>/run.json           the run's record (model.cppm)
//   <store>/runs/<id>/cases.ndjson       one line per case result
//   <store>/runs/<id>/programs.ndjson    one line per program result
//   <store>/runs/<id>/fixtures.ndjson    one line per run fixture served (P11), when any
//   <store>/runs/<id>/failures/<n>/      a failing program's whole outputs (out.txt, err.txt) and
//   its launch <store>/runs/<id>/progress.json      while the run runs: done and total, for `mtest
//   status` <store>/runs/<id>/cancel             asks the running run to stop (`mtest cancel`)
//   <store>/quarantine.json              cases whose failures do not fail a run, each until a date
export module mcpplibs.mtest.engine.store;

import std;
export import mcpplibs.mtest.engine.model;

export namespace mcpplibs::mtest::engine {

struct store {
    std::filesystem::path dir;
};

// The store of a project: created when absent, with a `.gitignore` of `*` so that it ignores
// itself.
store open_store(const std::filesystem::path& dir);

std::filesystem::path run_dir(const store& s, std::string_view id);

// Writes a finished run, and its line of the index.
void write_run(const store& s, const run_record& r);
// Keeps the whole outputs of a failing program of run `id`, the n-th of the run.
void write_failure(const store& s, std::string_view id, std::size_t n, const program_result& p,
                   std::string_view out, std::string_view err);

// Every run of the index, oldest first, without its programs and cases.
std::vector<run_record> runs(const store& s);
// A run with its programs and cases; `id` may also be `last`, or `last-<n>` (n runs before the
// last).
std::optional<run_record> load_run(const store& s, std::string_view id);
// The whole outputs kept for a program of a run.
std::optional<std::pair<std::string, std::string>>
failure_outputs(const store& s, std::string_view id, std::string_view program);

// Progress of a running run, and the request to cancel it.
void write_progress(const store& s, std::string_view id, const json& progress);
std::vector<std::pair<std::string, json>> active_runs(const store& s);
void request_cancel(const store& s, std::string_view id);
bool cancel_requested(const store& s, std::string_view id);
void finish_progress(const store& s, std::string_view id);

struct quarantined {
    std::string program, id, reason, until;
};
std::vector<quarantined> quarantine(const store& s);
void set_quarantine(const store& s, const std::vector<quarantined>& list);
// Whether a case is quarantined today.
bool is_quarantined(const std::vector<quarantined>& list, std::string_view program,
                    std::string_view id);

struct gc_report {
    std::size_t removed = 0, kept = 0;
    std::uintmax_t bytes = 0;
};
// Removes runs older than `keep_days` beyond the newest `keep_runs`.
gc_report collect(const store& s, int keep_days, int keep_runs);
// Problems of the store: index lines without a run, runs without an index line, files that do not
// parse.
std::vector<std::string> verify(const store& s);
// Takes the runs of another store (a CI artifact) into this one.
std::size_t import_store(const store& s, const std::filesystem::path& other);

}  // namespace mcpplibs::mtest::engine
