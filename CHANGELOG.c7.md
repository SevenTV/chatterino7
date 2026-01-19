# Changelog

## Unversioned

- Major: Added experimental Kick support (#351, #353, #354, #355, #356, #357, #360, #362, #367, #368, #369, #371)
- Minor: Increased radius of drop-shadows in paints to match the browser extension (#339, a4a86c9ca6e1bccf9253dce75bdfe838c15e71fd)
- Dev: Bumped Qt to 6.9.3 on Windows and macOS due to CVE-2025-10728 and CVE-2025-10729 (74077350da2d4cfff2ede0ce0ebb11654253b440)
- Dev: Updated the macOS Homebrew bottles on x86_64 to Sonoma as Ventura was EOL'd 2025-Sep-15 (#336)
- Dev: Preemptively treat version comparisons between `7.x.x` and `2.y.y` as upgrades (#372)

## 7.5.4

- Bugfix: Fixed certain paints such as the new "Coder Socks" one not displaying correctly in Chatterino (b306149e0a8d55f99dc9641e47c521b2f1c404a2)
- Bugfix: Fixed emojis being inserted from shortcodes between words without spaces (some 7TV emotes couldn't be sent, see Chatterino/chatterino2#6356). (341af2f1e66fcc7572d6def4d4232c4cd7905d23)
- Dev: Bumped OpenSSL on Windows to 3.5.2 (d9bb47a3e009457120e650138e170a3ef7d1cc56)

## 7.5.4-beta.1

- Bugfix: Fixed paints not appearing as with the browser extension (1d945c3d2bb86c662cd001908549c4e852baa6cb)
- Dev: Bumped Qt to 6.9.1 on Windows and macOS (f1c50e43b60dcb0d6af44ddc16f23d1c54e4b639).

## 7.5.3

- Dev: Downgrades are no longer treated as updates and the `v3` endpoint is now used to check for updates (9a05dc994ae6bba41ccc541156313eac558a15d7)

## 7.5.3-beta.1

- Minor: Added 7TV Discord link to about section and changed the commit link to point to SevenTV/Chatterino7 (#305)
- Bugfix: Fixed special 7TV emote sets not being applied and not showing in chat (5cc89d3ba9c11b9e0d36be9ac6f14f852b8713dc)
- Bugfix: Opening the avatar of a user from a usercard in the browser now opens the correct URL (c8a096c868864e0a51911640534d3e3283298bda)
- Bugfix: If a user never set their 7TV avatar, the usercard will no longer show a button to switch between Twitch and 7TV (519a2c3174a96fc19cf59a87005dda07c344b04a)
- Bugfix: Fixed mentions becoming unclickable when a user updated/announced their personal emotes (7ed952b61073e3081ab1377d1429f554f71b6a07)
- Dev: Updated kimageformats to v6.11.0, Boost to 1.87.0, and OpenSSL to 3.4.1 (9b5f69f8b15ca63fec8e8ee7406f8dc745ec9dda, d56af0f79d227359111b73c83c9cc657d8ccc811)
- Dev: On Windows, Chatterino7 now uses `SevenTV.Chatterino7` as it's AUMID (ae8bbab7c83b71727f6f4554c93dbf85c3c2a335)

## 7.5.2-beta.1

- Minor: Added setting to disable animated 7TV badges (3bbcc9f4a01a015f93f7855be36312e56968ac5f)
- Minor: Added setting to disable 7TV paint shadows (689235b28af59e950987ede595c29bc26f8c8776)
- Bugfix: Paints are no longer rendered in system messages (b55e723fcea9520f47b1a6bf786a52011ffc7289)
- Bugfix: Emotes now properly use the AVIF version if it's available and supported (9776bb03d9adb0021d452eb468379f3ee27d9bd9)
- Bugfix: Fixed 7TV avatars not loading in the usercard (f9a18c1b812cb8e036fe918cdf00eb4876114f4d)
- Dev(macOS): Changed CFBundleIdentifier from `com.chatterino` to `app.7tv.chatterino7` (fec0dbdf558b1e6e358971a256f5540d34bb6a8d)
- Dev: Updated Conan version used in CI to 2.4 (330d05d50ffd296b34744dbcc97290534e8cf704)
- Dev(Windows): Updated `libavif` to 1.0.4, `boost` to 1.85, and `openssl` to 3.2.2 (330d05d50ffd296b34744dbcc97290534e8cf704)
- Dev(macOS): A single universal app is now released for macOS (#274, #279)
- Dev: Refactored paints to avoid creation of intermediate widgets (#277)
- Dev(macOS): The minimum required macOS version is now 13.0 Ventura (5e4a9c54e1ef369bb033ab32759019d324c03c85)
- Dev: The client version is now sent through URL parameters to the 7TV EventAPI - previously, this was only sent in the `User-Agent` (1ad27c58fa4745cabf0358055df11be05cfe659a)
