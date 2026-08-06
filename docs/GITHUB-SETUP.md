# GitHub repository setup

After pushing the repository:

1. Enable GitHub Actions and allow actions from GitHub-owned publishers.
2. Keep the default `GITHUB_TOKEN` permission restricted; workflows request only
   the permissions needed by each job.
3. Protect the default branch and require the **CI / validate-and-test** check.
4. Enable Discussions if you want the support path described in `SUPPORT.md`.
5. Enable private vulnerability reporting for the workflow in `SECURITY.md`.
6. Review artifact and log retention settings.
7. Optionally enable immutable releases after the first successful prerelease.
8. Replace no files or assets after publishing; issue a new version instead.

The Q90 image workflow can also be run manually from the Actions tab. Tagged
releases are created only when a pushed tag exactly matches `v$(cat VERSION)`.
Artifact attestations work for public repositories on current GitHub plans and
for eligible private repositories.

The workflows use Node.js 24-native GitHub Actions. Self-hosted runners must be
version `2.327.1` or newer; GitHub-hosted `ubuntu-24.04` runners already meet
that requirement.
