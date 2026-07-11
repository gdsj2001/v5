function Get-RequiredGitScopeSentinels {
    return @(
        "AGENTS.md",
        ".gitignore",
        ".gitattributes",
        "功能\需求真源索引.md",
        "系统代码架构硬边界守则.md",
        "项目软硬件架构和后期修改指导说明.md",
        "linux\kernel\Makefile",
        "linux\kernel\v5_linux_source_identity.json",
        "linux\kernel\tools\build\Makefile",
        "linux\realtime\v5_realtime_source_identity.json",
        "linux\realtime\patches\build\build.scc",
        "linuxcnc\VERSION",
        "linuxcnc\v5_linuxcnc_source_identity.json",
        "linuxcnc\bin\profile_axis",
        "linuxcnc\tcl\bin\halshow.tcl",
        "board\petalinux\v5_petalinux_source_identity.json",
        "board\petalinux\project-spec\configs\config",
        "board\petalinux\project-spec\hw-description\system.bit",
        "board\petalinux\project-spec\hw-description\system.xsa",
        "board\petalinux\project-spec\meta-user\recipes-apps\v5-base-overlay\v5-base-overlay.bb",
        "board\petalinux\project-spec\meta-user\recipes-apps\v5-stepgen-module\files\zynq_stepgen_hw.c",
        "board\tools\petalinux\verify_v5_petalinux_source.py",
        "board\tools\petalinux\v5_petalinux_overlay.sh",
        "vivado_hw_project\vivado_hw_project.xpr"
    )
}

function Assert-GitScopeFiles([string]$Repo) {
    foreach ($relativePath in (Get-RequiredGitScopeSentinels)) {
        if (-not (Test-Path -LiteralPath (Join-Path $Repo $relativePath) -PathType Leaf)) {
            Fail "Required v5 Git scope file is missing: $relativePath"
        }
    }
}

function Assert-GitScopeTracked([string]$Repo) {
    Assert-GitScopeFiles $Repo
    foreach ($relativePath in (Get-RequiredGitScopeSentinels)) {
        $tracked = Get-GitOutput $Repo @("ls-files", "--error-unmatch", "--", $relativePath)
        if ([string]::IsNullOrWhiteSpace($tracked)) {
            Fail "Required v5 Git scope file was not staged: $relativePath"
        }
    }
    Write-Host "Git scope verified: canonical Linux, LinuxCNC, PetaLinux, Vivado, rules, and docs are staged."
}

function Get-GitDryRunCount([string]$Repo, [string[]]$GitArgs) {
    $output = @(& git -C $Repo @GitArgs 2>$null)
    if ($LASTEXITCODE -ne 0) {
        Fail "git $($GitArgs -join ' ') dry-run failed."
    }
    return $output.Count
}

function Get-RobocopyExcludedDirectories([string]$SourceRoot) {
    $globalGeneratedNames = @(
        ".git",
        "repo_ignored",
        ".pytest_cache",
        ".mypy_cache",
        ".ruff_cache",
        "__pycache__",
        "node_modules",
        ".npm",
        ".pnpm-store",
        ".vs",
        "CMakeFiles",
        "htmlcov",
        ".Xil",
        "*.cache",
        "*.gen",
        "*.hw",
        "*.runs",
        "artifacts",
        "evidence",
        "evidence_*",
        "*_evidence",
        "*evidence*",
        "Temporary"
    )

    $scopedGeneratedPaths = @(
        (Join-Path $SourceRoot "build"),
        (Join-Path $SourceRoot "build-*"),
        (Join-Path $SourceRoot "cmake-build-*"),
        (Join-Path $SourceRoot "out"),
        (Join-Path $SourceRoot "dist"),
        (Join-Path $SourceRoot "vm-v5"),
        (Join-Path $SourceRoot "tmp"),
        (Join-Path $SourceRoot "temp"),
        (Join-Path $SourceRoot "logs"),
        (Join-Path $SourceRoot "bak"),
        (Join-Path $SourceRoot "board\tools\v5_git_nas_backup\bin"),
        (Join-Path $SourceRoot "board\tools\v5_git_nas_backup\obj"),
        (Join-Path $SourceRoot "8ax-win\src\8ax.WinRemote\bin"),
        (Join-Path $SourceRoot "8ax-win\src\8ax.WinRemote\obj"),
        (Join-Path $SourceRoot "8ax-win\tests\8ax.WinRemote.Tests\bin"),
        (Join-Path $SourceRoot "8ax-win\tests\8ax.WinRemote.Tests\obj"),
        (Join-Path $SourceRoot "8ax-win\tools\mock-relay\bin"),
        (Join-Path $SourceRoot "8ax-win\tools\mock-relay\obj"),
        (Join-Path $SourceRoot "8ax-dealer-client-source\8ax.DealerClient\bin"),
        (Join-Path $SourceRoot "8ax-dealer-client-source\8ax.DealerClient\obj"),
        (Join-Path $SourceRoot "8ax-factory-client-source\8ax.FactoryClient\bin"),
        (Join-Path $SourceRoot "8ax-factory-client-source\8ax.FactoryClient\obj")
    )

    return $globalGeneratedNames + $scopedGeneratedPaths
}

