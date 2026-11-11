# Contributing to xlang

xlang is licensed under the [Apache License 2.0](LICENSE). Unless you state otherwise, any contribution you submit is licensed under the same terms (Apache-2.0 §5).

## Repositories

| Repository | Content |
| --- | --- |
| [xlang](https://github.com/hasirciogluhq/xlang) | CLI, tooling, build, integration (this repository) |
| [xlc](https://github.com/hasirciogluhq/xlc) | the compiler |
| [xl-std](https://github.com/hasirciogluhq/xl-std) | the standard library and its tests |
| [xlang-docs](https://github.com/hasirciogluhq/xlang-docs) | documentation site |
| [xl-vscode](https://github.com/hasirciogluhq/xl-vscode) | VS Code extension |

Open an issue or pull request in the repository the change belongs to. Library and compiler rules are in [`CLAUDE.md`](CLAUDE.md); read the stdlib and compiler sections before changing them.

## Workflow

1. Open an issue first for large changes or new language features.
2. Fork, branch from `development`, keep the change focused.
3. Build and run the tests of what you changed (see the [README](README.md#build)); run the full suites once before opening the PR.
4. Update the affected documentation in `xlang-docs/` in the same change.
5. Open a pull request against `development` and fill in the template. A maintainer review is required.

Behavior changes on `development` remove the old form completely; there are no compatibility layers.

## Commits

Use the imperative mood and an English subject of at most 72 characters; explain the why in the body.

## Security and conduct

Report vulnerabilities privately as described in [SECURITY.md](SECURITY.md). Participation is governed by the [Code of Conduct](CODE_OF_CONDUCT.md).
