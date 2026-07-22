@echo off
setlocal EnableExtensions EnableDelayedExpansion
title V5 Full Git Push

for %%I in ("%~dp0..\..\..") do set "REPO=%%~fI"
set "REMOTE=origin"
set "BRANCH=main"
set "GIT_PAGER=cat"
set "CHECK_ONLY=0"
set "V5_GIT_ALLOWED_NEW_ROOT=AGENTS.md"

if /i "%~1"=="--check-only" (
    set "CHECK_ONLY=1"
) else if not "%~1"=="" (
    echo ERROR: Unknown argument "%~1".
    echo Usage: %~nx0 [--check-only]
    exit /b 2
)

echo ============================================================
echo V5 full Git and Git LFS push
echo Repository: %REPO%
echo Target:     %REMOTE%/%BRANCH%
if "%CHECK_ONLY%"=="1" echo Mode:       check only ^(no commit, LFS upload or push^)
echo ============================================================
echo.

if not exist "%REPO%\.git" (
    echo ERROR: Git repository not found at %REPO%
    goto :fail
)

cd /d "%REPO%" || goto :fail

if not exist "%REPO%\AGENTS.md" (
    echo ERROR: Required root rule owner is missing: %REPO%\AGENTS.md
    goto :fail
)

where git >nul 2>&1 || (
    echo ERROR: git.exe was not found in PATH.
    goto :fail
)

git lfs version >nul 2>&1 || (
    echo ERROR: Git LFS is not installed or is unavailable.
    goto :fail
)

for /f "delims=" %%B in ('git branch --show-current') do set "CURRENT_BRANCH=%%B"
if /i not "%CURRENT_BRANCH%"=="%BRANCH%" (
    echo ERROR: Current branch is "%CURRENT_BRANCH%"; expected "%BRANCH%".
    goto :fail
)

echo [1/10] Fetching the current remote branch...
git fetch %REMOTE% %BRANCH% || goto :fail

git merge-base --is-ancestor %REMOTE%/%BRANCH% HEAD
if errorlevel 1 (
    echo ERROR: Local HEAD does not contain the current remote branch.
    echo Resolve the divergence manually before running this script again.
    goto :fail
)

echo [2/10] Sanitizing root outputs and staging canonical source...
echo Unregistered root files are preserved under repo_ignored before staging.
call :stage_snapshot
if errorlevel 1 goto :fail

git ls-files --error-unmatch -- AGENTS.md >nul 2>&1 || (
    echo ERROR: AGENTS.md was not staged as the canonical root rule owner.
    goto :fail
)

rem .gitignore owns the normal Git range. Deletions are intentionally allowed
rem here so previously tracked process/proof output can leave Git once. Any
rem added, copied, modified, renamed or type-changed forbidden output fails.
rem A new file directly under the repository root also fails unless it is
rem explicitly registered in the owner document and in allowedNewRoot below.
powershell -NoProfile -Command ^
    "$specs=@(':(top)repo_ignored/**',':(glob)**/repo_ignored/**',':(glob)**/__pycache__/**',':(glob)**/.pytest_cache/**',':(glob)**/node_modules/**',':(glob)**/Testing/Temporary/**',':(glob)**/artifacts/**',':(glob)**/evidence/**',':(glob)**/evidence_*/**',':(glob)**/*_evidence/**',':(glob)**/*evidence*/**',':(glob)**/graphify-out/**',':(top)build/**',':(top)build-*/**',':(top)board/build/**',':(top)8ax-win/publish/**',':(top)8ax-dealer-client-source/publish/**',':(top)8ax-factory-client-source/publish/**',':(glob)**/CMakeFiles/**',':(glob)**/.Xil/**',':(glob)**/*.tmp',':(glob)**/*.temp',':(glob)**/*.log',':(glob)**/*.gcda',':(glob)**/*.gcno',':(glob)**/*.profraw',':(glob)**/*.profdata',':(glob)**/*.dmp',':(top)-e',':(top)event',':(top)pclk',':(top)restart_required',':(top)rtcp_force_off',':(top)source_path',':(top)commandNum',':(top)trajectory',':(top)trajectory_line',':(top)wait',':(top)wait()');" ^
    "$forbidden=@(& git -c core.quotepath=false diff --cached --name-only --diff-filter=ACMRTUXB -- $specs);" ^
    "if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE };" ^
    "$indexPaths=@(& git -c core.quotepath=false ls-files --);" ^
    "if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE };" ^
    "$allowedRoot=@('.gitattributes','.gitignore','AGENTS.md') + @($env:V5_GIT_ALLOWED_NEW_ROOT -split ';' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) });" ^
    "$unexpectedRoot=@($indexPaths | Where-Object { $_ -and $_ -notmatch '[\\/]' -and $allowedRoot -notcontains $_ });" ^
    "$blocked=@($forbidden + $unexpectedRoot | Sort-Object -Unique);" ^
    "if ($blocked.Count -gt 0) { Write-Host 'ERROR: Temporary/process output or an unregistered root path is staged:'; $blocked | ForEach-Object { Write-Host ('  ' + $_) }; Write-Host 'Delete/move temporary output to repo_ignored, or register a real root owner before pushing.'; exit 3 }"
