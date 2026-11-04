# Security Policy

das2C reads data streams and files from sources it does not control, fetches
them over HTTP(S), and can hold login credentials for the servers it talks
to.  We take reports of security problems in any of those areas seriously,
even though most das data is public: the data may be open, but a user's login
is not.

## Supported Versions

Security fixes are made on the `master` branch and go out in the next
release of the current major version.  Older major versions do not receive
security fixes; please upgrade.

| Version | Supported |
|---------|-----------|
| 3.x     | Yes       |
| 2.x     | No        |

## Reporting a Vulnerability

Please do **not** open a public issue for a security problem.

Report it through GitHub's private vulnerability reporting instead:
<https://github.com/das-developers/das2C/security/advisories/new>.  The
same form is reachable from the repository's **Security and quality** tab
with the **Report a vulnerability** button; GitHub asks you to sign in
first.  The report is visible only to you and
the maintainers, and lets us work on a fix and an advisory with you in
private.

A useful report includes:

- The das2C version or commit, and the operating system and compiler
- Which program or library function is involved
- Steps or an input file that reproduces the problem
- What you think the impact is, for example memory corruption, credential
  disclosure, or a man-in-the-middle opportunity

## What to Expect

We aim to acknowledge a report within two weeks and to keep you informed as
we investigate.  Once a fix is ready we will publish a GitHub security
advisory, and request a CVE where one is warranted.  We are glad to credit
reporters in the advisory unless you would rather we did not.

We ask that you give us a chance to release a fix before disclosing the
problem publicly.  If we have not resolved a confirmed issue within 60 days
we will discuss a disclosure date with you.

## Scope

Areas of particular interest:

- Parsing of das1, das2 and das3 streams and other untrusted input
  (memory safety, crashes, unbounded resource use)
- The HTTP(S) client: TLS verification, redirects, proxy handling
- Credential handling: the credentials manager, its key file, and prompts

Problems in third-party dependencies (OpenSSL, expat, FFTW, zlib, CSPICE,
CDF) should be reported to those projects, though we would like to hear
about them if das2C's use of a library makes the problem worse.
