# amicpp

![C++14](https://img.shields.io/badge/C%2B%2B-14-blue.svg)
![CMake](https://img.shields.io/badge/CMake-%3E%3D%203.16-brightgreen.svg)
![License: BSL-1.0](https://img.shields.io/badge/License-BSL--1.0-lightgrey.svg)
[![CI](https://github.com/daniele77/amicpp/actions/workflows/ci.yml/badge.svg)](https://github.com/daniele77/amicpp/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/daniele77/amicpp)](https://github.com/daniele77/amicpp/releases)
[![Issues](https://img.shields.io/github/issues/daniele77/amicpp)](https://github.com/daniele77/amicpp/issues)
[![Stars](https://img.shields.io/github/stars/daniele77/amicpp?style=social)](https://github.com/daniele77/amicpp/stargazers)

> GitHub repository: `daniele77/amicpp`.

A modern C++14 library to connect to Asterisk Manager Interface (AMI) over TCP using Boost.

## Features

- Boost.Asio TCP client for AMI transport.
- RAII session management (`Login`/`Logoff`).
- Object-oriented API for AMI commands and responses.
- Modular event callback system for AMI events.
- CMake build and install support.

## Requirements

- CMake >= 3.13
- C++14 compiler
- Boost (component: `system`)
- Threads (`pthread` on Linux)

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j$(nproc)
```

## Local Boost setup (no system install)

You can build and use Boost in a local directory, keeping multiple versions side-by-side.

Current project requirement: **Boost >= 1.66** (`Boost.System` component).

### 1) Download and install Boost locally

```bash
mkdir -p ~/src ~/opt/boost
cd ~/src
wget https://archives.boost.io/release/1.86.0/source/boost_1_86_0.tar.gz
tar xf boost_1_86_0.tar.gz
cd boost_1_86_0

./bootstrap.sh --prefix=$HOME/opt/boost/1.86.0
./b2 install --with-system -j$(nproc)
```

### 2) Configure `amicpp` to use local Boost

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
  -DBoost_ROOT=$HOME/opt/boost/1.86.0 \
    -DBoost_NO_SYSTEM_PATHS=ON
cmake --build build -j
```

### 3) Switch between Boost versions

- Install each version in its own directory, for example:
    - `~/opt/boost/1.82.0`
    - `~/opt/boost/1.86.0`
- Re-run CMake with the desired `BOOST_ROOT`.
- For cleaner isolation, use one build folder per Boost version (for example `build-boost182`, `build-boost186`).

## Install

```bash
cmake --install build --prefix /usr/local
```

## API docs (Doxygen)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DAMICPP_BUILD_DOCS=ON
cmake --build build --target amicpp_docs
```

Generated HTML docs are written under `build/docs/html`.

## Quick start

```cpp
#include <amicpp/ami_client.hpp>
#include <amicpp/ami_session.hpp>

int main() {
    amicpp::AmiClient client;
    client.connect("127.0.0.1", "5038");

    auto handler_id = client.add_event_handler([](const amicpp::AmiMessage& ev) {
        if (ev.has("Event")) {
            std::cout << "AMI Event: " << ev.get("Event") << "\n";
        }
    });

    {
        amicpp::AmiSession session(client, "admin", "supersecret");

        amicpp::AmiMessage action;
        action.set("Action", "Ping");
        const auto response = client.send_action(action);

        std::cout << "Ping Response: " << response.get("Response", "Unknown") << "\n";
    }

    client.remove_event_handler(handler_id);
    client.disconnect();
    return 0;
}
```

See the runnable example in `examples/basic_client.cpp`.

## Status

Project bootstrap release (`0.1.0`), intended as a clean foundation for further AMI command modules.

## Releases

- Tag format: `vMAJOR.MINOR.PATCH` (example: `v0.2.0`).
- Pushing a version tag triggers GitHub release publication via `.github/workflows/release.yml`.
- See `RELEASING.md` for the full release checklist.

## License

Licensed under the Boost Software License 1.0. See [LICENSE](LICENSE).
