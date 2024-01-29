# ARK Server Mirror
[![Discord](https://img.shields.io/discord/937821572285206659?style=flat-square&label=Discord&logo=discord&logoColor=white&color=7289DA)](https://discord.com/servers/teknology-hub-937821572285206659)

## Overview

ARK Server Mirror is a lightweight mirror for ARK: Survival Evolved dedicated server that impersonates an ARK server in Steam network while forwarding all information from parent server of choice

## Runtime dependencies

Windows:
+ steam_api64.dll from Steamworks SDK v1.32 and a steamclient64.dll (both can be found in ARK Dedicated server at `Engine\Binaries\ThirdParty\Steamworks\Steamv132\Win64`) in the same directory

Linux:
+ glibc v2.38 (build from source on your machine to link against different version)
+ libsteam_api.so from Steamworks SDK v1.32 and a steamclient.so (can be found in ARK Dedicated server at `Engine\Binaries\Linux` and `ShooterGame\Binaries\Linux` respectively) in one of the directories checked by system's dynamic loader

## Command-line arguments

+ `--gamePort <port>` - Game port to report to clients. Keep in mind that the app doesn't use that port in any way, it's implied that you forward it to parent server by other means
+ `--queryPort <port>` - The port to listen on for Steam queries
+ `--displayName "Name"` - Session name to be displayed in server browser
+ `--parentIP <IP>` - IP address of the parent server (the server that is being mirrored)
+ `--parentQueryPort <port>` - Query port of the parent server
+ `--appIdOverride <AppId>` - Steam App ID to use if you want it to be something different than 346110
+ `--updateInterval <seconds>` - The interval in seconds between queries to parent server to update info. The default is 10

## Notes

+ Despite not using game port the app still binds to the Steam port as Game port + 1
+ To gracefully shutdown the app send 4 bytes with the value of zero to app's query port from localhost (any program running on the same host)

## Features

+ Returns all information from the parent server including tags, rules and players as-is, only Steam ID, game port and session name are replaced with mirror's own
+ If the parent servers stops responding, so does the mirror to avoid confusion, and it automatically starts responding again when parent server does
+ Query requests are not 1:1 forwarded to parent server, instead the parent server is queried with the specified interval and everyone else gets copy of the information from last query, this way DDOS attacks are not forwarded
+ No external dependencies other than Steam API and using native OS APIs resulting in very small executable size and memory footprint
+ You can run several instances of the app using the same files without conflicts

## License

ARK Server Mirror is licensed under the [MIT](https://github.com/Nuclearistt/ARKServerMirror/blob/main/LICENSE) license.