function Get-RobocopyExcludedFiles {
    return @(
        ".DS_Store",
        "Thumbs.db",
        "Desktop.ini",
        "*~",
        "*.swp",
        "*.swo",
        "*.tmp",
        "*.temp",
        "*.orig",
        "*.pyc",
        "*.pyo",
        ".coverage",
        "CMakeCache.txt",
        "cmake_install.cmake",
        "compile_commands.json",
        "*.jou",
        "*.str",
        "*.log",
        "*.wdb",
        "*.vcd",
        "*.zip",
        "*.7z",
        "*.rar",
        "*.tar",
        "*.tar.gz",
        "*.tgz",
        "*.tar.bz2",
        "*.tbz2",
        "*.tar.xz",
        "*.txz",
        "*.gz",
        "*.bz2",
        "*.xz",
        "*.zst",
        "*.deb",
        "*.rpm",
        "*.msi",
        "*.dmg",
        "*.pkg",
        "*.AppImage",
        "*.iso",
        "*.raw",
        "*.frame",
        "*.jsonl",
        "*frame*.json",
        "*metrics*.json",
        "*events*.json",
        "*summary*.json"
    )
}

function Remove-ExcludedGitTracking([string]$Repo) {
    $patterns = @(
        ":(glob)**/.DS_Store",
        ":(glob)**/Thumbs.db",
        ":(glob)**/Desktop.ini",
        ":(glob)**/*~",
        ":(glob)**/*.swp",
        ":(glob)**/*.swo",
        ":(glob)**/*.tmp",
        ":(glob)**/*.temp",
        ":(glob)**/*.orig",
        ":(glob)**/__pycache__/**",
        ":(glob)**/*.pyc",
        ":(glob)**/*.pyo",
        ":(glob)**/.pytest_cache/**",
        ":(glob)**/.mypy_cache/**",
        ":(glob)**/.ruff_cache/**",
        ":(glob)**/.coverage",
        ":(glob)**/htmlcov/**",
        ":(glob)**/node_modules/**",
        ":(glob)**/.npm/**",
        ":(glob)**/.pnpm-store/**",
        ":(glob)**/.vs/**",
        ":(glob)**/CMakeFiles/**",
        ":(glob)**/CMakeCache.txt",
        ":(glob)**/cmake_install.cmake",
        ":(glob)**/compile_commands.json",
        ":(top,glob)build/**",
        ":(top,glob)build-*/**",
        ":(top,glob)cmake-build-*/**",
        ":(top,glob)out/**",
        ":(top,glob)dist/**",
        ":(top,glob)vm-v5/**",
        ":(top,glob)tmp/**",
        ":(top,glob)temp/**",
        ":(top,glob)logs/**",
        ":(top,glob)bak/**",
        ":(glob)board/tools/v5_git_nas_backup/bin/**",
        ":(glob)board/tools/v5_git_nas_backup/obj/**",
        ":(glob)8ax-win/**/bin/**",
        ":(glob)8ax-win/**/obj/**",
        ":(glob)8ax-dealer-client-source/**/bin/**",
        ":(glob)8ax-dealer-client-source/**/obj/**",
        ":(glob)8ax-factory-client-source/**/bin/**",
        ":(glob)8ax-factory-client-source/**/obj/**",
        ":(glob)**/.Xil/**",
        ":(glob)**/*.cache/**",
        ":(glob)**/*.gen/**",
        ":(glob)**/*.hw/**",
        ":(glob)**/*.runs/**",
        ":(glob)**/*.jou",
        ":(glob)**/*.str",
        ":(glob)**/*.log",
        ":(glob)**/*.wdb",
        ":(glob)**/*.vcd",
        ":(glob)**/*.zip",
        ":(glob)**/*.7z",
        ":(glob)**/*.rar",
        ":(glob)**/*.tar",
        ":(glob)**/*.tar.gz",
        ":(glob)**/*.tgz",
        ":(glob)**/*.tar.bz2",
        ":(glob)**/*.tbz2",
        ":(glob)**/*.tar.xz",
        ":(glob)**/*.txz",
        ":(glob)**/*.gz",
        ":(glob)**/*.bz2",
        ":(glob)**/*.xz",
        ":(glob)**/*.zst",
        ":(glob)**/*.deb",
        ":(glob)**/*.rpm",
        ":(glob)**/*.msi",
        ":(glob)**/*.dmg",
        ":(glob)**/*.pkg",
        ":(glob)**/*.AppImage",
        ":(glob)**/*.iso",
        ":(glob)**/artifacts/**",
        ":(glob)**/evidence/**",
        ":(glob)**/evidence_*/**",
        ":(glob)**/*_evidence/**",
        ":(glob)**/*evidence*/**",
        ":(glob)repo_ignored/**",
        ":(glob)**/repo_ignored/**"
    )

    $gitArgs = @("rm", "--cached", "-r", "--ignore-unmatch", "--") + $patterns
    Invoke-GitChecked -Repo $Repo -GitArgs $gitArgs | Out-Null
}
