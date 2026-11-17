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

## Development environment

Develop and run the frontend, Java API and Python voice service in this project's independent `xiaozhi-dev` Dev Container. Run Git, builds, dependency installation and development/test tools inside that container. Never install development tools or dependencies on the host. Keep dependencies, caches and generated output in `xiaozhi-dev-*` Docker volumes; see [the local development guide](deploy/local-zhou/devcontainer.md).

Development MySQL and Redis run as independent Xiaozhi containers on `172.16.18.104`, with separate accounts, ports and data volumes. Do not use or modify AI Market's databases, Redis, files, containers or configuration. AI Market is a read-only reference. Keep private configuration and access material under `/home/timyai/dev/runtime/xiaozhi/`; never commit secrets. Container temporary files and logs belong under `/opt/xiaozhi-dev`, outside the source bind mount.

## Separate deployed environment

Production environment `114` is separate from development, not a dependency of local development. Do not connect development services to its databases, copy its runtime configuration, or change its services as part of local development. Production login accounts, SSH commands, jump routes and private access-file paths must not be recorded in repository files.

Only when explicitly working on that deployed environment, read [the current deployment entry](docs/deployment/current.md) and linked evidence before looking up endpoints or versions, provisioning, testing or restarting services. If these local documents are missing or stale, check the entry on GitHub's latest `main` first. Physical-device acceptance remains separate from development checks.
