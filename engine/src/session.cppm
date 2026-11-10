// The test session of the mcpp test spec (docs/specs/test.md O9): a client holds one session and
// runs, adds and cancels cases while it does. The session builds nothing: it asks mcpp for the
// programs' launches (B5, `mcpp test --no-run --message-format json`), starts each program with the
// protocol's variables, and states what the program reports as it happens. `mtest serve` answers
// the session's methods beside its commands.
export module mcpplibs.mtest.engine.session;

import std;
import mcpplibs.mtest.engine.model;

export namespace mcpplibs::mtest::engine {

class session {
public:
    // A notification: its method (`test/build`, `test/case`, `test/program`, `test/run`) and its
    // parameters. Called from the session's threads.
    using notify_fn = std::function<void(std::string_view method, const json& params)>;

    explicit session(notify_fn notify);
    ~session();
    session(const session&) = delete;
    session& operator=(const session&) = delete;

    // Each answers a request's parameters with its result, or an `error` member naming why not.
    json plan(const json& params);
    json run(const json& params);
    json add(const json& params);
    json cancel(const json& params);
    json status(const json& params);
    // Every run ended as it does: what the session's client asked for is done.
    void drain();
    // Every run cancelled, the threads ended.
    void shutdown();
private:
    struct impl;
    std::unique_ptr<impl> d_;
};

}  // namespace mcpplibs::mtest::engine
