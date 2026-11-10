# Testing

The repository has three test suites.

## Frontend and semantic checks (C++)

```
xmake build xlang-tests && xmake run xlang-tests
```

`test/compiler/` holds the C++ tests for the lexer, parser, semantic analysis and HIR lowering. They are fast (a few seconds) and every compiler change is expected to pass them.

## Language unit tests (`*.test.xl`)

```
xlang test test/unit/
xlang test test/unit/for.test.xl        # one file
xlang test 're:^for$'                   # by name
xlang test --parallel                   # all of them at once
```

`test/unit/` holds suites written in xlang itself, using `std/testing`. A file is a test when it is named `*.test.xl` and has an `@entry fn main() -> i32` that ends in `suiteRun(&s)`; a non-zero exit status fails the file. The runner accepts a file, a directory, a glob (`**`, `*`, `?`), `re:<regex>` or a path substring.

```xlang
from "std/testing" import Suite, T, suiteInit, test, suiteRun, expectEq

fn addWorks(t: *T) { expectEq(t, 1 + 2, 3) }

@entry fn main() -> i32 {
    local s: Suite
    suiteInit(&s)
    test(&s, "add works", addWorks)
    return suiteRun(&s)
}
```

The matchers are typed functions (`expect`, `expectTrue`, `expectEq`, `expectEqUInt`, `expectEqStr`, `expectNear`, `expectGt`, `expectNil`, ...). There is no discovery and no chained matcher syntax. These suites run on the host only; the cross-OS legs belong to the e2e programs.

The suites `slices.test.xl` and `for_iter.test.xl` cover slices (`[]T`, `[]mut T`, `buffer[lo..hi]`) and the iterating `for` (`for x in s`, `for x, i in s`); the matching e2e program is `for_iter`.

## End-to-end programs

```
xmake run e2e
```

`test/e2e/` holds one `.xl` program per feature. `xmake run e2e` compiles the runner `test/e2e/runner.xl`, which is itself an xlang program, and runs it. The runner reads the table `test/e2e/tests.txt` (`run|name|exit code|output substring` and `diag|name|flag|diagnostic code or ok`), builds each program with the compiler under test, starts it as a child process with its output redirected into a file (`processSpawnOut` in `std/os/process`), and checks the exit code and one substring of the output. It runs natively on Linux and macOS, one entry at a time, and takes an optional name filter (a plain substring of the table line):

```
xlang build test/e2e/runner.xl -o outputs/e2e-runner
outputs/e2e-runner outputs/xlang test/e2e              # the whole table
outputs/e2e-runner outputs/xlang test/e2e raii         # the entries whose line contains "raii"
```

The cross-target legs below, the TLS and HTTPS server/client pairs and the package cases are still run by the shell script `test/e2e/run.sh`, which is not part of `xmake run e2e` and takes the compiler path as its first argument. Each program is cross-built and run on the other targets the host can reach:

| Target | How |
|---|---|
| the host | built natively, run directly |
| Linux aarch64 and x86_64 | cross-built, run in a Docker container (`alpine:3`) |
| Windows x86_64 | cross-built, run under CrossOver or Wine when `cxrun` is on `PATH` |

A Docker registry failure is reported as a skip, not a failure: `alpine:3` is pulled once with three attempts, and a container that never starts is retried once.

### Running part of the suite

A full run takes a while, because every program is built four times. While working on one area, run only what changed:

```
XLANG_E2E_FILTER=math_ test/e2e/run.sh          # every test whose name contains "math_"
XLANG_E2E_FILTER='^big$' test/e2e/run.sh        # exactly one test
XLANG_E2E_FILTER='^(try|sched)$' test/e2e/run.sh
XLANG_E2E_FILTER=diag/ test/e2e/run.sh          # the compile-time diagnostic cases
XLANG_E2E_FILTER=packages test/e2e/run.sh       # the package system cases
```

The filter is a POSIX extended regular expression matched against the test name, not against the target label, so one name selects all four legs of that test. `run.sh` also takes the compiler path as its first argument: `test/e2e/run.sh ./outputs/xlang`.

The independent legs run in a pool of workers, one per CPU by default. `XLANG_E2E_JOBS=1` serialises them, which is easier to read when a failure is being traced, and a larger value helps on a machine with many cores. The server and client pairs (TLS, HTTPS) and the package cases stay sequential: each needs a free port or a shared project directory.

### Environment switches

| Variable | Effect |
|---|---|
| `XLANG_E2E_FILTER` | run only the tests whose name matches the regular expression |
| `XLANG_E2E_JOBS` | concurrent legs (default: the CPU count) |
| `XLANG_E2E_NET=1` | adds the tests that need the Internet (`https`) |
| `XLANG_E2E_S3=1` | adds `s3`, which needs an S3-compatible server on `127.0.0.1:18333` |

Run the full suite once at the end of a large change, and the single relevant program while working. A new e2e program is added to the `tests` array in `run.sh` (and to `docs/` for the feature it covers), where the fields are the name, the expected exit code, a substring of the expected output, whether the program uses OS threads, and whether it can run on Windows.
