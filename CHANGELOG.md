# Changelog

All notable changes to this project will be documented in this file.

## [0.1.0] - 2026-04-09

### Added

- Initial project structure and CMake build.
- `TcpClient` based on Boost.Asio for TCP communication.
- `AmiClient` for AMI command/response and event dispatching.
- `AmiSession` RAII login/logout wrapper for AMI sessions.
- Basic example client.
- GitHub CI workflow and automated tag-based release workflow.
- Optional Doxygen target (`amicpp_docs`) for API documentation.
