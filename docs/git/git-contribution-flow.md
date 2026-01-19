GIT CONTRIBUTION FLOW - LOCAL -> PERSONAL -> UPSTREAM
=====================================================

WELCOME
-------
This guide shows a simple way to contribute.


STEP 1 - UPDATE YOUR BASE
-------------------------
Fetch your fork and check for differences.

Commands:
```
git fetch origin
git diff main origin/main
git diff 4.x-master origin/4.x-master
```


STEP 2 - CREATE A WORK BRANCH
-----------------------------
From `main`:
```
git checkout main
git pull origin main
git switch -c <branch>
git push -u origin <branch>
```

From `4.x-master`:
```
git checkout 4.x-master
git pull origin 4.x-master
git switch -c <branch>
git push -u origin <branch>
```


STEP 3 - MAKE YOUR CHANGE
-------------------------
```
git add <files>
git commit -m "<message>"
git push
```


STEP 4 - OPEN A PULL REQUEST
----------------------------
Optional: open a PR in your fork to run CI.

Then open the upstream PR:
```
<your-github-username>/xymon:<branch>
-> xymon-monitoring/xymon:<branch>
```


STEP 5 - CLEAN UP
-----------------
```
git branch -d <branch>
git push origin --delete <branch>
```
