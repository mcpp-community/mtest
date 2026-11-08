// The static case manifest: the cases a source file declares with this library, read from its text
// without building it -- `"name"_test` literals with their attributes (`[kind::e2e]`,
// `[tag{"slow"}]`), inside named suites (`suite x{"name", ...}`) and inside other cases, with the
// identifiers the program would list (`suite/case/inner`). Tools that show or plan cases before a
// build read it (`mtest list --static`, an editor); the program's own listing (P3) is the
// authority once it is built.
export module mcpplibs.mtest.manifest;

import std;

export namespace mcpplibs::mtest::manifest {

struct static_case {
    std::string id;    // as the program lists it
    std::string name;  // the literal
    int line = 0, column = 0;
    std::string kind = "unit";
    std::vector<std::string> tags;
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

struct scope {
    std::string id;
    int depth;  // the brace depth inside it
};

// `[kind::e2e]`, `[tag{"slow"}]`, ... after a literal: each bracket group, read for the kind and
// the tags it names.
void read_attributes(reader& r, static_case& c) {
    for (r.skip_space(); r.peek() == '['; r.skip_space()) {
        int depth = 0;
        std::string last_word;
        do {
            const char ch = r.peek();
            if (ch == '"') {
                auto value = r.string_literal();
                if (last_word == "tag")
                    c.tags.push_back(value);
                continue;
            }
            if (ident(ch)) {
                auto w = r.word();
                if (last_word == "kind")
                    c.kind = w;
                last_word = w;
                continue;
            }
            if (ch == '[')
                ++depth;
            else if (ch == ']')
                --depth;
            else if (ch != ':' && ch != '{' && ch != '}' &&
                     !std::isspace(static_cast<unsigned char>(ch)))
                last_word.clear();
            r.step();
        } while (!r.done() && depth > 0);
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
    int pending_parens = 0;
    // `suite <var>{"<name>"`: 1 after `suite`, 2 after its variable, 3 after its `{`.
    int suite_state = 0;
    std::string previous;
    auto prefix = [&] { return scopes.empty() ? std::string{} : scopes.back().id + "/"; };
    while (!r.done()) {
        r.skip_space();
        if (r.done())
            break;
        const char ch = r.peek();
        if (ch == '"' || (ch == 'R' && r.peek(1) == '"')) {
            const int line = r.line, column = r.column();
            auto value = r.string_literal();
            if (suite_state == 3) {
                pending.reset();
                scopes.push_back({prefix() + value, depth});
                suite_state = 0;
                continue;
            }
            if (r.text.substr(r.at, 5) == "_test" && !ident(r.peek(5))) {
                for (int k = 0; k < 5; ++k)
                    r.step();
                static_case c{prefix() + value, value, line, column};
                read_attributes(r, c);
                pending = c.id;
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
            if (w == "suite" && (previous.empty() || previous == "mtest"))
                suite_state = 1;
            else if (suite_state == 1)
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
        // `suite x{"name"`: the brace right after the variable, then the literal right after it.
        if (suite_state == 3 || (suite_state == 2 && ch != '{'))
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
                scopes.push_back({*pending, depth});
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
