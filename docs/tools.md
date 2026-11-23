# Which Windows 11 command line tools does Command make?

Measured on 2026-11-23 on Windows 11 build 26300: 277 console programs in `System32` and `System32\wbem`, plus `chcp`, `format`, `mode`, `more` and `tree`. Many are services, installers or other makers' programs. Each tool is written from its documented behaviour, with the same name, switches and exit codes. Nothing of Microsoft's is copied, and help text is our own.

A tool is here when it works on plain Windows through public calls. It belongs to Impossible OS when it changes the boot, disks, drivers, security policy or system files, because only the OS knows how it stores them. Built-in words of `cmd` (`dir`, `copy`, `cd` and so on) belong to the OS's shell.

## Here, by what they need

| Needs | Tools |
| --- | --- |
| Tier 0, files and text | `attrib` `choice` `clip` `comp` `compact` `expand` `fc` `find` `findstr` `forfiles` `help` `makecab` `more` `replace` `robocopy` `sort` `subst` `tar` `timeout` `tree` `where` `xcopy` |
| Tier 0 and new calls, system information | `doskey` `driverquery` `hostname` `openfiles` `query` `qprocess` `quser` `setx` `systeminfo` `taskkill` `tasklist` `tskill` `tzutil` `waitfor` `whoami` |
| Registry and services | `reg` `regini` `sc` `schtasks` `shutdown` `logoff` `msg` `runas` |
| Permissions and hashes | `cacls` `certutil` (hash, encode and decode only) `cipher` `icacls` `takeown` |
| Network, tier 4 | `arp` `curl` `finger` `ftp` `getmac` `ipconfig` `nbtstat` `net` `netstat` `nslookup` `pathping` `ping` `route` `tracert` |
| Logs and measurements | `eventcreate` `logman` `relog` `tracerpt` `typeperf` `wevtutil` |
| Small | `chcp` `cmdkey` `edit` `label` `mode` `print` |

Already built, our own: `thash` `thex` `tfind` `ttree` `tcmp`.

## In Impossible OS

`auditpol` `bcdboot` `bcdedit` `bootsect` `chkdsk` `chkntfs` `convert` `defrag` `diskpart` `diskraid` `dism` `drvinst` `fltmc` `format` `fsutil` `gpresult` `gpupdate` `manage-bde` `mbr2gpt` `mountvol` `netsh` `pnputil` `powercfg` `reagentc` `recover` `repair-bde` `secedit` `sfc` `tpmtool` `verifier` `vssadmin` `w32tm` `wbadmin`

## Not made

Services and setup helpers, other makers' programs, Windows product activation and licensing, domain and Remote Desktop server tools, App-V and UE-V, Hyper-V host tools, `wsl`, and the script hosts `cscript` and `bash`.

## What has to happen first

The calls beyond tier 0 are added to Impossible SDK's list one group at a time, with the first tool that needs them. `ping` and `tracert` need the ICMP calls of `iphlpapi`, which work without administrator rights.

## In what order?

1. `ping` `tracert` `ipconfig` `netstat` `nslookup`
2. `find` `findstr` `sort` `more` `fc` `where` `tree` `xcopy` `robocopy`
3. `tasklist` `taskkill` `whoami` `systeminfo` `hostname` `shutdown`
4. `reg` `sc` `schtasks` `icacls`
5. The rest
