# 6ftp(1)

## Name
6ftp - Interactive FTP client.

## Synopsis
```
6ftp [-u user] [-p password] host [port]
```

## Duty
Connect to a remote FTP server over TCP, authenticate, and run an interactive
session for browsing directories and transferring files.

## Options
- `-u user` - Username to send with the initial `USER` command. If omitted,
  `6ftp` prompts on a tty and otherwise defaults to `anonymous`.
- `-p password` - Password to use for login. If omitted, `6ftp` prompts when
  needed on a tty. In non-interactive mode, non-anonymous logins must provide
  this option.

## Arguments
- `host` - Remote FTP server hostname or IPv4 address.
- `port` - Remote TCP port. Defaults to `21`.

## Commands
- `ls [path]` - List a remote directory using passive mode.
- `pwd` - Print the current remote working directory.
- `cd dir` - Change the remote working directory.
- `lcd dir` - Change the local working directory.
- `lpwd` - Print the local working directory.
- `get remote [local]` - Download a file.
- `put local [remote]` - Upload a file.
- `mkdir dir` - Create a remote directory.
- `rmdir dir` - Remove a remote directory.
- `rm path` - Delete a remote file.
- `rename old new` - Rename a remote file or directory.
- `binary` - Switch to `TYPE I`.
- `ascii` - Switch to `TYPE A`.
- `quote command` - Send a raw FTP command.
- `status` - Show connection state.
- `help` - Show the built-in command summary.
- `quit` - Close the control connection.

## Notes
- `6ftp` uses passive mode (`PASV`) for directory listings and file transfers.
- File uploads and downloads switch to binary mode automatically.
- When `stdin` is not a tty, commands can be piped into the session after
  connection and login complete.

## Examples
```
6ftp ftp.example.org
6ftp -u dakota ftp.example.org 2121
printf 'ls\nget README\nquit\n' | 6ftp -u anonymous ftp.example.org
```

## Source Audit
- Source file: user/6ftp.c
- Last updated: 2026-04-13