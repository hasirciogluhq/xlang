# Date and time

The date module is in `std/time`. It works on the proleptic Gregorian calendar and on UTC; it has no time zones.

| Function | Purpose |
|---|---|
| `DateTime` | a calendar date and time of day (year, month, day, hour, minute, second) |
| `daysFromCivil(y, m, d)` | days from 1970-01-01 to a civil date (Hinnant's days_from_civil) |
| `civilFromDays(days)` | the inverse of `daysFromCivil`, as a `DateTime` with the time at 00:00:00 |
| `unixSeconds(dt)` | seconds since 1970-01-01T00:00:00Z |
| `fromUnixSeconds(t)` | the `DateTime` of a Unix time |
| `isLeapYear(y)`, `daysInMonth(y, m)`, `validDate(y, m, d)` | calendar rules |
| `formatRfc3339(dt, out)` | writes the 20-byte form `YYYY-MM-DDTHH:MM:SSZ` |
| `parseRfc3339(text, n)` | returns `ParsedTime` (`value`, and `ok` false on bad input); accepts `T`, `t` or a space as separator, skips an optional fraction and requires `Z` |

The specification is RFC 3339 (https://www.rfc-editor.org/rfc/rfc3339), section 5.6 for the form and section 5.8 for leap seconds, which are not represented: a second value of 60 is refused.

The conversions were checked against the system `date` command. For example 2024-02-29T12:34:56 UTC is Unix time 1709210096.

Not implemented: ISO 8601 variants other than RFC 3339, time zones and the TZif database (RFC 8536). See [Known limitations](../known-limitations.html).

## Time zones (`std/time/tzif`)

| Function | Specification |
|---|---|
| `tzifParse(data, n)` returns `TzFile` | RFC 8536 (TZif version 2 and later; the 64-bit block is used when present) |
| `tzLocalAt(file, t)` returns `TzLocal` (`utoff` in seconds east of UTC, `isdst`, `abbr`) | RFC 8536 section 3.2 lookup rules |
| `posixFixed(data, off, len)` | POSIX.1 section 8.3, the footer TZ string when it names a standard time only |

The files are read from the system zoneinfo directory by the caller (for example `/usr/share/zoneinfo/America/New_York`); no file access happens in the module. Checked against the system files: New York gives EDT (-14400, DST) in July and EST in January, a time before the first transition gives LMT (-17762), and Europe/Istanbul gives +03 in 2024 from its footer.

A time after the last transition of a zone whose footer has a DST part uses the type of that last transition, because the POSIX rule is not evaluated. For the zones in the system database this is correct for dates up to the last listed transition, which tzdata extends to 2037 for most DST zones.
