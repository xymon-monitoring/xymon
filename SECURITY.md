# Security policy

## Reporting a vulnerability

Report it privately through GitHub:
<https://github.com/xymon-monitoring/xymon/security/advisories/new>
(the repository's **Security** tab, **Report a vulnerability**).

Do not report it on the mailing list (xymon@xymon.com), in a public issue or in a
pull request: they are public.

Please include:

- the affected versions, and the component (server, client, a CGI, the network protocol);
- the preconditions: configuration, and what access the attacker needs;
- the steps to reproduce, or a proof of concept;
- the impact;
- any disclosure deadline you work to.

The maintainers answer in the advisory, work out a fix there, and publish the
advisory with the release that carries it. Reporters are credited unless they ask
not to be.

## Supported versions

Fixes go into `main` and the next release. Older releases are not patched; the
fix is for the packagers to backport.
