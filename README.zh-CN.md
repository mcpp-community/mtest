# mtest

面向 Agent 时代的测试系统：测试程序与运行它们的工具之间的一份协议，一个无宏、无头文件的 C++ 测试库，
以及一个运行、存储并解释任意语言测试的引擎——同样服务于人、CI 与 Agent。

[English](README.md) | 简体中文

## 组成

各部分可以单独使用；组合使用时共享同一数据模型。

| 部分 | 提供 | 位置 |
|---|---|---|
| 协议 | 测试程序如何列出、选择与报告其用例 | [spec/protocol.md](spec/protocol.md) |
| C++ 库 | 用例、期望、夹具；`bench`、`cts`、`e2e` 模块 | [lib/](lib/) — `mcpplibs.mtest` |
| 引擎 | run、plan、explain、history、analyze、graph、gate、trace、bisect、ci | [engine/](engine/) — `mtest` 程序 |
| 服务 | 以 JSON-RPC 与 MCP 工具提供引擎的命令 | `mtest serve` — [docs/service.md](docs/service.md) |
| 适配 | GoogleTest、Bash、Python | [adapters/](adapters/) — [docs/adapters.md](docs/adapters.md) |
| C ABI | 供任何有 C FFI 的语言使用的协议 | [capi/](capi/) — `include/mtest.h` |
| 一致性 | 检验协议每一种实现的同一套用例 | [conformance/](conformance/) |

## 用库写测试

```cpp
import mcpplibs.mtest;
using namespace mcpplibs::mtest;

int main() {
    "adds"_test = [] { expect(eq(1 + 1, 2)); };
    "reads a file"_test[tag{"fs"}] = [] {
        auto dir = temp_dir();
        expect(std::filesystem::is_directory(dir));
    };
    return report();
}
```

```toml
[dev-dependencies]
mcpplibs.mtest = "0.8.2"
```

失败时给出位置与两侧的值（`expected 2, actual 3`）。在运行器之下，同一程序按协议报告；单独运行时每个用例输出一行。

文件可以只以对象声明用例而不写 `main`，由 mtest 的入口作为起点（mcpp 2026.11.8.1 及以后；更早的 mcpp 需写
`[test] main = { from = "mtest/main" }`）。每个对象是一个用例，运行器按它声明的属性（类型即类别，以及资源、时限、隔离、重试、诊断）列举、选择与调度；其函数体声明的用例可在其中单独选择（[lib/docs/writing-tests.md](lib/docs/writing-tests.md)）：

```cpp
import mcpplibs.mtest;
using namespace mcpplibs::mtest;

UTest empty{"an empty line", [] { expect(eq(parse(""), 0)); }};

E2E cli{"cli", resource{"serial:home"}, timeout{std::chrono::minutes{2}}, [] {
    "prints its version"_test = [] { /* ... */ };
    "reads its configuration"_test[retries{1}] = [] { /* ... */ };
}};
```

用例在何处运行、需要机器具备什么，同样是属性：`only{platform::windows}`、`except{architecture::aarch64}`，以及在代码中声明一次的 `capability`（`const capability gcc{"gcc", {.program = {"g++"}}};`，再写 `"links"_test[gcc]`）。无论由哪个运行器启动，程序都在场所之外跳过用例并说明原因；`on<platform::windows>([&](auto) { ... })` 中的代码在其他平台的构建中被丢弃。

## 用引擎运行测试

```bash
mtest run ./build/tests/*          # 任意程序：按协议，否则按退出码
mtest run --set smoke              # mtest.toml 中的集合：smoke、regression、pr、full、release
mtest explain                      # 上一次失败的位置、原因，以及复现它的命令
mtest plan --since origin/main     # 一次改动影响哪些用例，以及每个用例入选的原因
mtest analyze hot                  # 最慢的用例；另有 durations、path、failures、flaky、bench
mtest report --format md           # 以 Markdown 报告一次运行：失败、指标、资源使用、以 mermaid 绘出的关键路径
mtest serve                        # 同样的命令，供 Agent（MCP）或 IDE（JSON-RPC）使用
```

每个命令都接受 `--json`，每个结果都带 `conclusion`。见 [docs/commands.md](docs/commands.md) 与
[docs/config.md](docs/config.md)。

## 等级

| 等级 | 程序要做的 | 运行器得到的 |
|---|---|---|
| L0 | 无 | 每个程序一个结果，按退出码 |
| L1 | 列出、选择、报告 | 用例树、单个用例、失败位置 |
| L2 | 指标 | bench 判定、一致性条款 |
| L3 | 资源、产物 | 调度、受影响选择 |

库、C ABI 与脚本适配为 L3；GoogleTest 为 L1。

## 构建

mtest 以 [mcpp](https://github.com/mcpp-community/mcpp) 构建：

```bash
mcpp build                      # 全部成员
mcpp test -p mcpplibs.mtest -p mtest.mtest    # 库与引擎的测试
mcpp test -p conformance        # 以库检验协议（MTEST_CTS_PROGRAM：检验其他实现）
```

## 许可

[Apache-2.0](LICENSE)
