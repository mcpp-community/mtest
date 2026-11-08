// The static case manifest: the cases a source file declares with this library, read from its text
// without building it -- `"name"_test` literals with their attributes (`[kind::e2e]`,
// `[tag{"slow"}]`, `[resource{"machine"}]`), the cases named suites are (`E2E cli{"cli",
// resource{"serial:home"}, [] { ... }}`), and the cases declared inside either, which have the
// kind and attributes of what declares them, with the identifiers the program reports
// (`suite/case/inner`). Tools that show or plan cases before a
// build read it (`mtest list --static`, an editor); the program's own listing (P3) is the
// authority once it is built. What only the program decides -- a `skip_if` condition, a time
// limit -- is left to that listing.
export module mcpplibs.mtest.manifest;

import std;

export namespace mcpplibs::mtest::manifest {

struct static_case {
    std::string id;    // as the program lists it
    std::string name;  // the literal
    int line = 0, column = 0;
    std::string kind;  // empty: no attribute and no suite states one (the program's is given)
    std::vector<std::string> tags, resources, clauses, artifacts;
};

std::vector<static_case> scan(std::string_view source);

}  // namespace mcpplibs::mtest::manifest

namespace mcpplibs::mtest::manifest {

namespace {

bool ident(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// A reader of C++ text that skips comments and knows where literals end.
struct reader {
    std::string_view text;
    std::size_t at = 0;
    int line = 1;
    std::size_t line_start = 0;

    bool done() const {
        return at >= text.size();
    }
    char peek(std::size_t k = 0) const {
        return at + k < text.size() ? text[at + k] : '\0';
    }
    void step() {
        if (text[at] == '\n') {
            ++line;
            line_start = at + 1;
        }
        ++at;
    }
    int column() const {
        return static_cast<int>(at - line_start) + 1;
    }

    // Whitespace and comments.
    void skip_space() {
        while (!done()) {
            if (std::isspace(static_cast<unsigned char>(peek())))
                step();
            else if (peek() == '/' && peek(1) == '/')
                while (!done() && peek() != '\n')
                    step();
            else if (peek() == '/' && peek(1) == '*') {
                step();
                step();
                while (!done() && !(peek() == '*' && peek(1) == '/'))
                    step();
                if (!done()) {
                    step();
                    step();
                }
            } else
                return;
        }
    }

    // A string literal at `at` (`"..."` or `R"d(...)d"`); its value.
    std::string string_literal() {
        std::string value;
        if (peek() == 'R') {
            step();
            step();  // R"
            std::string delim;
            while (!done() && peek() != '(')
                delim += peek(), step();
            step();
            const std::string end = ")" + delim + "\"";
            while (!done() && text.substr(at, end.size()) != end)
                value += peek(), step();
            for (std::size_t k = 0; k < end.size() && !done(); ++k)
                step();
            return value;
        }
        step();  // "
        while (!done() && peek() != '"') {
            if (peek() == '\\') {
                step();
                if (done())
                    break;
            }
            value += peek();
            step();
        }
        if (!done())
            step();
        return value;
    }

    void char_literal() {
        step();
        while (!done() && peek() != '\'') {
            if (peek() == '\\')
                step();
            if (!done())
                step();
        }
        if (!done())
            step();
    }

    std::string word() {
        std::string w;
        while (!done() && ident(peek()))
            w += peek(), step();
        return w;
    }
};

// What a case has: its own attributes within its suite's and its enclosing cases'.
struct attrs {
    std::string kind;
    std::vector<std::string> tags, resources, clauses, artifacts;
};

void add(std::vector<std::string>& to, const std::string& value) {
    if (std::ranges::find(to, value) == to.end())
        to.push_back(value);
}

attrs within(const attrs& outer, const attrs& inner) {
    attrs a = outer;
    if (!inner.kind.empty())
        a.kind = inner.kind;
    for (const auto& x : inner.tags)
        add(a.tags, x);
    for (const auto& x : inner.resources)
        add(a.resources, x);
    for (const auto& x : inner.clauses)
        add(a.clauses, x);
    for (const auto& x : inner.artifacts)
        add(a.artifacts, x);
    return a;
}

struct scope {
    std::string id;
    int depth;  // the brace depth inside it
    attrs has;
};

// The suite types and the kind each states (Suite and suite state none).
std::optional<std::string> suite_kind(std::string_view word) {
    if (word == "Suite" || word == "suite")
        return std::string{};
    if (word == "UTest")
        return std::string("unit");
    if (word == "CTS")
        return std::string("cts");
    if (word == "E2E")
        return std::string("e2e");
    if (word == "Bench")
        return std::string("bench");
    return std::nullopt;
}

// One attribute's words and literals (`kind::e2e`, `tag{"slow"}`, `mtest::resource("machine")`):
// a literal is what the word before it names; a word after `kind` is the kind.
struct attribute_reader {
    attrs& into;
    std::string last_word;

    void literal(const std::string& value) {
        if (last_word == "tag")
            add(into.tags, value);
        else if (last_word == "resource")
            add(into.resources, value);
        else if (last_word == "clause")
            add(into.clauses, value);
        else if (last_word == "artifact")
            add(into.artifacts, value);
    }
    void word(std::string w) {
        if (last_word == "kind")
            into.kind = w;
        last_word = std::move(w);
    }
    // Reads one token at `r`; false at a character that is not part of an attribute.
    bool token(reader& r) {
        const char ch = r.peek();
        if (ch == '"' || (ch == 'R' && r.peek(1) == '"')) {
            literal(r.string_literal());
            return true;
        }
        if (ident(ch)) {
            word(r.word());
            return true;
        }
        if (ch != ':' && ch != '{' && ch != '}' && ch != '(' && ch != ')' &&
            !std::isspace(static_cast<unsigned char>(ch)))
            last_word.clear();
        return false;
    }
};

// `[kind::e2e]`, `[tag{"slow"}]`, ... after a case's literal: each bracket group.
void read_case_attributes(reader& r, attrs& into) {
    attribute_reader a{into};
    for (r.skip_space(); r.peek() == '['; r.skip_space()) {
        int depth = 0;
        do {
            if (a.token(r))
                continue;
            if (r.peek() == '[')
                ++depth;
            else if (r.peek() == ']')
                --depth;
            r.step();
        } while (!r.done() && depth > 0);
    }
}

// `, resource{"machine"}, tag{"slow"}` between a suite's name and its body: up to the body's `[`
// (a lambda) or the suite's closing `}`, at the suite's own depth.
void read_suite_attributes(reader& r, attrs& into) {
    attribute_reader a{into};
    int nesting = 0;
    for (r.skip_space(); !r.done(); r.skip_space()) {
        const char ch = r.peek();
        if (nesting == 0 && (ch == '[' || ch == '}'))
            return;
        if (a.token(r))
            continue;
        if (ch == '{' || ch == '(')
            ++nesting;
        else if (ch == '}' || ch == ')')
            --nesting;
        r.step();
    }
}

}  // namespace

std::vector<static_case> scan(std::string_view source) {
    reader r{source};
    std::vector<static_case> out;
    std::vector<scope> scopes;
    int depth = 0;
    // A scope opened by the next `{`: a named suite's or a case's body.
    std::optional<std::string> pending;
    attrs pending_attrs;
    int pending_parens = 0;
    // `<Suite type> <var>{"<name>"`: 1 after the type, 2 after its variable, 3 after its `{`.
    int suite_state = 0;
    std::string suite_type_kind;
    std::string previous;
    // A suite type of the file's own (`class Fixture : public CTS { ... }`) is of its base's kind:
    // 1 after `class`, 2 after its name, 3 after its `:`.
    std::map<std::string, std::string> derived;
    int class_state = 0;
    std::string class_name;
    auto kind_of = [&](const std::string& w) -> std::optional<std::string> {
        if (auto k = suite_kind(w))
            return k;
        if (auto it = derived.find(w); it != derived.end())
            return it->second;
        return std::nullopt;
    };
    auto prefix = [&] { return scopes.empty() ? std::string{} : scopes.back().id + "/"; };
    auto outer = [&] { return scopes.empty() ? attrs{} : scopes.back().has; };
    while (!r.done()) {
        r.skip_space();
        if (r.done())
            break;
        const char ch = r.peek();
        if (ch == '"' || (ch == 'R' && r.peek(1) == '"')) {
            const int line = r.line, column = r.column();
            auto value = r.string_literal();
            if (suite_state == 3) {
                // A named suite is a case, and the scope of the cases its body declares.
                pending.reset();
                attrs own;
                own.kind = suite_type_kind;
                if (suite_type_kind == "bench")
                    own.resources.push_back("machine");
                read_suite_attributes(r, own);
                const attrs has = within(outer(), own);
                out.push_back({prefix() + value, value, line, column, has.kind, has.tags,
                               has.resources, has.clauses, has.artifacts});
                scopes.push_back({prefix() + value, depth, has});
                suite_state = 0;
                continue;
            }
            if (r.text.substr(r.at, 5) == "_test" && !ident(r.peek(5))) {
                for (int k = 0; k < 5; ++k)
                    r.step();
                attrs own;
                read_case_attributes(r, own);
                const attrs has = within(outer(), own);
                static_case c{prefix() + value, value,         line,        column,       has.kind,
                              has.tags,         has.resources, has.clauses, has.artifacts};
                pending = c.id;
                pending_attrs = has;
                pending_parens = 0;
                out.push_back(std::move(c));
            }
            suite_state = 0;
            continue;
        }
        if (ch == '\'') {
            r.char_literal();
            continue;
        }
        if (ident(ch)) {
            auto w = r.word();
            if (w == "class" || w == "struct") {
                class_state = 1;
            } else if (class_state == 1 && w != "final") {
                class_name = w;
                class_state = 2;
            } else if (class_state == 3 && w != "public" && w != "mtest" && w != "mcpplibs") {
                if (auto k = kind_of(w))
                    derived[class_name] = *k;
                class_state = 0;
            }
            if (auto k = kind_of(w);
                k && class_state == 0 && (previous.empty() || previous == "mtest")) {
                suite_state = 1;
                suite_type_kind = *k;
            } else if (suite_state == 1)
                suite_state = 2;
            else
                suite_state = 0;
            previous = w == "mtest" || w == "mcpplibs" ? w : std::string{};
            continue;
        }
        r.step();
        if (ch == ':' && r.peek() == ':') {
            r.step();
            continue;  // `mtest::suite`: `previous` stays
        }
        previous.clear();
        class_state = ch == ':' && class_state == 2 ? 3 : 0;
        // `suite x{"name"`: the brace right after the variable, then the literal right after it.
        if (suite_state == 1 || suite_state == 3 || (suite_state == 2 && ch != '{'))
            suite_state = 0;
        if (ch == '(')
            ++pending_parens;
        else if (ch == ')')
            --pending_parens;
        else if (ch == '{') {
            ++depth;
            if (suite_state == 2)
                suite_state = 3;
            else if (pending && pending_parens <= 0) {
                scopes.push_back({*pending, depth, pending_attrs});
                pending.reset();
            }
        } else if (ch == '}') {
            while (!scopes.empty() && scopes.back().depth >= depth)
                scopes.pop_back();
            --depth;
        } else if (ch == ';') {
            pending.reset();
            suite_state = 0;
        }
    }
    return out;
}

}  // namespace mcpplibs::mtest::manifest
