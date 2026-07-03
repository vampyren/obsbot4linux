# Release process & notes template

Every GitHub release uses the SAME title format and the SAME chapters, in the
same order. No ad-hoc sections, no emoji section headers.

## Title

```
v0.X.Y — Short headline (3–6 words)
```

## Body template

```markdown
## Highlights
- The features/changes a user cares about, most important first
- Hardware-verified claims say so explicitly

## Fixes
- User-visible fixes only (omit this chapter entirely for a first release —
  never leave it empty)

## Install
​```sh
chmod +x OBSBOT4Linux-x86_64.AppImage
./OBSBOT4Linux-x86_64.AppImage
​```
Hardware-validated on an OBSBOT Tiny 3 (fw X.X.X.X). To build from source instead, see [docs/INSTALL.md](https://github.com/vampyren/obsbot4linux/blob/main/docs/INSTALL.md).

## Bundled SDK notice
This AppImage bundles OBSBOT's proprietary `libdev.so` SDK for out-of-the-box convenience; the SDK is excluded from the source repository. If you are OBSBOT or object to this distribution, contact vampyren@protonmail.com and it will be removed promptly. Building from source never redistributes the SDK.

---
*Not affiliated with or endorsed by OBSBOT. Licensed EUPL-1.2.*
*Version numbers bump on every development round; public releases are cut when a feature branch merges.*

**Full changelog**: https://github.com/vampyren/obsbot4linux/compare/vPREV...vTHIS
```

## Release checklist

1. Feature branch approved by hardware testing → merge `--no-ff` into `main`
   with `Closes #N` lines; push.
2. Tag: `git tag -a vX.Y.Z -m "OBSBOT4Linux vX.Y.Z — headline"`; push the tag.
3. Verify `dist/OBSBOT4Linux-x86_64.AppImage` is built FROM the merged commit
   (check the packed version string: `strings -e l` on the extracted binary)
   and passes `--self-test`.
4. `gh release create vX.Y.Z dist/OBSBOT4Linux-x86_64.AppImage` with the title
   and body per the template above.
5. Delete the feature branch (local + remote).
