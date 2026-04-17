# Releasing

This project follows semantic versioning (`MAJOR.MINOR.PATCH`).

## Release flow

1. Update `CHANGELOG.md` with the new version section.
2. Ensure CI is green on main branch.
3. Create and push a signed (or annotated) tag:

```bash
git tag -a v0.2.0 -m "amicpp v0.2.0"
git push origin v0.2.0
```

4. GitHub Actions workflow `Release` publishes a GitHub release automatically.
5. Optionally, publish binaries/packages from release artifacts.

## Versioning rules

- `MAJOR`: incompatible API changes.
- `MINOR`: backward-compatible features.
- `PATCH`: backward-compatible bug fixes.
