# obs-shoutout-manager

An OBS Studio plugin that adds a dockable **Shoutout Queue** panel. Authenticate with Twitch once; then queue up streamer names from OBS or directly from chat — the plugin handles the rest, respecting Twitch's 2-minute global cooldown and 1-hour per-streamer cooldown automatically.

## Features

- **`!bso` chat command** — broadcasters and mods queue shoutouts directly from Twitch chat. Supports multiple names at once: `!bso name1 name2 name3`
- **OBS dock queue** — names can also be added manually from the dock panel inside OBS
- **OAuth PKCE auth** — no client secret in the binary; each user signs in via their browser
- **Persistent session** — tokens survive OBS restarts
- **Smart queue** — skips users on cooldown, pauses automatically when stream is offline, retries after token expiry
- **Chat confirmation** — posts `Shoutout sent to @username!` in chat after each successful shoutout
- **EventSub WebSocket** — uses Twitch's current chat API (not deprecated IRC)
- **Cross-platform** — Windows x64, macOS Universal (arm64 + x86_64), Linux x86_64

## Installation

1. Download the zip for your platform from the [Releases](../../releases) page.
2. Extract into your OBS plugins folder:

| Platform | Path |
|----------|------|
| Windows | `C:\Program Files\obs-studio\obs-plugins\64bit\` |
| macOS | `~/Library/Application Support/obs-studio/plugins/` |
| Linux | `~/.config/obs-studio/plugins/` |

3. Restart OBS. The **Shoutout Manager** dock will appear under **View → Docks**.

## Usage

1. Open OBS and find the **Shoutout Manager** dock (**View → Docks → Shoutout Manager**).
2. Click **Connect Twitch Account** — your browser opens to Twitch's auth page.
3. Approve the permissions and return to OBS. The dock shows your username and a green **Chat connected** indicator.
4. Queue shoutouts in either of two ways:
   - **From chat:** a broadcaster or mod types `!bso username` (or `!bso name1 name2 name3` for multiple)
   - **From the dock:** type a username into the text field and press **Add** or Enter
5. The plugin processes the queue automatically, sending each shoutout and posting a confirmation in chat.

### Cooldown rules

| Rule | Duration |
|------|----------|
| Global (Twitch limit) | 125 seconds between any two shoutouts |
| Per-streamer | 60 minutes before the same channel can be shouted out again |

Shoutouts queued while offline will be held and processed automatically once you go live.

### Who can use `!bso`?

Only the **broadcaster** and **moderators**. Regular viewers typing `!bso` are silently ignored.

---

## Twitch App Setup (for builders / distributors)

The plugin is built with a specific Twitch Client ID baked in at compile time. To build your own copy:

1. Go to the [Twitch Developer Console](https://dev.twitch.tv/console).
2. Create a new application:
   - **Name**: anything unique (e.g. `MyChannel Shoutout Manager`)
   - **OAuth Redirect URLs**: `http://localhost`
   - **Category**: Chat Bot
   - **Client Type**: Confidential
3. Copy the **Client ID** — you do not need the Client Secret (PKCE is used).
4. Add `TWITCH_CLIENT_ID` as a GitHub Actions secret in your fork, or pass it at configure time (see below).

## Building from source

### Prerequisites

- [CMake 3.16+](https://cmake.org/)
- OBS Studio development headers — fetched automatically via `buildspec.json`
- Qt 6.7+ (Core, Widgets, Network, WebSockets) — fetched automatically

> **Note:** This project requires the cmake helpers from [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate). Clone that template first, then replace its `src/`, `data/`, `CMakeLists.txt`, `buildspec.json`, and `.github/` with the files from this repo.

### Build commands

```bash
# Windows
cmake -B build --preset windows-x64 -DTWITCH_CLIENT_ID=your_client_id
cmake --build build --preset windows-x64 --config RelWithDebInfo

# macOS
cmake -B build --preset macos -DTWITCH_CLIENT_ID=your_client_id
cmake --build build --preset macos

# Linux
cmake -B build --preset linux-x86_64 -DTWITCH_CLIENT_ID=your_client_id
cmake --build build --preset linux-x86_64
```

### Automated builds via GitHub Actions

1. Fork this repo.
2. Add `TWITCH_CLIENT_ID` as a repository secret (**Settings → Secrets and variables → Actions**).
3. Push a version tag: `git tag v1.0.0 && git push --tags`
4. GitHub Actions builds all three platforms and publishes a release with downloadable zips.

---

## Required Twitch scopes

| Scope | Purpose |
|-------|---------|
| `moderator:manage:shoutouts` | Send shoutouts via the Helix API |
| `user:bot` | Post chat confirmation messages |
| `user:read:chat` | Receive chat messages via EventSub WebSocket (for `!bso`) |
