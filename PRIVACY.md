# Privacy policy

**Mupen64Plus-AE Turnip** (shown on your device as "M64P Turnip")

Last updated: 28 October 2026

## In short

- The app shows no ads. It has no analytics and no crash reporting.
- The maintainer runs no server. The app sends nothing to the maintainer.
- Some features send data to other services: RetroAchievements, Google Drive and GitHub,
  which also serves the cover art. Release V339 instead gets cover art from a server at
  `zurita.me`, and uses a netplay server there too. Cover art is on by default.
  RetroAchievements and Google Drive backup are off until you turn them on. Each feature is
  described below.
- The app keeps your settings, saves and sign-in details on your device. They leave it only
  for the features below, for example when Drive backup uploads your saves.

## Who publishes the app

The maintainer, the GitHub user [pwnedbygary](https://github.com/pwnedbygary), publishes the
app at <https://github.com/pwnedbygary/mupen64plus-ae-turnip>. The app is a free Nintendo 64
emulator for Android, and a fork of Mupen64Plus-AE. Its source code is public, under the GNU
General Public License version 3. Its package name is `org.mupen64plusae.turnip.pwnedbygary`.

To contact the maintainer about privacy, start a discussion at
<https://github.com/pwnedbygary/mupen64plus-ae-turnip/discussions>. Discussions are public,
so do not post passwords, tokens or other private details there.

## Which releases this policy covers

This policy covers release V339 and every later release at
<https://github.com/pwnedbygary/mupen64plus-ae-turnip/releases>. It also covers the test
builds that the repository's GitHub workflow makes. Their package name ends in `.debug`, and
they keep their own, separate data. A test build made from the current code works as the
rest of this policy describes.

### How release V339 differs

V339 was published on 15 October 2026. It was built from commit `d9779061b`. Its version
name starts with 3.0.338. The rest of this policy describes the current code, from which the
next release and the test builds are made. V339 differs from it in these ways:

- It identifies itself to RetroAchievements as `Mupen64PlusAE/3.0 (Android)`. The current
  code sends `Mupen64Turnip/`, the app version and the Android version, for example
  `Mupen64Turnip/3.0.340 (Android 13)`.
- It keeps your RetroAchievements password on the device after you sign in, and sends the
  password to sign in at the start of each game. The current code signs in with the session
  token first, and deletes the password when a sign-in returns a session token.
- Its log can contain the e-mail address of your Google account, when you sign in for
  Google Drive backup. During netplay, the log can contain IP addresses (the host's address,
  your device's local and public addresses, and your router's address) and the host's device
  name. A log that you share from V339 contains them. The current code does not write them
  to the log.
- It declares four more permissions: `AD_ID` (the advertising ID), `CHECK_LICENSE` (Google
  Play licensing), `ACCESS_WIFI_STATE` and `BLUETOOTH_CONNECT`. No code in V339 uses the
  first three, and V339 never asks for the fourth. The current code declares none of them.
- It does not keep a file of the RetroAchievements unlocks that it could not send yet.
- It keeps no RetroAchievements progress files. Instead, it keeps one copy of each game's
  achievement progress in its private settings.
- During netplay, a device that joins a game sends the host the name of each save file, not
  only the file's extension.
- When it joins a netplay game, it keeps looking for rooms on your local network for the
  whole game, and connects to each new room that it finds.
- It downloads cover art over plain HTTP, not HTTPS, from `coverart.zurita.me`. The app got
  this address from Francisco Zurita's commits to his fork of Mupen64Plus-AE, on which this
  app is based, and the domain carries his name. Anyone who can see your network traffic, for
  example on public Wi-Fi, can see which covers it downloads. No privacy policy covers this
  server, and how long it keeps its logs is unknown. It is hosted by DigitalOcean, LLC (US),
  which lists the server's address in Clifton, New Jersey, US. The current code downloads
  cover art over HTTPS from GitHub (see "Cover art (on by default)").
- Its online netplay codes go through a server, `np.zurita.me` (port 37520). When you host
  and tap **Get Code**, the app registers your room there. The server sees your public IP
  address, gives your address and your room's port to anyone who enters your code, and is
  told when the game starts. When you join with a code, the app sends the code to the server
  and receives the host's IP address and port. The app got this address from Francisco
  Zurita's commits, as it got the cover-art server's, and the two servers have the same IP
  address. No privacy policy covers this server, and how long it keeps its logs is unknown.
  V339's log can also contain the code of a room that you host. The current code makes codes
  on your device, without a server (see "Online netplay codes").
- It contains a library, `libhidapi.so`, that the app never uses. Builds of the current code
  leave it out.

## What the app does not do

- It shows no ads.
- It contains no analytics, crash-reporting or advertising code. This code was removed on
  18 August 2026, before release v335.
- It does not read your advertising ID, Android ID or IMEI, and it does not collect or share
  your hardware serial number.
- It does not ask for your location, camera, microphone, contacts or phone.
- It has no user accounts of its own. The maintainer runs no server, and the app sends
  nothing to the maintainer.

## What leaves your device, feature by feature

Each connection to the internet shows your IP address to the server that it reaches.

### RetroAchievements (off by default)

RetroAchievements is a service that tracks achievements in classic games. The app uses it
only if you turn on **Enable RetroAchievements** in the **RetroAchievements** settings and
sign in with your RetroAchievements account. Then it connects when you sign in from those
settings, and each time you start a game.

The app sends this over HTTPS to `retroachievements.org`:

- your RetroAchievements user name, with your password or your session token, to sign in;
- when you start a game: an MD5 hash that identifies the game file, and whether you play in
  hardcore or casual mode;
- while you play: the game's status line ("rich presence"), 30 seconds after the game loads
  and then every 2 minutes;
- each achievement that you unlock, and your leaderboard scores (hardcore mode only);
- the app version and your Android version, in the User-Agent of each request.

Netplay games always run in casual mode.

RetroAchievements receives this data. Its website is served through Cloudflare, Inc., a US
company, so your requests go through Cloudflare's network first. RetroAchievements does not
publish where its servers are or how long it keeps this data. Its
[Legal & Terms](https://retroachievements.org/terms) page says that it keeps personal
information "as long as necessary", and API usage logs "for a period deemed appropriate".

Your RetroAchievements profile is public. Other people can see, for example, your status
line, your last game and your recent unlocks.

The app does not download badge or avatar images from RetroAchievements.

### Cover art (on by default)

When you scan for games with **Download cover art** ticked, the app asks GitHub for a cover
image for each game in your library that has no cover yet, including games that earlier
scans found. The box is ticked by default. The app also asks GitHub for a game's cover,
without asking you, when another app (for example a game frontend) opens a game file with
this app and the game is not in your library yet. No setting turns off this second case.

The app sends:

- the game's name, as part of the image's web address;
- Android's default User-Agent, which contains your Android version, your device model and
  its build ID.

The images come over HTTPS from the maintainer's public repository on GitHub,
<https://github.com/pwnedbygary/mupen64plus-ae-turnip-coverart>, by way of
`raw.githubusercontent.com`. GitHub, Inc. (US), not the maintainer, receives these requests,
under the
[GitHub General Privacy Statement](https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement).
GitHub shows anyone with push access to a repository, such as the maintainer, its traffic of
the past 14 days: the numbers of visitors and of full clones, the referring sites and the
popular content
([Viewing traffic to a repository](https://docs.github.com/en/repositories/viewing-activity-and-data-for-your-repository/viewing-traffic-to-a-repository)).
GitHub's documentation does not say whether downloads through `raw.githubusercontent.com`
count in this traffic.
Release V339 gets cover art from another server (see "How release V339 differs").

To stop downloads during a scan, untick **Download cover art** on the scan screen.

### Online netplay codes

Online netplay lets you play with someone over the internet with a code. The app makes and
reads codes on your device, without a server.

- When you host and tap **Get Code**, the app asks your router to open ports for the game,
  and asks the router for its public IP address (see "Port mapping on your router"). It then
  shows a code made from that address and your room's port, with the address and port below
  it. The code is your public IP address and port written another way: anyone who has your
  code can work out your address.
- When you join with a code, the app reads the host's IP address and port from the code and
  connects to the host directly.

When the app cannot make a code, for example when automatic port mapping is off, it shows no
code, and it may ask you to share your public IP address and your room's port with the other
players yourself. Release V339 uses a server for codes instead (see "How release V339
differs").

### Playing with other people (netplay)

In a netplay game, local or online, the players' devices connect to each other directly.
The other players receive:

- your device name: the name set in Android's settings, or else the Bluetooth name, or else
  the device model;
- your IP address;
- from the host: an MD5 hash of the game, the names of the host's video and RSP plugins, and
  its ports;
- save-file data and settings for the game, and your controller input.

When you open a room to host a game, local or online, the app announces a room named
"M64PlusAE" on your local network, with your device's local IP address and port. It repeats
the announcement while the room is open. When you join a local game, the app listens for
these announcements.

### Port mapping on your router

When you host an online game and tap **Get Code**, the app asks your router to open three
ports for the game. It uses NAT-PMP, or UPnP if NAT-PMP fails. It also asks the router for
its public IP address, to make the code. With UPnP, the mappings are named "M64Plus Room",
"M64Plus Core TCP" and "M64Plus Core UDP". A mapping lasts 12 hours.
With UPnP, it has no time limit if your router accepts only permanent mappings. The app
removes the mappings when the netplay session ends. If the app stops before that, they can
stay until they expire or until you remove them in your router's settings.

This is on by default. To turn it off, untick **Automatically map external ports** in the
**Netplay** settings.

### Google Drive backup (off by default)

This feature copies your game saves to your own Google Drive.

- To use it, sign in with **Sign in to Google drive** in the **Data** settings. Google
  Play services handles the sign-in. The app asks for your basic profile, your e-mail
  address, and access only to the Drive files that it creates or that you open with it.
- When **Upload to Google Drive** is on, the app uploads a game's saves each time you exit
  the game. It uploads the game's save folder (battery saves, save states and
  RetroAchievements progress files) and the save files named after the game, to a folder
  named "M64Plus FZ" in your Drive. It deletes the older copies there first. Uploads wait for
  Wi-Fi or another unmetered network, unless **Backup over cell data** is on.
- You can also upload or download the backup of one game by hand, in that game's settings.

Google receives these files and your sign-in, under the
[Google Privacy Policy](https://policies.google.com/privacy). Google says that it keeps
servers around the world. Your files stay in your Drive until you delete them.

**Sign out of Google drive** does not remove the app's access to your Drive. To remove it,
use the [third-party connections page](https://myaccount.google.com/connections) of your
Google Account.

Google sign-in works only if Google has a client registered for the app's package name and
signing key. The maintainer has not confirmed that sign-in works in the published releases.
If you cannot sign in, the app sends nothing to Google Drive.

### GPU drivers and the update check (GitHub)

- When you pick one of six public driver projects in the GPU driver downloader
  (**Download driver…**), the app asks GitHub for that project's latest releases. When you
  then pick a driver, the app downloads it from GitHub.
- Each time you open the **Display** settings, the app checks GitHub for newer versions of
  the drivers that you downloaded before. If you downloaded none, it sends nothing.
- When you tap **Check for updates**, the app asks GitHub for the latest release of this
  app.

These requests carry no account details. GitHub sees your IP address and the User-Agent
`Mupen64PlusAE-Turnip`. GitHub, Inc. (US) receives them under the
[GitHub General Privacy Statement](https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement).
GitHub says that it stores data in "your local region, the United States, and other
countries".

### Links

Links to help, bug reports, releases, credits, game wiki pages, this privacy policy and the
licenses page open in your web browser.
The app gives the browser only the address.

### The app's log and hardware info

The app writes diagnostic messages to Android's system log on your device. It does not send
them anywhere by itself. The **Logcat** screen shows the app's log and has a **Share**
button. The **Hardware info** screen shows details about your device: its processor (with
the serial number removed), model, manufacturer and build, and your input devices, with their
names and Android's descriptors for them. It also has a **Share** button.

If you share either one, it goes to the app that you choose. The app does not write your
Google e-mail address, IP addresses or device names to the log, except in V339 (see above).
The log can contain the file paths of your games and MD5 hashes of games.

### Android TV home screen

On Android TV, the app adds a channel with your games' titles and cover art to the home
screen. This stays on the device.

## Where the services are and how long they keep data

| Service | Used for | Location | How long it keeps data |
|---|---|---|---|
| RetroAchievements | Achievements | Served through Cloudflare, Inc. (US). RetroAchievements does not publish where its servers are. | Not published. Its terms say "as long as necessary", and API usage logs "for a period deemed appropriate". |
| `coverart.zurita.me` and `np.zurita.me`, in release V339 only (addresses from Francisco Zurita's commits) | Cover art and online netplay codes in release V339 | DigitalOcean, LLC (US); DigitalOcean lists the address in Clifton, New Jersey, US | Unknown. No privacy policy covers these servers. |
| Google (sign-in and Drive) | Drive backup | "servers around the world" (Google) | Under Google's policy. Your backup files stay in your Drive until you delete them. |
| GitHub | Cover art; GPU drivers; update check | "your local region, the United States, and other countries" (GitHub) | Under GitHub's privacy statement. |
| Other players; your router | Netplay | Direct connections, no server | Not applicable |
| The maintainer | Nothing | No server | Keeps no data from the app |

The maintainer keeps no telemetry and no personal data from the app.

## What stays on your device

The app keeps these in its private storage, which other apps cannot read:

- Your settings. They include your RetroAchievements user name and session token, and your
  RetroAchievements password until a sign-in returns a token. They are not encrypted.
- Your game saves, save states and automatic saves. You choose how many automatic saves to
  keep for each game, from 0 to 10.
- RetroAchievements progress files, saved with each save state.
- On a device updated from V339, the copy of each game's achievement progress that V339 kept
  in its private settings. The current code neither reads nor deletes it. Clearing the app's
  storage removes it.
- A list of RetroAchievements unlocks and leaderboard scores that could not be sent yet. Each
  entry holds its type, the achievement or leaderboard ID, whether the unlock was made in
  hardcore mode or the score, the time, the game hash and your user name, but no password or
  token.
  An entry is removed when the server accepts or refuses it. The list keeps at most 256
  entries.
- Cover art, controller profiles, and the GPU drivers that you downloaded, each with a note
  of the project and release that it came from.

Some data can be outside private storage:

- Screenshots: on Android 9 and older, they go to the shared folder `Pictures/mupen64plus`,
  which other apps can read. On Android 10 and later, they stay in the app's private cache.
- If you choose an external folder for game data in the **Data** settings, the app also
  copies your saves there.
- Files from older versions of the app can be in the `mupen64plus-fz` folder of your shared
  storage.

Google Play services, not the app, keeps your Google sign-in.

The app turns off Android's cloud backup of its data. On some devices, Android can still
copy app data when you move to a new device.

## How to delete your data

- To delete everything that the app keeps in its private storage, clear the app's storage in
  Android's settings, or uninstall the app. When you uninstall, Android can ask whether to
  keep the app's data. Choose not to keep it.
- Uninstalling does not delete the external game-data folder that you chose, screenshots in
  `Pictures/mupen64plus` (Android 9 and older), or the `mupen64plus-fz` folder from older
  versions. Delete them with a file manager.
- The app has no RetroAchievements sign-out button. Clearing the app's storage removes your
  RetroAchievements user name, session token and any saved password.
- Other services keep their copies. Delete your backups in Google Drive, remove the app's
  access in your Google Account, and manage your RetroAchievements account at
  retroachievements.org.

## Permissions

These are the permissions that the app declares:

| Permission | Why |
|---|---|
| `INTERNET` | The online features above |
| `ACCESS_NETWORK_STATE` | To wait for Wi-Fi before Drive uploads, and to find your router for port mapping |
| `READ_EXTERNAL_STORAGE`, `WRITE_EXTERNAL_STORAGE` | Asked for only on Android 10 and older, to read games and write files in shared storage |
| `VIBRATE` | Rumble |
| `BLUETOOTH` | To use the Bluetooth name as your netplay device name, on Android 11 and older |
| `CHANGE_WIFI_MULTICAST_STATE` | To announce and find local netplay rooms |
| `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_CONNECTED_DEVICE`, `FOREGROUND_SERVICE_DATA_SYNC` | To keep the emulator, netplay, game scans, file tasks and Drive transfers running, with a notification |
| `POST_NOTIFICATIONS` | Asked for on Android 13 and later, to show those notifications |
| `RECEIVE_BOOT_COMPLETED` | To refresh the Android TV home-screen channel after a restart |
| `READ_EPG_DATA`, `WRITE_EPG_DATA` | The Android TV home-screen channel |

AndroidX Core, a library that the app uses, adds one more permission to the finished app:
`<package name>.DYNAMIC_RECEIVER_NOT_EXPORTED_PERMISSION`. Its protection level is
"signature", so only apps signed with the same key as this app can hold it.

## Other places where you might share data

If you report a problem or ask a question in the repository's GitHub issues or discussions,
what you post is public. GitHub handles it under the
[GitHub General Privacy Statement](https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement).
Read a log before you post it.

## Children

The maintainer collects no personal data from anyone, including children. The online
services named above have their own rules about children. Read them before a child uses
those features.

## Changes to this policy

When the app's handling of data changes, this file is updated and the date at the top
changes. Each earlier version stays in the repository's history.

## Contact

<https://github.com/pwnedbygary/mupen64plus-ae-turnip/discussions>
