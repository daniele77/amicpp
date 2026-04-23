# Contributing

Thanks for your interest in contributing to `amicpp`.

## Development setup

1. Install a C++14 compiler and CMake >= 3.12.
2. Install Boost (at least `system`) and pthread support.
3. Configure and build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Guidelines

- Keep changes focused and minimal.
- Preserve API stability when possible.
- Update documentation (`README.md`, `CHANGELOG.md`) for user-visible changes.
- Ensure code compiles with `-std=c++14`.

## Pull requests

- Describe the problem and the solution.
- Include reproduction steps for bug fixes.
- Link related issues when available.
