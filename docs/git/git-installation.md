GIT INSTALLATION AND AUTH
=========================

PURPOSE
-------
This document covers Git and GitHub CLI installation and login.


INSTALLATION
------------
Recommended environment: Linux or WSL (same as CI).

Linux / WSL:
```
sudo apt update
sudo apt install -y git gh
```

macOS:
```
brew install git gh
```

Windows:
```
winget install Git.Git GitHub.cli
```
```
choco install git gh
```


AUTHENTICATE gh (ONCE, BROWSER-BASED)
-------------------------------------
Run:
```
gh auth login
```

Choose:
- GitHub.com
- HTTPS
- Authenticate Git with GitHub credentials: Yes
- Login method: Browser

Verify:
```
gh auth status
```
