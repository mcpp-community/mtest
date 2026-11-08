module mcpplibs.mtest.engine.coverage;

import std;
import mcpplibs.os;

namespace mcpplibs::mtest::engine {

namespace os = mcpplibs::os;

namespace {

constexpr std::uint32_t kGcdaMagic = 0x67636461;  // "gcda"
constexpr std::uint32_t kArcsTag = 0x01a10000;    // GCOV_TAG_COUNTER_BASE: the arc counters

std::uint32_t swapped(std::uint32_t v) {
    return (v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24);
}

// A path without its last extension, in forward slashes: `obj/a/b.m.o` -> `obj/a/b.m`.
std::string stem_of(const std::filesystem::path& p) {
    auto s = os::fs::generic_utf8(p);
    const auto slash = s.rfind('/');
    const auto dot = s.rfind('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        s.erase(dot);
    return s;
}

// The compile database's objects, by their stem below `objects`, each naming its source from the
// root.
std::expected<std::map<std::string, std::string>, std::string>
sources_by_object(const config& c, const std::filesystem::path& objects) {
    std::map<std::string, std::string> out;
    if (c.coverage.database.empty())
        return std::unexpected(std::string("[coverage] database names no compile database"));
    const auto file = c.root / os::fs::from_utf8(c.coverage.database);
    std::ifstream in(file, std::ios::binary);
    auto db = json::parse(in, nullptr, false);
    if (db.is_discarded() || !db.is_array())
        return std::unexpected(
            std::format("{} is not a compile database (a JSON array)", os::fs::utf8(file)));
    std::error_code ec;
    for (const auto& e : db) {
        if (!e.is_object() || !e.contains("output") || !e.contains("file"))
            continue;
        std::filesystem::path dir = os::fs::from_utf8(e.value("directory", ""));
        if (dir.is_relative())
            dir = c.root / dir;
        auto output = os::fs::from_utf8(e.value("output", ""));
        auto source = os::fs::from_utf8(e.value("file", ""));
        if (output.is_relative())
            output = dir / output;
        if (source.is_relative())
            source = dir / source;
        const auto rel = std::filesystem::relative(output.lexically_normal(), objects, ec);
        if (ec || rel.empty() || rel.begin()->string() == "..")
            continue;
        const auto from_root = std::filesystem::relative(source.lexically_normal(), c.root, ec);
        out[stem_of(rel)] = os::fs::generic_utf8(ec ? source : from_root);
    }
    return out;
}

// The sources one program's run executed: each `.gcda` below `dir` that records a counter that
// ran, by its object's source.
std::vector<std::string> executed_sources(const std::filesystem::path& dir,
                                          const std::map<std::string, std::string>& sources) {
    std::set<std::string> files;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(dir, ec), end; it != end;
         it.increment(ec)) {
        if (it->path().extension() != ".gcda" || !gcda_executed(it->path()))
            continue;
        const auto rel = std::filesystem::relative(it->path(), dir, ec);
        if (auto s = sources.find(stem_of(rel)); s != sources.end())
            files.insert(s->second);
    }
    return {files.begin(), files.end()};
}

}  // namespace

std::filesystem::path coverage_file(const store& s) {
    return s.dir / "coverage.json";
}

std::expected<coverage_map, std::string> read_coverage(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return std::unexpected(std::format("no coverage map at {}", os::fs::utf8(file)));
    auto j = json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object() || j.value("format", "") != kCoverageFormat)
        return std::unexpected(
            std::format("{} is not a coverage map ({})", os::fs::utf8(file), kCoverageFormat));
    coverage_map m;
    m.commit = j.value("commit", "");
    m.time = j.value("time", "");
    m.epoch = j.value("epoch", std::int64_t{0});
    if (j.contains("cases") && j["cases"].is_object())
        for (auto it = j["cases"].begin(); it != j["cases"].end(); ++it) {
            auto& files = m.cases[it.key()];
            if (it.value().is_array())
                for (const auto& f : it.value())
                    if (f.is_string())
                        files.push_back(f.get<std::string>());
            std::ranges::sort(files);
        }
    return m;
}

void write_coverage(const std::filesystem::path& file, const coverage_map& m) {
    json cases = json::object();
    for (const auto& [k, files] : m.cases)
        cases[k] = files;
    json j = {{"format", kCoverageFormat},
              {"commit", m.commit},
              {"time", m.time},
              {"epoch", m.epoch},
              {"cases", cases}};
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const auto tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary);
        out << dumped(j) << '\n';
    }
    std::filesystem::rename(tmp, file, ec);
}

std::optional<coverage_map> fresh_coverage(const store& s, std::chrono::milliseconds max_age) {
    auto m = read_coverage(coverage_file(s));
    if (!m)
        return std::nullopt;
    const auto measured = std::chrono::sys_seconds{std::chrono::seconds{m->epoch}};
    if (std::chrono::system_clock::now() - measured > max_age)
        return std::nullopt;
    return std::move(*m);
}

bool gcda_executed(const std::filesystem::path& gcda) {
    std::ifstream in(gcda, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
    if (bytes.size() < 16 || bytes.size() % 4)
        return false;
    std::vector<std::uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    const bool swap = words[0] != kGcdaMagic;
    if (swap && swapped(words[0]) != kGcdaMagic)
        return false;
    auto word = [&](std::size_t i) { return swap ? swapped(words[i]) : words[i]; };
    // magic, version, stamp, checksum; then records of a tag and a length in bytes.
    for (std::size_t i = 4; i + 2 <= words.size();) {
        const auto tag = word(i);
        const auto length = static_cast<std::int32_t>(word(i + 1));
        i += 2;
        if (tag == 0)
            break;
        if (length <= 0)
            continue;  // a run of zero counters, or an empty record
        const auto n = static_cast<std::size_t>(length) / 4;
        if (tag == kArcsTag)
            for (std::size_t k = i; k < i + n && k < words.size(); ++k)
                if (word(k) != 0)
                    return true;
        i += n;
    }
    return false;
}

int gcov_strip(const std::filesystem::path& objects) {
    std::error_code ec;
    const auto abs = std::filesystem::absolute(objects, ec).lexically_normal();
    int n = 0;
    for (const auto& part : abs.relative_path())
        if (!part.empty())
            ++n;
    return n;
}

std::expected<coverage_map, std::string>
collect_coverage(const config& c,
                 const std::map<std::filesystem::path, std::vector<std::string>>& dirs,
                 std::string commit) {
    const auto objects = c.root / os::fs::from_utf8(c.coverage.objects);
    auto sources = sources_by_object(c, objects.lexically_normal());
    if (!sources)
        return std::unexpected(sources.error());
    coverage_map m;
    m.commit = std::move(commit);
    m.time = now_iso();
    m.epoch = std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
    for (const auto& [dir, cases] : dirs) {
        const auto files = executed_sources(dir, *sources);
        for (const auto& k : cases)
            m.cases[k] = files;
    }
    return m;
}

}  // namespace mcpplibs::mtest::engine
