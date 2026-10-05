# Security Policy

## Reporting a vulnerability

Please report security vulnerabilities **privately** — do not open a public
issue, pull request, or discussion for a suspected vulnerability.

Use GitHub's private vulnerability reporting:

1. Go to the **Security** tab of this repository.
2. Click **Report a vulnerability** (GitHub Security Advisories).
3. Describe the issue, how to reproduce it, and the impact you observed.

This routes the report privately to the maintainer. If private reporting is not
available to you, open a minimal issue asking the maintainer to enable it —
without disclosing the vulnerability details — and wait for a private channel.

## What to include

- A clear description of the vulnerability and its impact.
- Steps to reproduce (a minimal case is ideal).
- The affected version / commit (`keur --version`).
- Any suggested remediation, if you have one.

## Response

As a solo-maintained project, there is no guaranteed SLA, but reports are taken
seriously and triaged as soon as possible. You will receive an acknowledgement
when the report is seen, an assessment of severity and affected versions, and
notification when a fix is released. Please allow reasonable time for a fix
before any public disclosure (coordinated disclosure).

## Scope

keur is a local commit-time guard (a CLI + rule engine). Security-relevant
reports include, for example: a crafted `.rule` file or scanned input that
causes keur to execute untrusted code, leak data, or crash in a way that
bypasses the gate; or a flaw in the hook/scanner-dispatch trust model
(see `docs/adr/ADR-019`). Findings produced *by* keur's rules against your own
code are not vulnerabilities in keur itself.

## Supported versions

The latest release on the default branch (`main`) receives security fixes.
