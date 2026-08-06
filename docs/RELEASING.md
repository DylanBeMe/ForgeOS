# Releasing ForgeOS

1. Update `VERSION`, `CHANGELOG.md`, documentation, and screenshots.
2. Run `./tests/run.sh` from a clean checkout.
3. Build and hardware-test the Q90 image on a spare microSD card.
4. Confirm every item in `RELEASE-CHECKLIST.md` that applies.
5. Commit the release and push it through pull-request CI.
6. Create and push an annotated tag matching `v$(cat VERSION)`.

```sh
git tag -a "v$(cat VERSION)" -m "ForgeOS $(cat VERSION)"
git push origin "v$(cat VERSION)"
```

`.github/workflows/release.yml` then:

- rejects a tag that does not match `VERSION`;
- validates adapters, themes, shell scripts, C code, and packaging;
- builds the generic simulator and full Q90 image;
- compresses the image as `.img.xz`;
- creates and verifies `SHA256SUMS`;
- creates a provenance attestation when the repository plan supports it;
- publishes a GitHub prerelease using notes extracted from `CHANGELOG.md`.

Never replace assets on an existing release. Correct the source, increment the
version, and publish a new tag so checksums and provenance remain meaningful.
