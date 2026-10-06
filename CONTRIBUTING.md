# Contributing

Bug reports are welcome as issues. Include the program, the fault record or diagnostic it produced, `cint --version`, and the C compiler you built with. A question about the specification is welcome too; cite the edition and clause (`SPEC-01P.1 IM-106`).

A pull request that changes behavior comes with a conformance case under `conformance/` that shows the change, and passes the checks the CI runs. This repository receives one commit per release, so an accepted pull request is applied upstream and appears in the next release, credited by name in the commit and in the changelog.

Every commit carries a Developer Certificate of Origin sign-off (`git commit -s`, which adds `Signed-off-by: Your Name <you@example.com>`): it certifies that you wrote the change, or have the right to submit it, under this repository's license. See <https://developercertificate.org/>.
