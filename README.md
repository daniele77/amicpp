# amicpp

![C++14](https://img.shields.io/badge/C%2B%2B-14-blue.svg)
![CMake](https://img.shields.io/badge/CMake-%3E%3D%203.13-brightgreen.svg)
![License: BSL-1.0](https://img.shields.io/badge/License-BSL--1.0-lightgrey.svg)
[![CI](https://github.com/daniele77/amicpp/actions/workflows/ci.yml/badge.svg)](https://github.com/daniele77/amicpp/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/daniele77/amicpp)](https://github.com/daniele77/amicpp/releases)
[![Issues](https://img.shields.io/github/issues/daniele77/amicpp)](https://github.com/daniele77/amicpp/issues)
[![Stars](https://img.shields.io/github/stars/daniele77/amicpp?style=social)](https://github.com/daniele77/amicpp/stargazers)

> GitHub repository: `daniele77/amicpp`.

A modern C++14 library to connect to Asterisk Manager Interface (AMI) over TCP using Boost, supporting both synchronous and asynchronous APIs.

## Features

- Boost.Asio AMI transport with both synchronous and asynchronous APIs.
- External `boost::asio::io_context` provided by the application.
- Sync API (`connect`, `disconnect`, `send_action`) and async API (`async_connect`, `async_disconnect`, `async_send_action`).
- RAII session management (`Login`/`Logoff`) with internal async synchronization.
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
- Re-run CMake with the desired `Boost_ROOT`.
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

### Synchronous API

```cpp
#include <amicpp/ami_client.hpp>
#include <amicpp/ami_session.hpp>

#include <boost/asio.hpp>

int main() {
    boost::asio::io_context io_context;
    amicpp::AmiClient client(io_context);

    client.connect("127.0.0.1", "5038");

    {
        amicpp::AmiSession session(client, "admin", "secret");

        amicpp::AmiMessage action;
        action.set("Action", "Ping");

        const auto response = client.send_action(std::move(action));
        std::cout << response.get("Response", "Unknown") << "\n";
    }

    client.disconnect();
    return 0;
}
```

### Asynchronous API

```cpp
#include <amicpp/ami_client.hpp>
#include <amicpp/ami_session.hpp>

#include <boost/asio.hpp>

#include <thread>

int main() {
    boost::asio::io_context io_context;
    auto work_guard = boost::asio::make_work_guard(io_context);
    std::thread io_thread([&io_context] { io_context.run(); });

    amicpp::AmiClient client(io_context);

    client.async_connect("127.0.0.1", "5038", [&client](const std::string& result) {
        if (result.find("Connect error:") != 0 && result.find("Banner read error:") != 0) {
            std::cout << "Connected: " << result << "\n";

            auto handler_id = client.add_event_handler([](const amicpp::AmiMessage& ev) {
                if (ev.has("Event")) {
                    std::cout << "Event: " << ev.get("Event") << "\n";
                }
            });

            amicpp::AmiMessage login;
            login.set("Action", "Login");
            login.set("Username", "admin");
            login.set("Secret", "secret");

            client.async_send_action(std::move(login), [&client, handler_id](bool login_ok, const amicpp::AmiMessage& login_resp) {
                if (!login_ok || login_resp.get("Response") != "Success") {
                    client.remove_event_handler(handler_id);
                    client.async_disconnect();
                    return;
                }

                amicpp::AmiMessage action;
                action.set("Action", "Ping");

                client.async_send_action(std::move(action), [&client, handler_id](bool success, const amicpp::AmiMessage& response) {
                    if (success) {
                        std::cout << "Ping: " << response.get("Response", "Unknown") << "\n";
                    }

                    amicpp::AmiMessage logoff;
                    logoff.set("Action", "Logoff");
                    client.async_send_action(std::move(logoff), [&client, handler_id](bool, const amicpp::AmiMessage&) {
                        client.remove_event_handler(handler_id);
                        client.async_disconnect();
                    });
                });
            });
        }
    });

    io_thread.join();
    return 0;
}
```

Runnable examples:

- `examples/basic_client.cpp`: dual mode demo (`--sync` / `--async`).
- `examples/async_client.cpp`: async-only callback chain (`Connect -> Login -> Ping -> Logoff`).
- `examples/sync_client.cpp`: sync-only flow using `connect`, `AmiSession`, and `send_action`.

## Run examples

```bash
# Dual mode example (default async mode)
./build-boost186/amicpp_basic_client 127.0.0.1 5038 admin supersecret

# Dual mode example in sync mode
./build-boost186/amicpp_basic_client --sync 127.0.0.1 5038 admin supersecret

# Async-only example
./build-boost186/amicpp_async_client 127.0.0.1 5038 admin supersecret

# Sync-only example
./build-boost186/amicpp_sync_client 127.0.0.1 5038 admin supersecret
```

## Status

Project bootstrap release (`0.1.0`), now with both synchronous and asynchronous APIs.

## Releases

- Tag format: `vMAJOR.MINOR.PATCH` (example: `v0.2.0`).
- Pushing a version tag triggers GitHub release publication via `.github/workflows/release.yml`.
- See `RELEASING.md` for the full release checklist.

## License

Licensed under the Boost Software License 1.0. See [LICENSE](LICENSE).
