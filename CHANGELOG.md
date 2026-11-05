# Changelog

All notable changes to this fork are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Versioned release packages for Linux and Windows, built by CI.
- `qal-hard-linked` build option to link OpenAL directly. The Windows packages use
  it, so the OpenAL sound engine (`s_enable 2`) works without installing OpenAL.
