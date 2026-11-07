module mcpplibs.mtest.e2e;

import std;
import mcpplibs.os;

namespace mcpplibs::mtest::e2e {

namespace os = mcpplibs::os;

namespace {
constexpr std::string_view protocol_variables[] = {"MTEST_PROTOCOL",  "MTEST_MODE",
                                                   "MTEST_REPORT",    "MTEST_SELECT",
                                                   "MTEST_ISOLATION", "MTEST_TMPDIR",
                                                   "MTEST_SEED"};
}

outcome run(command c) {
    outcome r;
    if (c.argv.empty())
        return r;
    // An ok image (`.oke`) is started by a loader: this program's own when it is an image, else
    // `okrun`.
    if (c.argv.front().ends_with(".oke")) {
        auto loader = os::env::loader();
        if (!loader)
            loader = os::env::find_executable("okrun");
        if (loader)
            c.argv.insert(c.argv.begin(), os::fs::utf8(*loader));
    }
    os::process::spawn_options o;
    o.program = os::fs::from_utf8(c.argv.front());
    o.args.assign(c.argv.begin() + 1, c.argv.end());
    o.cwd = c.cwd;
    for (auto v : protocol_variables)
        o.env.emplace_back(std::string(v), std::nullopt);
    for (auto& kv : c.env)
        o.env.push_back(std::move(kv));
    o.in = c.input.empty() ? os::process::stdio::null : os::process::stdio::pipe;
    os::process::bounds limits;
    limits.hard = c.timeout;
    auto done = os::process::run(std::move(o), limits, c.input);
    if (!done) {
        r.err = std::format("{} did not start: {}", c.argv.front(), done.error().message());
        return r;
    }
    r.started = true;
    r.status = done->status.shell_code();
    r.timed_out = done->timed_out;
    r.out = std::move(done->out);
    r.err = std::move(done->err);
    r.duration = done->duration;
    return r;
}

std::string program(std::string_view name) {
    std::string key = "MCPP_BIN_";
    for (char ch : name)
        key += std::isalnum(static_cast<unsigned char>(ch))
                   ? char(std::toupper(static_cast<unsigned char>(ch)))
                   : '_';
    if (auto v = os::env::get(key); v && !v->empty())
        return *v;
    if (auto p = os::env::find_executable(name))
        return os::fs::utf8(*p);
    return std::string(name);
}

std::filesystem::path
tree(std::initializer_list<std::pair<std::string_view, std::string_view>> files) {
    static int trees = 0;
    const auto root = temp_dir() / std::format("tree-{}", ++trees);
    for (const auto& [path, content] : files) {
        const auto file = root / os::fs::from_utf8(path);
        std::filesystem::create_directories(file.parent_path());
        if (auto w = os::fs::write(file, content); !w)
            fail(std::format("could not write {}: {}", os::fs::utf8(file), w.error().message()));
    }
    std::filesystem::create_directories(root);
    return root;
}

comparison golden(const std::filesystem::path& file, std::string_view text) {
    comparison c;
    if (os::env::get("MTEST_GOLDEN").value_or("") == "update") {
        std::filesystem::create_directories(file.parent_path());
        c.held = os::fs::write(file, text).has_value();
        if (!c.held)
            c.expression = std::format("could not write {}", os::fs::utf8(file));
        return c;
    }
    auto have = os::fs::read(file);
    if (!have) {
        c.expression =
            std::format("no golden file {} (MTEST_GOLDEN=update writes it)", os::fs::utf8(file));
        return c;
    }
    c.held = *have == text;
    if (!c.held) {
        std::size_t at = 0;
        while (at < have->size() && at < text.size() && (*have)[at] == text[at])
            ++at;
        const auto line = 1 + std::count(have->begin(), have->begin() + std::ptrdiff_t(at), '\n');
        c.expected = have->substr(at > 40 ? at - 40 : 0, 120);
        c.actual = std::string(text.substr(at > 40 ? at - 40 : 0, 120));
        c.expression = std::format("differs from {} at line {} (MTEST_GOLDEN=update rewrites it)",
                                   os::fs::utf8(file), line);
    }
    return c;
}

}  // namespace mcpplibs::mtest::e2e
