# Shell Commands & Concepts for AOSP Developers
### From Android App Dev to AOSP System Dev — 500 Commands & Concepts

> Written for: 5+ years Android app dev experience, learning AOSP system layer.
> Context: AOSP 15 / android-15.0.0_r14 / Raspberry Pi 5 / Ubuntu host.
> Think of this as your Android Studio → Terminal transition guide.

---

## Table of Contents

1. [Shell Basics & Navigation](#1-shell-basics--navigation)
2. [File & Directory Operations](#2-file--directory-operations)
3. [File Viewing & Reading](#3-file-viewing--reading)
4. [Text Processing & Search](#4-text-processing--search)
5. [Permissions & Ownership](#5-permissions--ownership)
6. [Process Management](#6-process-management)
7. [Disk & Filesystem](#7-disk--filesystem)
8. [Archives & Compression](#8-archives--compression)
9. [Environment & Variables](#9-environment--variables)
10. [Networking](#10-networking)
11. [Git for AOSP](#11-git-for-aosp)
12. [Repo Tool (AOSP Multi-Git)](#12-repo-tool-aosp-multi-git)
13. [AOSP Build System](#13-aosp-build-system)
14. [Soong / Android.bp Concepts](#14-soong--androidbp-concepts)
15. [ADB — Advanced Usage](#15-adb--advanced-usage)
16. [Android Shell (adb shell) Commands](#16-android-shell-adb-shell-commands)
17. [Logcat — Advanced](#17-logcat--advanced)
18. [SELinux Tools & Debugging](#18-selinux-tools--debugging)
19. [Binder & Service Debugging](#19-binder--service-debugging)
20. [sysfs / procfs Navigation](#20-sysfs--procfs-navigation)
21. [Native Crash & Tombstone Analysis](#21-native-crash--tombstone-analysis)
22. [Performance & Memory Profiling](#22-performance--memory-profiling)
23. [Shell Scripting Essentials](#23-shell-scripting-essentials)
24. [Useful One-Liners for AOSP Work](#24-useful-one-liners-for-aosp-work)
25. [Concepts Glossary](#25-concepts-glossary)

---

## 1. Shell Basics & Navigation

> Coming from Android Studio: the terminal IS your IDE for AOSP. These are the foundations.

| # | Command | Explanation |
|---|---------|-------------|
| 1 | `pwd` | Print Working Directory — shows your current location. Like checking the breadcrumb in your file explorer. |
| 2 | `cd /path/to/dir` | Change Directory. Absolute path (starts with `/`). |
| 3 | `cd vendor/myoem` | Change to a relative path (relative to where you are now). |
| 4 | `cd ~` | Go to your home directory (`/home/arun`). |
| 5 | `cd -` | Go back to the previous directory. Toggle between two dirs. Very useful. |
| 6 | `cd ..` | Go up one directory level. |
| 7 | `cd ../..` | Go up two levels. |
| 8 | `ls` | List files and directories. |
| 9 | `ls -l` | Long listing: permissions, owner, size, date. |
| 10 | `ls -la` | Long listing including hidden files (files starting with `.`). |
| 11 | `ls -lh` | Long listing with human-readable file sizes (KB, MB, GB). |
| 12 | `ls -lt` | Sort by modification time, newest first. |
| 13 | `ls -lS` | Sort by file size, largest first. |
| 14 | `ls -R` | List recursively — all subdirectories too. |
| 15 | `ls vendor/myoem/` | List a specific directory without `cd`-ing into it. |
| 16 | `tree` | Visual tree of directory structure. Install: `sudo apt install tree`. |
| 17 | `tree -L 2` | Tree limited to 2 levels deep. Great for AOSP module structure overview. |
| 18 | `clear` | Clear the terminal screen. Shortcut: `Ctrl+L`. |
| 19 | `history` | Show command history (last ~1000 commands). |
| 20 | `history \| grep adb` | Search your command history for any `adb` command you ran before. |
| 21 | `!!` | Re-run the last command. Useful: `sudo !!` to re-run with sudo. |
| 22 | `!<n>` | Re-run command number `n` from history (e.g., `!42`). |
| 23 | `Ctrl+R` | Reverse search through command history. Start typing and it finds matches. |
| 24 | `Ctrl+C` | Cancel / interrupt the running command. |
| 25 | `Ctrl+Z` | Suspend (pause) the running command. Use `fg` to resume. |
| 26 | `fg` | Bring the suspended (Ctrl+Z) command back to foreground. |
| 27 | `bg` | Run the suspended command in the background. |
| 28 | `jobs` | List all background/suspended jobs in the current shell. |
| 29 | `exit` | Exit the current shell or SSH session. |
| 30 | `man <command>` | Manual page for a command (e.g., `man ls`). Press `q` to quit. |
| 31 | `<command> --help` | Quick help summary for a command. Shorter than `man`. |
| 32 | `which <program>` | Show the full path of a program (e.g., `which python3` → `/usr/bin/python3`). |
| 33 | `type <command>` | Show what type a command is (binary, alias, shell built-in). |
| 34 | `alias ll='ls -la'` | Create a shortcut. Add to `~/.bashrc` to make it permanent. |
| 35 | `source ~/.bashrc` | Reload `.bashrc` without restarting the terminal. Use after editing it. |
| 36 | `echo $PATH` | Show the directories the shell searches for commands. |
| 37 | `Tab` key | Auto-complete file names and commands. Press twice for all options. |
| 38 | `Ctrl+A` | Move cursor to beginning of line. |
| 39 | `Ctrl+E` | Move cursor to end of line. |
| 40 | `Ctrl+U` | Delete everything before the cursor on the current line. |

---

## 2. File & Directory Operations

| # | Command | Explanation |
|---|---------|-------------|
| 41 | `mkdir mydir` | Make a new directory. |
| 42 | `mkdir -p a/b/c` | Make directory and all parent directories at once. `-p` = parents. Essential for creating AOSP module paths. |
| 43 | `cp file.txt copy.txt` | Copy a file. |
| 44 | `cp -r srcdir/ destdir/` | Copy a directory recursively. `-r` = recursive. |
| 45 | `cp -a srcdir/ destdir/` | Copy preserving all attributes (permissions, timestamps, symlinks). Better than `-r` for system files. |
| 46 | `mv file.txt newname.txt` | Rename a file (move within same dir). |
| 47 | `mv file.txt /other/path/` | Move a file to another directory. |
| 48 | `rm file.txt` | Delete a file. **Permanent — no Recycle Bin.** |
| 49 | `rm -r mydir/` | Delete a directory and all its contents recursively. |
| 50 | `rm -rf mydir/` | Force delete without prompting. **Be very careful.** |
| 51 | `rmdir emptydir` | Remove an empty directory (safer than `rm -r`). |
| 52 | `touch newfile.txt` | Create an empty file, or update its timestamp if it exists. |
| 53 | `ln -s /path/to/real /path/to/link` | Create a symbolic (soft) link. Like a shortcut. Common in AOSP out/ directories. |
| 54 | `ln -sf /new/target /path/to/link` | Create/overwrite a symlink forcefully. |
| 55 | `readlink -f /path/to/link` | Resolve a symlink to its real path. |
| 56 | `realpath somefile` | Get the absolute real path of a file (resolves `..` and symlinks). |
| 57 | `file somefile` | Detect file type from its content (e.g., ELF 64-bit, ASCII text). |
| 58 | `stat somefile` | Show full file metadata: size, inode, permissions, timestamps. |
| 59 | `wc -l file.txt` | Count lines in a file. Useful: `wc -l $(find . -name "*.cpp")`. |
| 60 | `wc -c file.txt` | Count bytes in a file. |
| 61 | `diff file1 file2` | Show differences between two files. |
| 62 | `diff -u file1 file2` | Unified diff format (like git diff output). |
| 63 | `diff -r dir1/ dir2/` | Recursively diff two directories. |
| 64 | `cp /dev/null file.txt` | Empty/truncate a file without deleting it. |
| 65 | `truncate -s 0 file.txt` | Truncate a file to zero bytes. |

---

## 3. File Viewing & Reading

| # | Command | Explanation |
|---|---------|-------------|
| 66 | `cat file.txt` | Print entire file to terminal. Good for small files. |
| 67 | `cat -n file.txt` | Print with line numbers. |
| 68 | `cat file1 file2` | Concatenate and print multiple files. |
| 69 | `less file.txt` | Page through a large file interactively. Better than `cat`. Press `q` to quit, `/` to search, `n`/`N` to navigate matches. |
| 70 | `more file.txt` | Older pager, similar to `less` but less featured. |
| 71 | `head file.txt` | Show first 10 lines of a file. |
| 72 | `head -n 50 file.txt` | Show first 50 lines. |
| 73 | `tail file.txt` | Show last 10 lines of a file. |
| 74 | `tail -n 50 file.txt` | Show last 50 lines. |
| 75 | `tail -f logfile.txt` | Follow a file as it grows (like live log streaming). Essential for watching build logs. |
| 76 | `tail -f logfile.txt \| grep ERROR` | Follow log and filter in real time. |
| 77 | `xxd file.bin` | Hex dump of a binary file. Useful for inspecting ELF headers, binary configs. |
| 78 | `xxd -l 64 file.bin` | Hex dump of only first 64 bytes. |
| 79 | `od -c file.bin` | Octal dump showing characters. Useful to spot hidden characters in text files. |
| 80 | `strings file.so` | Extract human-readable strings from a binary. Great for checking what's compiled into a `.so`. |

---

## 4. Text Processing & Search

> These replace what you'd do with Ctrl+F, Find-in-Files, and RegEx in Android Studio.

| # | Command | Explanation |
|---|---------|-------------|
| 81 | `grep "pattern" file.txt` | Search for a pattern in a file. |
| 82 | `grep -r "pattern" .` | Recursive search in current directory and all subdirectories. |
| 83 | `grep -r "IThermalControlService" vendor/myoem/` | Find all files referencing a specific AIDL interface. |
| 84 | `grep -rn "pattern" .` | Recursive search with line numbers. |
| 85 | `grep -ri "pattern" .` | Recursive, case-insensitive search. |
| 86 | `grep -rl "pattern" .` | List only file names that contain the pattern (not the matching lines). |
| 87 | `grep -v "pattern" file.txt` | Show lines that do NOT match the pattern (invert). |
| 88 | `grep -E "pattern1\|pattern2" file.txt` | Match either pattern1 or pattern2 (extended regex). |
| 89 | `grep -A 3 "pattern" file.txt` | Show 3 lines After each match (context). |
| 90 | `grep -B 3 "pattern" file.txt` | Show 3 lines Before each match. |
| 91 | `grep -C 3 "pattern" file.txt` | Show 3 lines before and after (Context). |
| 92 | `grep "^service" file.rc` | Match lines that START with "service" (`^` = start of line). |
| 93 | `grep "\.cpp$" filelist.txt` | Match lines that END with ".cpp" (`$` = end of line). |
| 94 | `grep -c "pattern" file.txt` | Count how many lines match. |
| 95 | `grep --include="*.cpp" -r "pattern" .` | Recursive grep only in `.cpp` files. |
| 96 | `grep --exclude="*.o" -r "pattern" .` | Recursive grep excluding `.o` files. |
| 97 | `find . -name "Android.bp"` | Find all `Android.bp` files from current directory. |
| 98 | `find . -name "*.cpp"` | Find all C++ source files. |
| 99 | `find . -name "*.cpp" -newer reference.txt` | Find `.cpp` files newer than `reference.txt`. |
| 100 | `find . -type f -name "*.h"` | Find only regular files (not dirs) with `.h` extension. |
| 101 | `find . -type d -name "sepolicy"` | Find all directories named `sepolicy`. |
| 102 | `find . -size +1M` | Find files larger than 1 MB. |
| 103 | `find . -empty` | Find empty files and directories. |
| 104 | `find . -name "*.cpp" -exec grep -l "ThermalControl" {} \;` | Find `.cpp` files that contain "ThermalControl". |
| 105 | `sed 's/old/new/g' file.txt` | Replace all occurrences of "old" with "new" and print. |
| 106 | `sed -i 's/old/new/g' file.txt` | Replace in-place (modifies the file). `-i` = in-place. |
| 107 | `sed -n '10,20p' file.txt` | Print only lines 10 through 20. |
| 108 | `sed '/pattern/d' file.txt` | Delete lines matching a pattern. |
| 109 | `awk '{print $1}' file.txt` | Print the first column (whitespace-delimited). |
| 110 | `awk '{print $NF}' file.txt` | Print the last column (`NF` = number of fields). |
| 111 | `awk -F: '{print $1}' /etc/passwd` | Use `:` as delimiter, print first field. |
| 112 | `awk '/pattern/ {print $0}' file.txt` | Print lines matching a pattern (like grep but more powerful). |
| 113 | `cut -d: -f1 /etc/passwd` | Cut field 1 using `:` delimiter. Like `awk` but simpler. |
| 114 | `cut -c1-10 file.txt` | Cut characters 1 through 10 of each line. |
| 115 | `sort file.txt` | Sort lines alphabetically. |
| 116 | `sort -n file.txt` | Sort numerically. |
| 117 | `sort -r file.txt` | Sort in reverse order. |
| 118 | `sort -u file.txt` | Sort and remove duplicates. |
| 119 | `sort -k2 file.txt` | Sort by the 2nd column. |
| 120 | `uniq file.txt` | Remove consecutive duplicate lines (use after `sort`). |
| 121 | `uniq -c file.txt` | Count occurrences of each unique line. |
| 122 | `tr 'a-z' 'A-Z' < file.txt` | Translate lowercase to uppercase. |
| 123 | `tr -d '\r' < file.txt > fixed.txt` | Remove Windows carriage returns (`\r`) from a file. Common issue with cross-platform files. |
| 124 | `xargs` | Take input from stdin and pass as arguments (e.g., `find . -name "*.o" \| xargs rm`). |
| 125 | `tee file.txt` | Read from stdin and write to both file and stdout simultaneously (e.g., `make 2>&1 \| tee build.log`). |

---

## 5. Permissions & Ownership

> Critical for AOSP: wrong permissions = SELinux denials, service crashes, sysfs write failures.

| # | Command | Explanation |
|---|---------|-------------|
| 126 | `ls -la` | Show permissions in format: `drwxr-xr-x`. d=dir, r=read, w=write, x=execute. Three groups: owner, group, others. |
| 127 | `chmod 755 file` | Set permissions: owner=rwx(7), group=r-x(5), others=r-x(5). |
| 128 | `chmod 644 file` | Set permissions: owner=rw-(6), group=r--(4), others=r--(4). Common for config files. |
| 129 | `chmod 664 /sys/class/hwmon/hwmon2/pwm1` | Allow group write access to sysfs file (used in RPi5 RC file). |
| 130 | `chmod +x script.sh` | Add execute permission for all users. |
| 131 | `chmod -x script.sh` | Remove execute permission. |
| 132 | `chmod -R 755 mydir/` | Apply permissions recursively to a directory. |
| 133 | `chown user:group file` | Change owner and group of a file. |
| 134 | `chown root:system /sys/class/hwmon/hwmon2/pwm1` | Set owner to root, group to system (used in RPi5 RC file for sysfs). |
| 135 | `chown -R user:group dir/` | Recursively change ownership. |
| 136 | `id` | Show current user ID (uid), group ID (gid), and groups. |
| 137 | `id username` | Show uid/gid for a specific user. |
| 138 | `groups` | Show all groups the current user belongs to. |
| 139 | `sudo <command>` | Run command as root (superuser). |
| 140 | `sudo -i` | Open a root shell (stay as root). Use with care. |
| 141 | `umask` | Show default permission mask for new files (subtracts from 666/777). |
| 142 | `stat -c "%a %n" file` | Show permissions as numeric octal (e.g., `644 file.txt`). |

---

## 6. Process Management

| # | Command | Explanation |
|---|---------|-------------|
| 143 | `ps aux` | Show all running processes with CPU and memory usage. |
| 144 | `ps aux \| grep thermalcontrold` | Find a specific process by name. |
| 145 | `ps -ef` | Show all processes in full-format (shows parent PID). |
| 146 | `pgrep thermalcontrold` | Get the PID of a process by name. Cleaner than `ps \| grep`. |
| 147 | `pkill thermalcontrold` | Kill a process by name. |
| 148 | `kill <pid>` | Send SIGTERM (graceful shutdown) to a process by PID. |
| 149 | `kill -9 <pid>` | Send SIGKILL (force kill) — cannot be caught or ignored. |
| 150 | `kill -3 <pid>` | Send SIGQUIT — Java processes dump a stack trace to logcat. |
| 151 | `killall <name>` | Kill all processes with a given name. |
| 152 | `top` | Interactive live view of CPU/memory usage by process. Press `q` to quit. |
| 153 | `top -b -n 1` | Non-interactive single snapshot of `top`. Good for scripts. |
| 154 | `htop` | Better interactive process viewer. Install: `sudo apt install htop`. |
| 155 | `nice -n 10 <command>` | Run command with lower priority (niceness 10). Range: -20 (high) to 19 (low). |
| 156 | `renice 10 -p <pid>` | Change priority of a running process. |
| 157 | `nohup <command> &` | Run command immune to hangups (keeps running after you close terminal). |
| 158 | `<command> &` | Run command in the background. |
| 159 | `wait` | Wait for all background jobs to complete. |
| 160 | `strace <command>` | Trace system calls made by a program. Invaluable for debugging sysfs/Binder access issues. |
| 161 | `strace -p <pid>` | Attach strace to an already-running process. |
| 162 | `strace -e trace=file <command>` | Trace only file-related system calls (open, read, write, stat). |
| 163 | `ltrace <command>` | Trace library calls. Shows which `.so` functions are called. |
| 164 | `lsof -p <pid>` | List open files for a process (sockets, pipes, regular files). |
| 165 | `lsof /dev/binder` | Show processes that have the Binder device open. |

---

## 7. Disk & Filesystem

| # | Command | Explanation |
|---|---------|-------------|
| 166 | `df -h` | Disk space used/free per filesystem, human-readable. |
| 167 | `df -h /` | Disk space for the root filesystem specifically. |
| 168 | `du -sh dir/` | Total disk usage of a directory, human-readable. |
| 169 | `du -sh *` | Disk usage of each item in current directory. |
| 170 | `du -sh out/` | Check how large the AOSP build output is (often 100+ GB). |
| 171 | `du --max-depth=1 -h out/` | Show disk usage one level deep in `out/`. |
| 172 | `mount` | List all mounted filesystems. |
| 173 | `mount \| grep vendor` | Check if /vendor is mounted and its options (ro/rw). |
| 174 | `mount -o remount,rw /vendor` | Remount /vendor read-write (on device). **RPi5 workflow.** |
| 175 | `umount /path` | Unmount a filesystem. |
| 176 | `lsblk` | List block devices (disks, partitions) in a tree. |
| 177 | `lsblk -f` | List block devices with filesystem type and UUID. |
| 178 | `blkid` | Show block device UUIDs and filesystem types. |
| 179 | `fdisk -l` | List disk partitions (requires root). |
| 180 | `dd if=image.img of=/dev/sdX bs=4M status=progress` | Write disk image to SD card (e.g., flashing AOSP image to RPi5). **Double-check `/dev/sdX`!** |
| 181 | `sync` | Flush filesystem caches to disk. Always run before rebooting after pushing files. |
| 182 | `fsck /dev/sdX` | Filesystem check and repair (on unmounted partition). |
| 183 | `mkfs.ext4 /dev/sdX` | Format partition as ext4. |
| 184 | `losetup -f --show image.img` | Mount a disk image as a loop device for inspection. |
| 185 | `losetup -d /dev/loopX` | Detach a loop device. |

---

## 8. Archives & Compression

| # | Command | Explanation |
|---|---------|-------------|
| 186 | `tar -czf archive.tar.gz dir/` | Create a gzip-compressed tarball of a directory. (`c`=create, `z`=gzip, `f`=filename). |
| 187 | `tar -xzf archive.tar.gz` | Extract a gzip tarball. (`x`=extract). |
| 188 | `tar -xzf archive.tar.gz -C /target/dir/` | Extract to a specific directory. |
| 189 | `tar -tzf archive.tar.gz` | List contents of a tarball without extracting. |
| 190 | `tar -czf - dir/ \| ssh user@host 'tar -xzf - -C /remote/path/'` | Pipe tar over SSH — copy directory to remote without intermediate file. |
| 191 | `zip -r archive.zip dir/` | Create a zip archive. |
| 192 | `unzip archive.zip` | Extract a zip archive. |
| 193 | `unzip -l archive.zip` | List zip contents without extracting. |
| 194 | `gzip file.txt` | Compress a single file (creates file.txt.gz, removes original). |
| 195 | `gunzip file.txt.gz` | Decompress a gzip file. |
| 196 | `bzip2 file.txt` | Compress with bzip2 (better ratio than gzip but slower). |
| 197 | `xz file.txt` | Compress with xz (best ratio, used for AOSP release images). |
| 198 | `zcat file.gz` | View contents of a gzip file without decompressing. |

---

## 9. Environment & Variables

| # | Command | Explanation |
|---|---------|-------------|
| 199 | `env` | Print all environment variables. |
| 200 | `printenv PATH` | Print a specific environment variable. |
| 201 | `export MY_VAR="value"` | Set and export an environment variable (available to child processes). |
| 202 | `MY_VAR="value" command` | Set a variable only for one command's execution. |
| 203 | `unset MY_VAR` | Remove an environment variable. |
| 204 | `echo $HOME` | Print the value of a variable. |
| 205 | `echo $ANDROID_BUILD_TOP` | Print AOSP root directory (set by `envsetup.sh`). |
| 206 | `echo $TARGET_PRODUCT` | Print the lunch target product (e.g., `myoem_rpi5`). |
| 207 | `echo $OUT` | Print the build output directory (set by lunch). |
| 208 | `source build/envsetup.sh` | Load AOSP build environment functions into current shell. **Must run before lunch.** |
| 209 | `~/.bashrc` | File that runs on every new bash shell. Add `export` statements here for persistence. |
| 210 | `~/.bash_profile` | Runs only on login shells. Add PATH changes here. |

---

## 10. Networking

| # | Command | Explanation |
|---|---------|-------------|
| 211 | `ip addr` | Show network interfaces and IP addresses. Modern replacement for `ifconfig`. |
| 212 | `ip addr show eth0` | Show details for a specific interface. |
| 213 | `ifconfig` | Older tool to show/configure network interfaces. |
| 214 | `ping -c 4 8.8.8.8` | Send 4 ICMP pings to check connectivity. |
| 215 | `ping -c 4 192.168.1.100` | Ping your device's IP to check it's reachable. |
| 216 | `ip route` | Show routing table. |
| 217 | `netstat -tulpn` | Show all listening ports with process names. Useful to check if a service is listening. |
| 218 | `ss -tulpn` | Modern replacement for `netstat`. |
| 219 | `curl -O <url>` | Download a file from a URL. |
| 220 | `curl -I <url>` | Fetch only HTTP headers (check if URL is reachable). |
| 221 | `wget <url>` | Download a file. Alternative to curl. |
| 222 | `ssh user@host` | SSH into a remote machine. |
| 223 | `ssh-keygen -t rsa -b 4096` | Generate an SSH key pair. |
| 224 | `ssh-copy-id user@host` | Copy your public key to a remote host for passwordless login. |
| 225 | `scp file.txt user@host:/path/` | Securely copy a file to a remote host over SSH. |
| 226 | `scp user@host:/path/file.txt .` | Copy a file FROM a remote host. |
| 227 | `rsync -av src/ user@host:/dest/` | Sync directories over network (only transfers changed files). |
| 228 | `nmap -p 5555 <device_ip>` | Check if ADB TCP port is open on device. |
| 229 | `tcpdump -i eth0` | Capture network packets (requires root). |
| 230 | `nc -l 1234` | Start a simple netcat listener on port 1234. |

---

## 11. Git for AOSP

> AOSP uses hundreds of Git repos managed by the `repo` tool. But individual module work is still plain Git.

| # | Command | Explanation |
|---|---------|-------------|
| 231 | `git status` | Show modified, staged, and untracked files. First command to run when checking state. |
| 232 | `git diff` | Show unstaged changes (what you changed but haven't staged). |
| 233 | `git diff --staged` | Show staged changes (what will be in the next commit). |
| 234 | `git diff HEAD` | Show all changes (staged + unstaged) vs last commit. |
| 235 | `git log` | Show commit history. |
| 236 | `git log --oneline` | Compact one-line per commit view. |
| 237 | `git log --oneline --graph` | Visual branch graph + one-line log. |
| 238 | `git log -n 10` | Show only last 10 commits. |
| 239 | `git log --author="Arun"` | Filter commits by author. |
| 240 | `git log -- vendor/myoem/hal/` | Show commits that touched a specific path. |
| 241 | `git add file.cpp` | Stage a specific file for commit. |
| 242 | `git add -p` | Interactively stage parts of files (choose individual hunks). |
| 243 | `git add .` | Stage all changes in current directory. |
| 244 | `git commit -m "message"` | Commit staged changes with a message. |
| 245 | `git commit --amend` | Modify the last commit (message or content). Don't use on pushed commits. |
| 246 | `git stash` | Temporarily save uncommitted changes. Useful to switch branches quickly. |
| 247 | `git stash pop` | Restore the last stashed changes. |
| 248 | `git stash list` | List all stashed changes. |
| 249 | `git branch` | List local branches. |
| 250 | `git branch -a` | List all branches (local + remote). |
| 251 | `git checkout -b my-feature` | Create and switch to a new branch. |
| 252 | `git checkout main` | Switch to an existing branch. |
| 253 | `git merge feature-branch` | Merge a branch into the current branch. |
| 254 | `git rebase main` | Rebase current branch on top of main. |
| 255 | `git cherry-pick <sha>` | Apply a specific commit from another branch. Common in AOSP backporting. |
| 256 | `git reset HEAD file.cpp` | Unstage a file (undo `git add`). |
| 257 | `git checkout -- file.cpp` | Discard changes to a file (restore from last commit). **Destructive.** |
| 258 | `git clean -fd` | Delete all untracked files and directories. **Destructive.** |
| 259 | `git remote -v` | Show remote repository URLs. |
| 260 | `git fetch` | Fetch remote changes without merging. |
| 261 | `git pull` | Fetch + merge remote changes. |
| 262 | `git push origin my-branch` | Push a branch to remote. |
| 263 | `git tag v1.0` | Create a tag at current commit. |
| 264 | `git show <sha>` | Show a specific commit's changes and metadata. |
| 265 | `git blame file.cpp` | Show who last modified each line of a file. |
| 266 | `git bisect start` | Start binary search to find which commit introduced a bug. |
| 267 | `git bisect good <sha>` | Mark a commit as good during bisect. |
| 268 | `git bisect bad <sha>` | Mark a commit as bad during bisect. |
| 269 | `git shortlog -sn` | Show commit count per author. |
| 270 | `git grep "pattern"` | Grep through all tracked files in the repo. Faster than `grep -r`. |

---

## 12. Repo Tool (AOSP Multi-Git)

> `repo` is Google's tool that manages the 800+ Git repos that make up AOSP. Every AOSP developer uses it.

| # | Command | Explanation |
|---|---------|-------------|
| 271 | `repo init -u <manifest_url> -b <branch>` | Initialize a new AOSP workspace with a manifest and branch (e.g., `android-15.0.0_r14`). |
| 272 | `repo sync` | Download/sync all repositories defined in the manifest. Can take hours on first run. |
| 273 | `repo sync -j8` | Sync using 8 parallel jobs. Use based on your internet speed and CPU. |
| 274 | `repo sync -c` | Sync only the current branch (faster, skips other branches). |
| 275 | `repo sync vendor/myoem` | Sync only a specific project/path. |
| 276 | `repo status` | Show git status across all repos (shows which have local changes). |
| 277 | `repo diff` | Show diffs across all repos that have local changes. |
| 278 | `repo forall -c 'git log --oneline -5'` | Run a git command in every project. |
| 279 | `repo forall -p -c 'git diff'` | Show diffs in all projects that have changes (`-p` prints project name). |
| 280 | `repo start my-branch .` | Create a new branch in the current project (`.` = current dir). |
| 281 | `repo start my-branch --all` | Create a new branch in ALL projects. |
| 282 | `repo branches` | List branches across all projects. |
| 283 | `repo upload` | Upload local commits for code review (Gerrit). |
| 284 | `repo upload --cbr .` | Upload the current branch of the current project. |
| 285 | `repo prune` | Delete local branches that have been merged upstream. |
| 286 | `repo manifest` | Print the current manifest XML. |
| 287 | `repo manifest -r -o snapshot.xml` | Save a pinned manifest (exact SHAs) for reproducibility. |
| 288 | `repo grep "pattern"` | Grep across all repos in the AOSP tree. |
| 289 | `repo info` | Show info about the current repo (URL, branches, etc.). |
| 290 | `repo version` | Show the repo tool version. |

---

## 13. AOSP Build System

> These replace your "Build > Make Project" and Gradle commands from Android Studio.

| # | Command | Explanation |
|---|---------|-------------|
| 291 | `source build/envsetup.sh` | Load build environment (functions like `lunch`, `m`, `mm`, `croot`). **Run once per shell session.** |
| 292 | `lunch` | Interactive menu to choose build target. |
| 293 | `lunch myoem_rpi5-trunk_staging-userdebug` | Directly set target without menu. Sets `$TARGET_PRODUCT`, `$OUT`, etc. |
| 294 | `m` | Build everything (like `make` but smarter, uses `soong_ui`). |
| 295 | `m -j$(nproc)` | Build using all available CPU cores. |
| 296 | `m thermalcontrold` | Build only the `thermalcontrold` module and its dependencies. |
| 297 | `m libthermalcontrolhal` | Build only the HAL shared library. |
| 298 | `mm` | Build all modules in the current directory. Run from inside a module's directory. |
| 299 | `mm -j8` | Build current directory modules using 8 threads. |
| 300 | `mmm vendor/myoem/hal/thermalcontrol/` | Build a specific path's modules without `cd`-ing there. |
| 301 | `mma` | Build current directory modules AND all their dependencies. |
| 302 | `mmma vendor/myoem/` | Build all modules under `vendor/myoem/` and their dependencies. |
| 303 | `make clean` | Delete all build output (`out/` directory). Takes a long time. |
| 304 | `make installclean` | Partial clean — removes installed files but keeps compiled objects. Faster than full clean. |
| 305 | `make snod` | Rebuild system.img without recompiling (just re-packages). Faster for system-image-only changes. |
| 306 | `make vnod` | Rebuild vendor.img without recompiling. **Useful for RPi5 vendor changes.** |
| 307 | `croot` | `cd` to the AOSP root directory from anywhere (function loaded by `envsetup.sh`). |
| 308 | `cgrep "pattern"` | Grep only C/C++ files in the AOSP tree (function from `envsetup.sh`). |
| 309 | `jgrep "pattern"` | Grep only Java files in the AOSP tree. |
| 310 | `bpgrep "pattern"` | Grep only Android.bp files in the AOSP tree. |
| 311 | `godir <file>` | `cd` to the directory containing a file by name (searches AOSP tree). |
| 312 | `echo $OUT` | Show build output path (e.g., `out/target/product/rpi5`). |
| 313 | `ls $OUT/` | List the build output directory contents (system.img, vendor.img, etc.). |
| 314 | `ls $OUT/vendor/lib64/` | Check if your `.so` was built. |
| 315 | `ls $OUT/vendor/bin/` | Check if your binary was built. |
| 316 | `m nothing` | Parse and validate all Android.bp files without building. Fast way to catch syntax errors. |
| 317 | `SANITIZE_HOST=address m` | Build with Address Sanitizer for host tools. |
| 318 | `make dist` | Build and collect distributable artifacts (release builds). |
| 319 | `m droid` | The default build target (same as just `m`). |
| 320 | `make -j1` | Build with a single thread (useful for reading build errors clearly). |

---

## 14. Soong / Android.bp Concepts

> Android.bp replaced Android.mk in AOSP. It's a JSON-like format processed by Soong (the build system).

| # | Command / Concept | Explanation |
|---|---------|-------------|
| 321 | `blueprint_tools` | Low-level tools that process `.bp` files. Usually used indirectly via Soong. |
| 322 | `bpfmt -w Android.bp` | Format an `Android.bp` file in-place (like `gofmt` for Go). |
| 323 | `bpfix -f Android.bp` | Fix common issues in `Android.bp` automatically. |
| 324 | `m nothing 2>&1 \| grep "error"` | Check Android.bp for errors without actually building. |
| 325 | `soong_ui --make-mode ...` | The actual build backend invoked by `m`. Rarely called directly. |
| 326 | `cc_library_shared` | Build a `.so` shared library (e.g., `libthermalcontrolhal.so`). |
| 327 | `cc_library_static` | Build a `.a` static library (linked into the binary). |
| 328 | `cc_binary` | Build a native executable (e.g., `thermalcontrold`). |
| 329 | `cc_test` | Build a native test binary (run with `atest` or manually). |
| 330 | `java_library` | Build a Java library (`.jar`). |
| 331 | `java_sdk_library` | Build a Java library that generates an API stub (for `uses_libs`). |
| 332 | `android_app` | Build an APK application. |
| 333 | `aidl_interface` | Define an AIDL interface that generates C++/Java/NDK/Rust backends. |
| 334 | `vendor: true` | Mark a module as vendor partition. Can use vendor-only libraries. |
| 335 | `vendor_available: true` | Mark a library as available to vendor modules. |
| 336 | `proprietary: true` | Mark as proprietary/OEM code (similar to `vendor: true` with stricter constraints). |
| 337 | `shared_libs: ["libfoo"]` | Link against shared libraries at runtime. |
| 338 | `static_libs: ["libfoo"]` | Link statically (compiled in, no runtime dependency). |
| 339 | `cflags: ["-DDEBUG"]` | Pass compiler flags. Add `-Wall -Werror` for stricter builds. |
| 340 | `init_rc: ["myservice.rc"]` | Install an RC file with the binary (sets up systemd-like service definition). |
| 341 | `srcs: ["src/**/*.cpp"]` | Glob pattern to include all `.cpp` files under `src/`. |
| 342 | `local_include_dirs: ["include"]` | Add local include directory (not exported to users of this lib). |
| 343 | `export_include_dirs: ["include"]` | Export include directory (dependent modules get it too). |
| 344 | `defaults: ["mydefaults"]` | Inherit settings from a `cc_defaults` block (DRY pattern). |
| 345 | `installable: false` | Don't install to device — useful for test helpers. |

---

## 15. ADB — Advanced Usage

| # | Command | Explanation |
|---|---------|-------------|
| 346 | `adb shell` | Open interactive shell on device. |
| 347 | `adb root && adb shell` | Root shell in one go. |
| 348 | `adb shell 'cmd thermalcontrol status'` | Run a complex command with quotes from host. |
| 349 | `adb push $OUT/vendor/lib64/libthermalcontrolhal.so /vendor/lib64/` | Push a rebuilt library to device. |
| 350 | `adb push $OUT/vendor/bin/thermalcontrold /vendor/bin/` | Push a rebuilt binary to device. |
| 351 | `adb shell sync && adb reboot` | Flush and reboot — use after pushing files. |
| 352 | `adb shell 'stop thermalcontrold && start thermalcontrold'` | Restart a running service without full reboot. |
| 353 | `adb shell stop thermalcontrold` | Stop a service defined in an RC file. |
| 354 | `adb shell start thermalcontrold` | Start a service defined in an RC file. |
| 355 | `adb forward tcp:5000 tcp:5000` | Forward a host port to device port (useful for gdbserver). |
| 356 | `adb reverse tcp:8080 tcp:8080` | Forward a device port back to host (device accesses host services). |
| 357 | `adb shell input keyevent 26` | Simulate power button press. |
| 358 | `adb shell input keyevent 3` | Simulate Home button press. |
| 359 | `adb shell input text "hello"` | Type text into focused input field. |
| 360 | `adb shell input tap 500 500` | Simulate a touch at (x=500, y=500). |
| 361 | `adb shell screencap /sdcard/screen.png && adb pull /sdcard/screen.png` | Take a screenshot. |
| 362 | `adb shell screenrecord /sdcard/demo.mp4` | Record screen video (press Ctrl+C to stop). |
| 363 | `adb shell wm size` | Get display resolution. |
| 364 | `adb shell wm density` | Get display density (DPI). |
| 365 | `adb shell cmd package resolve-activity -c android.intent.category.LAUNCHER` | List all launcher activities. |
| 366 | `adb shell settings get global airplane_mode_on` | Read a system setting value. |
| 367 | `adb shell settings put global airplane_mode_on 1` | Write a system setting value. |
| 368 | `adb shell cmd wifi enable` | Enable WiFi from shell. |
| 369 | `adb shell cmd wifi disable` | Disable WiFi from shell. |
| 370 | `adb bugreport` | Generate a full bug report (zip file with logs, props, dumpsys). Essential for filing bugs. |

---

## 16. Android Shell (adb shell) Commands

> These are commands that run ON the device shell, not on your host.

| # | Command | Explanation |
|---|---------|-------------|
| 371 | `getprop ro.build.fingerprint` | Get full build fingerprint. |
| 372 | `getprop \| grep "selinux"` | Check SELinux-related properties. |
| 373 | `setprop debug.myoem.loglevel 5` | Set a debug property at runtime. |
| 374 | `resetprop <key> <value>` | Set any property including read-only (`ro.*`) ones (Magisk tool, not stock). |
| 375 | `service list` | List all Binder services registered in ServiceManager. |
| 376 | `service check thermalcontrolservice` | Check if your service is registered. |
| 377 | `service call <name> <code>` | Make a raw Binder call to a service (advanced debugging). |
| 378 | `dumpsys thermalcontrolservice` | Call `dump()` on your custom service (if you implemented it). |
| 379 | `cmd <service> <args>` | Call a service's command interface (newer API than `service call`). |
| 380 | `am stack list` | List activity stacks. |
| 381 | `am display-size WxH` | Override display size (useful for testing responsive layouts). |
| 382 | `am display-density <dpi>` | Override display density. |
| 383 | `pm grant <pkg> <permission>` | Grant a runtime permission to an app. |
| 384 | `pm revoke <pkg> <permission>` | Revoke a runtime permission from an app. |
| 385 | `pm list permissions -g` | List all permissions grouped by group. |
| 386 | `pm dump <pkg>` | Dump all package info (activities, services, permissions, providers). |
| 387 | `pm path <pkg>` | Show APK path on device. |
| 388 | `content query --uri content://settings/global` | Query a ContentProvider from shell. |
| 389 | `content insert --uri content://settings/global --bind name:s:mykey --bind value:s:myval` | Insert into a ContentProvider from shell. |
| 390 | `svc wifi enable` | Enable/disable WiFi (`svc` controls system services). |
| 391 | `svc bluetooth enable` | Enable Bluetooth. |
| 392 | `svc power stayon true` | Keep screen on while plugged in. |
| 393 | `wm size 1920x1080` | Override screen resolution. |
| 394 | `wm size reset` | Reset screen resolution to default. |
| 395 | `dmesg` | Print kernel ring buffer (kernel log). Same as `adb logcat -b kernel`. |
| 396 | `dmesg \| grep "thermal"` | Filter kernel log for thermal events. |
| 397 | `dmesg -w` | Watch kernel log in real time. |
| 398 | `cat /proc/version` | Show kernel version. |
| 399 | `cat /proc/cpuinfo` | Show CPU info (model, cores, speed). |
| 400 | `cat /proc/meminfo` | Show memory statistics (MemTotal, MemFree, etc.). |
| 401 | `cat /proc/interrupts` | Show hardware interrupt counters. |
| 402 | `cat /proc/mounts` | Show all mounted filesystems on device. |
| 403 | `cat /proc/net/dev` | Show network interface statistics. |
| 404 | `ls /dev/` | List device files (character/block devices). |
| 405 | `ls /dev/binder` | Check Binder device file exists. |
| 406 | `ls /sys/class/thermal/` | List thermal zones. |
| 407 | `ls /sys/class/hwmon/` | List hardware monitor interfaces. |
| 408 | `cat /sys/class/thermal/thermal_zone0/temp` | Read CPU temperature in millidegrees. |
| 409 | `cat /sys/class/hwmon/hwmon2/pwm1` | Read current fan PWM value. |
| 410 | `echo 200 > /sys/class/hwmon/hwmon2/pwm1` | Set fan PWM to 200 (requires root + chown on RPi5). |

---

## 17. Logcat — Advanced

| # | Command | Explanation |
|---|---------|-------------|
| 411 | `adb logcat -s TAG:D` | Show DEBUG and above for a specific tag. |
| 412 | `adb logcat TAG1:D TAG2:I *:S` | Multiple tag filters: TAG1 at Debug, TAG2 at Info, everything else Silent. |
| 413 | `adb logcat -v color` | Color-coded output by log level. |
| 414 | `adb logcat -v long` | Verbose format showing all fields on separate lines. |
| 415 | `adb logcat -v epoch` | Timestamps as Unix epoch. Good for correlating with host logs. |
| 416 | `adb logcat --pid=<pid>` | Filter logcat to a specific process ID. |
| 417 | `adb logcat --uid=<uid>` | Filter logcat to a specific user ID. |
| 418 | `adb logcat -b radio` | Show radio/telephony buffer. |
| 419 | `adb logcat -b events` | Show events buffer (system events, ANR events, etc.). |
| 420 | `adb logcat -G 50M` | Set logcat buffer size to 50 MB (don't lose logs during long tests). |
| 421 | `adb logcat *:V \| tee device.log` | Save logcat to file while still viewing it in terminal. |
| 422 | `adb logcat -d > logcat_$(date +%Y%m%d_%H%M%S).txt` | Dump logcat to a timestamped file. |
| 423 | `adb logcat -d \| grep -E "(E\|F)/" ` | Show only Error and Fatal log lines. |
| 424 | `adb shell 'logcat -b crash -d'` | Get crash buffer from within the shell. |
| 425 | `adb logcat AndroidRuntime:E *:S` | Show only Java crashes (unhandled exceptions). |

---

## 18. SELinux Tools & Debugging

| # | Command | Explanation |
|---|---------|-------------|
| 426 | `adb shell getenforce` | Check SELinux mode: `Enforcing` or `Permissive`. |
| 427 | `adb shell setenforce 0` | Permissive mode (denials logged but not blocked). Use to test if SELinux is causing issues. |
| 428 | `adb shell ls -laZ /sys/class/hwmon/hwmon2/` | Show security context (label) of sysfs files. |
| 429 | `adb shell ls -laZ /vendor/bin/thermalcontrold` | Check SELinux label on your binary. |
| 430 | `adb shell ps -eZ \| grep thermalcontrold` | Check which SELinux domain your service runs in. |
| 431 | `adb logcat -d \| grep "avc: denied"` | Find SELinux denials. |
| 432 | `adb shell dmesg \| grep "avc:"` | Find SELinux denials in kernel log (more complete). |
| 433 | `audit2allow` | Convert SELinux denial messages to policy rules (host tool). |
| 434 | `adb logcat -d \| grep "avc: denied" \| audit2allow` | Auto-generate SELinux allow rules from denials. |
| 435 | `checkpolicy` | Compile and check SELinux policy files. |
| 436 | `sesearch --allow -s thermalcontrold_t` | Search what is allowed for a domain in a compiled policy. |
| 437 | `adb shell cat /sys/fs/selinux/enforce` | Read enforce flag directly from the kernel sysfs. |
| 438 | `adb shell cat /sys/fs/selinux/policy` | Read compiled SELinux policy from device. |
| 439 | `adb pull /sys/fs/selinux/policy && sesearch --allow -s myservice_t policy` | Analyze device policy on host. |
| 440 | `adb shell restorecon -R /vendor/bin/thermalcontrold` | Re-apply SELinux context from policy to a file. |

---

## 19. Binder & Service Debugging

| # | Command | Explanation |
|---|---------|-------------|
| 441 | `adb shell service list` | List ALL registered Binder services. Verify your service appears here after starting. |
| 442 | `adb shell service check com.myoem.thermalcontrol.IThermalControlService` | Check if your AIDL service is registered. |
| 443 | `adb shell dumpsys -l` | List all services that respond to `dumpsys` (a subset of all Binder services). |
| 444 | `adb shell cat /sys/kernel/debug/binder/state` | Raw Binder kernel state dump (requires root). |
| 445 | `adb shell cat /sys/kernel/debug/binder/stats` | Binder transaction statistics. |
| 446 | `adb shell cat /sys/kernel/debug/binder/transactions` | Active Binder transactions (useful for deadlock debugging). |
| 447 | `adb shell cat /sys/kernel/debug/binder/failed_transaction_log` | Failed Binder transactions log. |
| 448 | `adb shell lsof \| grep binder` | List processes with Binder file descriptors open. |
| 449 | `adb shell dumpsys activity service <pkg>/<service>` | Dump state of a bound Android service. |
| 450 | `adb shell dumpsys activity services` | Dump state of all running Android services. |

---

## 20. sysfs / procfs Navigation

| # | Command | Explanation |
|---|---------|-------------|
| 451 | `adb shell find /sys -name "temp" 2>/dev/null` | Find all temperature sysfs files. |
| 452 | `adb shell find /sys/class/thermal -name "temp"` | List all thermal zone temperature files. |
| 453 | `adb shell for f in /sys/class/thermal/thermal_zone*/temp; do echo "$f: $(cat $f)"; done` | Read all thermal zone temperatures in a loop. |
| 454 | `adb shell cat /sys/class/thermal/thermal_zone0/type` | Show the type/name of a thermal zone (e.g., "cpu-thermal"). |
| 455 | `adb shell ls /sys/class/hwmon/` | List all hwmon interfaces (fan controllers, power monitors). |
| 456 | `adb shell cat /sys/class/hwmon/hwmon2/name` | Show the name/driver of an hwmon interface. |
| 457 | `adb shell find /sys/class/hwmon -name "pwm*"` | Find all PWM fan control files. |
| 458 | `adb shell cat /proc/sys/kernel/printk` | Show kernel log level settings. |
| 459 | `adb shell echo 7 > /proc/sys/kernel/printk` | Set kernel log level to DEBUG (7). |
| 460 | `adb shell cat /proc/sys/vm/swappiness` | Read current swappiness setting. |
| 461 | `adb shell cat /proc/sys/net/ipv4/ip_forward` | Check if IP forwarding is enabled. |
| 462 | `adb shell ls -la /proc/<pid>/fd/` | List open file descriptors for a process. |
| 463 | `adb shell cat /proc/<pid>/cmdline` | Show the command line a process was started with. |
| 464 | `adb shell cat /proc/<pid>/oom_score_adj` | Show OOM kill priority for a process. |

---

## 21. Native Crash & Tombstone Analysis

| # | Command | Explanation |
|---|---------|-------------|
| 465 | `adb pull /data/tombstones/` | Pull all native crash tombstones from device. |
| 466 | `adb pull /data/tombstones/tombstone_00` | Pull the most recent tombstone. |
| 467 | `cat tombstone_00` | Read a tombstone — contains signal, backtrace, registers, memory maps. |
| 468 | `adb logcat -b crash` | Show crash log buffer in real time. |
| 469 | `addr2line -e out/target/product/rpi5/symbols/vendor/lib64/libthermalcontrolhal.so <address>` | Convert a crash address to a source file + line number. **Use symbols, not stripped binary.** |
| 470 | `addr2line -Cfe <so_path> <addr>` | Demangle C++ names + show file + line. `-C` = demangle, `-f` = show function. |
| 471 | `nm -D libthermalcontrolhal.so` | List dynamic symbols in a shared library. Check what's exported. |
| 472 | `objdump -d libthermalcontrolhal.so \| less` | Disassemble a shared library. |
| 473 | `readelf -h mybinary` | Show ELF header (architecture, entry point, type). |
| 474 | `readelf -d mybinary` | Show dynamic section (needed libraries: `NEEDED` entries). |
| 475 | `readelf -s mybinary \| grep "functionName"` | Look up a symbol in an ELF. |
| 476 | `ldd mybinary` | List shared library dependencies (host binaries only). |
| 477 | `ndk-stack -sym $OUT/symbols -dump tombstone_00` | Human-readable backtrace from tombstone using symbols (NDK tool). |
| 478 | `stack --symbols $OUT/symbols < tombstone_00` | AOSP `stack` script for symbolized backtraces. |
| 479 | `adb logcat \| grep "FATAL\|SIGSEGV\|signal 11"` | Watch for native crashes in real time. |
| 480 | `adb shell debuggerd <pid>` | Force-dump a process's backtrace without killing it. |

---

## 22. Performance & Memory Profiling

| # | Command | Explanation |
|---|---------|-------------|
| 481 | `adb shell dumpsys meminfo <pkg>` | Show detailed memory breakdown for a process (PSS, RSS, Java heap, native heap). |
| 482 | `adb shell dumpsys meminfo -a` | Memory info for all processes. |
| 483 | `adb shell cat /proc/<pid>/smaps` | Low-level memory maps for a process (very detailed). |
| 484 | `adb shell vmstat` | Show virtual memory statistics (paging, I/O, CPU). |
| 485 | `adb shell dumpsys cpuinfo` | CPU usage by process from the system perspective. |
| 486 | `adb shell top -n 1 -s 6` | Snapshot of top, sorted by CPU (`-s 6`). |
| 487 | `adb shell dumpsys gfxinfo <pkg>` | GPU rendering performance for a specific app (frame stats). |
| 488 | `adb shell dumpsys batterystats \| grep <pkg>` | Battery usage attributed to a specific package. |
| 489 | `adb shell simpleperf record -p <pid>` | Record CPU performance profile of a process (simpleperf = Android's perf). |
| 490 | `adb shell simpleperf stat -p <pid>` | Show CPU hardware counter statistics for a process. |

---

## 23. Shell Scripting Essentials

| # | Concept | Explanation |
|---|---------|-------------|
| 491 | `#!/bin/bash` | Shebang — first line of a script, tells OS which interpreter to use. |
| 492 | `chmod +x script.sh && ./script.sh` | Make a script executable and run it. |
| 493 | `if [ condition ]; then ... fi` | Basic if-else. `[ ]` is the `test` command. |
| 494 | `if [ -f "file.txt" ]; then echo "exists"; fi` | Check if a file exists (`-f` = regular file, `-d` = directory). |
| 495 | `for f in *.cpp; do echo "$f"; done` | Loop over files. |
| 496 | `while [ condition ]; do ... done` | While loop. |
| 497 | `$?` | Exit code of the last command. `0` = success, anything else = failure. |
| 498 | `command1 && command2` | Run `command2` only if `command1` succeeds (exit code 0). |
| 499 | `command1 \|\| command2` | Run `command2` only if `command1` fails. |
| 500 | `2>&1` | Redirect stderr to stdout (combine error and normal output, e.g., for `tee` or `grep`). |

---

## 24. Useful One-Liners for AOSP Work

```bash
# Watch a sysfs value change in real time (every 1 second)
watch -n 1 'adb shell cat /sys/class/thermal/thermal_zone0/temp'

# Push a rebuilt .so and restart its service (RPi5 workflow)
adb root && adb shell mount -o remount,rw /vendor && \
  adb push $OUT/vendor/lib64/libthermalcontrolhal.so /vendor/lib64/ && \
  adb shell sync && \
  adb shell stop thermalcontrold && adb shell start thermalcontrold

# Find which AOSP module provides a specific file
adb shell pm list packages -f | grep "thermalcontrol"

# Show all SELinux domains currently in use on the device
adb shell ps -eZ | awk '{print $1}' | sort -u

# Count lines of code in vendor/myoem/ (C++, Java, bp)
find vendor/myoem -name "*.cpp" -o -name "*.h" -o -name "*.java" -o -name "*.bp" | xargs wc -l

# Find all Android.bp files in your vendor layer
find vendor/myoem -name "Android.bp"

# Check which packages define a specific AIDL service interface
grep -r "IThermalControlService" vendor/myoem/ --include="*.aidl" --include="*.cpp" --include="*.java"

# Monitor device logs filtered to your service tags
adb logcat -s ThermalControlHal:D ThermalControlService:D AndroidRuntime:E

# Decode a base64-encoded SELinux context from audit log
adb logcat -d | grep "avc: denied" | head -20

# Build only your module and check for errors (fast iteration)
mmm vendor/myoem/hal/thermalcontrol/ 2>&1 | grep -E "(error:|warning:)"

# Find files modified in the last 10 minutes (see what the build system touched)
find out/target/product/rpi5/vendor -newer /tmp/stamp -type f 2>/dev/null

# Quick thermal check (host command - reads from device)
adb shell 'echo "CPU Temp: $(($(cat /sys/class/thermal/thermal_zone0/temp)/1000))°C"'
```

---

## 25. Concepts Glossary

> Key concepts from AOSP that every system developer should know.

| Term | Explanation |
|------|-------------|
| **Binder** | Android's IPC (Inter-Process Communication) mechanism. All cross-process calls (ServiceManager, AIDL) go through the `/dev/binder` kernel driver. Replaces traditional UNIX sockets for Android services. |
| **AIDL** | Android Interface Definition Language. Defines an IPC contract (methods, types). The build system generates C++/Java/NDK/Rust bindings from `.aidl` files. |
| **HAL (Hardware Abstraction Layer)** | The layer between Android framework and hardware drivers. Your `libthermalcontrolhal.so` is a HAL. |
| **LLNDK** | Low-Level NDK. Stable system libraries that vendor code can link against (e.g., `libbinder_ndk`). Vendor code must use LLNDK, not platform libs. |
| **Treble** | Android's project to separate vendor code from platform code. Result: vendor.img and system.img can be updated independently. Why `vendor: true` exists in `Android.bp`. |
| **SELinux** | Security-Enhanced Linux. Mandatory Access Control enforced by the kernel. Every file, process, and socket has a security context (label). Denials appear in logcat as `avc: denied`. |
| **sysfs** | A virtual filesystem (`/sys/`) exposing kernel driver state (hardware sensors, LEDs, fans) as readable/writable files. Your ThermalControl HAL reads `/sys/class/thermal/` and writes `/sys/class/hwmon/`. |
| **procfs** | Virtual filesystem (`/proc/`) exposing process and kernel state (memory, CPU, network). Read-only system information. |
| **RC files** | Android Init Language files (`.rc`). Describe services (how to start them, their permissions) and actions (commands to run at boot). Processed by `init`. |
| **ServiceManager** | The central registry for Binder services. `addService()` registers your service; `getService()` looks it up. Listed by `adb shell service list`. |
| **Soong** | AOSP's primary build system. Processes `Android.bp` files. Replaced Make for most modules. Written in Go. |
| **Tombstone** | A file written to `/data/tombstones/` when a native process crashes. Contains signal, backtrace, register dump, memory maps. |
| **ANR** | Application Not Responding. Triggered when the main thread is blocked >5 seconds (Activity) or >10 seconds (Service). Traces written to `/data/anr/`. |
| **`userdebug` build** | Build type between `user` (production) and `eng` (full debug). Has `adb root` support and debug symbols. What you use: `myoem_rpi5-trunk_staging-userdebug`. |
| **`$OUT`** | Shell variable pointing to your build output directory (e.g., `out/target/product/rpi5`). Set by `lunch`. |
| **Vendor partition** | `/vendor` — partition for OEM/BSP code (HALs, services, RC files, SEPolicy). Separate from `/system`. You push your work here. |
| **`init.rc`** | Root RC file processed by Android `init` on boot. Imports other `.rc` files. Your `thermalcontrold.rc` gets included via the binary's `init_rc` property. |
| **`logd`** | Android's log daemon. All `ALOGD/ALOGI/ALOGE` calls in C++ go here. `logcat` reads from `logd`. |
| **Hwmon** | Linux hardware monitoring subsystem. Exposes fan speeds, temperatures, voltages via `/sys/class/hwmon/`. Used by your ThermalControlHal for fan PWM. |

---

## Quick Reference Card

```
# Host setup (once per terminal session)
source build/envsetup.sh
lunch myoem_rpi5-trunk_staging-userdebug

# Build a module
mmm vendor/myoem/hal/thermalcontrol/

# Push & test (RPi5 workflow)
adb root
adb shell mount -o remount,rw /vendor
adb push $OUT/vendor/lib64/libthermalcontrolhal.so /vendor/lib64/
adb shell sync
adb reboot

# Check service
adb shell service list | grep thermal
adb shell ps -eZ | grep thermalcontrold

# Debug
adb logcat -s ThermalControlHal
adb logcat -d | grep "avc: denied"
adb shell dmesg | grep "thermal\|avc"
```

---

*Last updated: March 2026 — AOSP 15 / android-15.0.0_r14 / Raspberry Pi 5*