if errorlevel 1 goto :fail

echo Verifying canonical Git recovery sentinels in the prospective index...
powershell -NoProfile -Command ^
    "$requiredFiles=@('.gitattributes','.gitignore','AGENTS.md','linux/kernel/v5_linux_source_identity.json','linux/realtime/v5_realtime_source_identity.json','linuxcnc/v5_linuxcnc_source_identity.json','board/petalinux/v5_petalinux_source_identity.json','board/petalinux/config.project','board/petalinux/project-spec/configs/config','board/petalinux/project-spec/configs/rootfs_config','board/petalinux/project-spec/hw-description/system.xsa','board/petalinux/project-spec/hw-description/system.bit','board/third_party/petalinux-source-packages/v5_source_packages.json','vivado_hw_project/vivado_hw_project.xpr','new-vivado/z20_v1_5_hw_project/z20_v1_5_hw_project.xpr','new-vivado/z20_v1_5_hw_project/board_inputs/system.xsa');" ^
    "$requiredPrefixes=@('linux/kernel/tools/build','linux/realtime/patches/build','linuxcnc/bin','linuxcnc/tcl/bin','board/tools/petalinux','board/petalinux/project-spec/meta-user','board/third_party/petalinux-source-packages');" ^
    "$missing=@();" ^
    "foreach ($path in $requiredFiles) { $matches=@(& git -c core.quotepath=false ls-files -- $path); if ($LASTEXITCODE -ne 0 -or $matches.Count -ne 1) { $missing += $path } };" ^
    "foreach ($prefix in $requiredPrefixes) { $matches=@(& git -c core.quotepath=false ls-files -- ($prefix + '/**')); if ($LASTEXITCODE -ne 0 -or $matches.Count -eq 0) { $missing += ($prefix + '/**') } };" ^
    "if ($missing.Count -gt 0) { Write-Host 'ERROR: Required Git recovery owner/sentinel is absent from the index:'; $missing | Sort-Object -Unique | ForEach-Object { Write-Host ('  ' + $_) }; exit 4 };" ^
    "Write-Host 'Git recovery sentinels: OK.'"
if errorlevel 1 goto :fail

echo [3/10] Checking staged whitespace errors...
git --no-pager diff --cached --check
if errorlevel 1 (
    echo ERROR: Staged whitespace validation failed.
    goto :fail
)

echo [4/10] Reviewing the full snapshot before commit and push...
set "CHANGE_COUNT="
for /f "delims=" %%C in ('powershell -NoProfile -Command "$files = @(& git diff --cached --name-only '%REMOTE%/%BRANCH%' --); if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; $files.Count"') do set "CHANGE_COUNT=%%C"
if not defined CHANGE_COUNT (
    echo ERROR: Unable to count the files that would be pushed.
    goto :fail
)

