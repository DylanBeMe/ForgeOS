# Verifying ForgeOS 0.6.4

Download `SHA256SUMS` into the same directory as the release assets, then run:

```sh
sha256sum -c SHA256SUMS
unzip -t forgeos-0.6.4-source.zip
unzip -t q90-forgeos-0.6.4-sd-overlay.zip
xz -t forgeos-q90-0.6.4.img.xz
```

For a GitHub-hosted build with an artifact attestation:

```sh
gh attestation verify forgeos-q90-0.6.4.img.xz --repo OWNER/REPOSITORY
```

After decompression, verify the image again if a matching checksum is supplied
for the raw image by the workflow artifact. Never flash an asset that fails an
integrity check.
