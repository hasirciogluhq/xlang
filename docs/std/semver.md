# Versions (SemVer)

The version module is in `std/semver`. It parses and compares versions by precedence as defined by Semantic Versioning 2.0.0 (https://semver.org/spec/v2.0.0.html).

| Function | Purpose |
|---|---|
| `semverParse(text)` | returns a `Semver` (`major`, `minor`, `patch` as `ui64`, `pre` as a view, and `valid`) |
| `semverCompare(a, b)` | -1, 0 or 1 by precedence; build metadata is ignored |

The rules applied: numeric fields have no leading zeros; pre-release identifiers are non-empty, use `[0-9A-Za-z-]`, and numeric ones have no leading zeros; build metadata may contain leading zeros. A pre-release version is lower than the normal version; numeric identifiers compare by value and rank below alphanumeric ones; a shorter identifier list is lower when the shared identifiers are equal.

The `pre` view points into the input text, so the input must outlive the `Semver`. Numbers above 64 bits make the version invalid.

Version ranges (`^1.2`, `~1.2`) are not implemented.