echo.
echo Change count: !CHANGE_COUNT!
powershell -NoProfile -Command "$added=0L; $deleted=0L; $rows=@(& git diff --cached --numstat '%REMOTE%/%BRANCH%' --); if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; foreach ($row in $rows) { $parts=$row -split '\t',3; if ($parts.Length -lt 2) { continue }; $number=0L; if ([long]::TryParse($parts[0],[ref]$number)) { $added += $number }; $number=0L; if ([long]::TryParse($parts[1],[ref]$number)) { $deleted += $number } }; $esc=[char]27; Write-Output ('Line changes: ' + $esc + '[92m+' + $added + $esc + '[0m ' + $esc + '[91m-' + $deleted + $esc + '[0m')" || goto :fail
echo Changed files ^(status and path^):
if "!CHANGE_COUNT!"=="0" (
    echo   ^(none^)
) else (
    git -c core.quotepath=false --no-pager diff --cached --name-status %REMOTE%/%BRANCH% -- || goto :fail
)
echo.

if "!CHECK_ONLY!"=="1" (
    echo CHECK PASSED: staging, exclusion and whitespace validation succeeded.
    echo No commit, Git LFS upload or push was performed.
    exit /b 0
)

git diff --cached --quiet
if errorlevel 1 (
    echo [5/10] Creating a recovery snapshot commit...
    for /f "delims=" %%T in ('powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd_HH-mm-ss"') do set "STAMP=%%T"
    git commit -m "backup: full V5 snapshot !STAMP!" || goto :fail
) else (
    echo [5/10] No uncommitted changes; using the current HEAD.
)

echo [6/10] Uploading every reachable Git LFS object...
git lfs push --all %REMOTE% %BRANCH% || goto :fail

echo [7/10] Pushing %BRANCH% to %REMOTE%...
git push %REMOTE% %BRANCH% || goto :fail

echo [8/10] Verifying the local Git and LFS object stores...
git fsck --full --strict --no-dangling || goto :fail
git lfs fsck || goto :fail

echo [9/10] Refreshing the remote reference...
git fetch %REMOTE% %BRANCH% || goto :fail

for /f "delims=" %%H in ('git rev-parse HEAD') do set "LOCAL_HEAD=%%H"
for /f "delims=" %%H in ('git rev-parse %REMOTE%/%BRANCH%') do set "REMOTE_HEAD=%%H"
if /i not "%LOCAL_HEAD%"=="%REMOTE_HEAD%" (
    echo ERROR: Verification failed.
    echo Local:  %LOCAL_HEAD%
    echo Remote: %REMOTE_HEAD%
    goto :fail
)

echo [10/10] Checking for files changed during the push...
if not exist "%REPO%\AGENTS.md" (
    echo ERROR: AGENTS.md disappeared during the push.
    goto :fail
)
git cat-file -e HEAD:AGENTS.md 2>nul || (
    echo ERROR: The pushed HEAD does not contain AGENTS.md.
    goto :fail
)
for /f "delims=" %%S in ('git status --porcelain') do (
    echo ERROR: The working tree changed while the push was running.
    git status --short
    goto :fail
)

echo.
echo SUCCESS: Full V5 Git snapshot is on %REMOTE%/%BRANCH%.
echo HEAD: %LOCAL_HEAD%
echo.
pause
exit /b 0

:stage_snapshot
for /l %%A in (1,1,3) do (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare_v5_git_snapshot.ps1" -Repository "%REPO%" -BaselineRef "%REMOTE%/%BRANCH%"
    if errorlevel 1 exit /b 1

    git add -A -- .
    if not errorlevel 1 (
        git rm -r --cached --ignore-unmatch -- "8ax-win/publish" "8ax-dealer-client-source/publish" "8ax-factory-client-source/publish"
        if errorlevel 1 exit /b 1
        exit /b 0
    )

    if %%A LSS 3 echo WARNING: staging attempt %%A failed; rerunning the root preflight before retry.
)
echo ERROR: Staging failed after three sanitized attempts.
exit /b 1

:fail
echo.
echo FAILED: Full V5 push did not complete. Review the error above.
echo No VM or board operation was performed.
echo.
pause
exit /b 1
