## Agent skills

### Issue tracker

Issues and specs live in GitHub Issues for `yanghan24/xiaozhi`. Before creating, reading, or updating tickets or specs, read `docs/agents/issue-tracker.md`.

### Triage labels

Use the five default triage roles. Before triaging an issue or changing its triage status, read `docs/agents/triage-labels.md`.

### Domain docs

Single-context layout: root `GLOSSARY.md` and `docs/adr/`. Before exploring the codebase, read `docs/agents/domain.md`.

## Collaboration

Before editing project files, creating branches or worktrees, committing, pushing, opening PRs, or undoing changes, read `CONTRIBUTING.md` for this repository's collaboration and recovery workflow.

Before planning or running physical-device tests, read the testing environment policy in `CONTRIBUTING.md`.

## Firmware delivery

Before modifying firmware source or uploading firmware artifacts, read [the GitHub firmware publishing workflow](docs/development/firmware-publishing.md). Finish firmware work by pushing its source changes and uploading the corresponding build with checksums.

## Backend validation

Run backend services, regression tests, and device protocol checks on Ubuntu `192.168.168.114` using SSH user `jt`. The local computer may provide editing, SSH, and browser access. Before looking up public endpoints or deployment versions, or provisioning, testing, or restarting backend services, read [the current deployment entry](docs/deployment/current.md) and its linked evidence. If local deployment documentation is missing or stale, check that entry on GitHub's latest `main` before concluding the information is unavailable.
