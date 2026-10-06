# Schema conformance

`test_schema_examples` reads every official example from the MCP
specification's schema through the SDK and writes it back. The written
value must equal the example as canonical JSON: key order is free, numbers
compare by value, and nothing may be added, dropped, renamed or change type.

- **Fixtures:** `fixtures/2026-07-28/`, copied unchanged from the
  specification. `SOURCE.md` there records the commit they came from, and
  `LICENSE` the terms they are under.
- **Coverage:** every fixture type is either checked or named in
  `kKnownGaps` with the reason it can't be yet. A type in neither fails the
  suite, and so does a gap whose fixtures all pass, so a fix is kept by
  taking the type off the list.

## Refreshing the fixtures

From a checkout of the specification repository:

```sh
scripts/update-schema-fixtures.sh /path/to/modelcontextprotocol
```

This replaces `fixtures/2026-07-28/` and updates `SOURCE.md`. Run the suite
afterwards: a new fixture type has to be checked or named as a gap.
