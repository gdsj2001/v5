function Get-ProjectPath {
  param(
    [string]$RootDir,
    [string]$RelativePath
  )
  return Join-Path $RootDir ($RelativePath -replace '/', [System.IO.Path]::DirectorySeparatorChar)
}

function Read-ProjectText {
  param(
    [string]$RootDir,
    [string]$RelativePath
  )

  $path = Get-ProjectPath -RootDir $RootDir -RelativePath $RelativePath
  if (-not (Test-Path -LiteralPath $path)) {
    throw "Missing required file: $RelativePath"
  }
  return Get-Content -LiteralPath $path -Raw -Encoding UTF8
}

function Get-VivadoWarningRows {
  param([string]$ProjectDir)

  $logs = @(
    @{ phase = 'synth'; path = 'z20_v1_5_hw_project.runs/synth_1/runme.log' },
    @{ phase = 'impl'; path = 'z20_v1_5_hw_project.runs/impl_1/runme.log' }
  )

  $rows = New-Object System.Collections.Generic.List[object]
  foreach ($log in $logs) {
    $text = Read-ProjectText -RootDir $ProjectDir -RelativePath $log.path
    $lineNumber = 0
    foreach ($line in ($text -split '\r?\n')) {
      $lineNumber += 1
      if ($line -notmatch '^WARNING:') {
        continue
      }
      $code = 'NO_CODE'
      if ($line -match '^WARNING:\s*\[(?<code>[^\]]+)\]') {
        $code = $Matches.code
      }
      $rows.Add([pscustomobject]@{
          phase = $log.phase
          source = $log.path
          line_number = $lineNumber
          code = $code
          line = $line
        })
    }
  }
  return $rows.ToArray()
}

function Get-VivadoWarningClassification {
  param(
    [string]$Code,
    [string]$Line
  )

  switch ($Code) {
    'Common 17-1361' {
      return [pscustomobject]@{
        classification = 'vivado_message_config_duplicate'
        owner = 'generated_bd_ip_flow'
        next_action = 'cleanup_optional'
        allowed = $true
      }
    }
    'Vivado 12-2489' {
      return [pscustomobject]@{
        classification = 'ps7_generated_clock_jitter_rounding'
        owner = 'generated_ps7_xdc'
        next_action = 'monitor'
        allowed = $true
      }
    }
    'Vivado 12-180' {
      return [pscustomobject]@{
        classification = 'xpm_memory_xdc_no_cells'
        owner = 'vivado_xpm_ip'
        next_action = 'monitor'
        allowed = $true
      }
    }
    'Synth 8-6901' {
      return [pscustomobject]@{
        classification = 'retired_hdmi_dvi_encoder_regression'
        owner = 'retired_hdmi_must_not_reappear'
        next_action = 'remove_retired_hdmi_bd_or_source_residue'
        allowed = $false
      }
    }
    'Synth 8-2048' {
      return [pscustomobject]@{
        classification = 'generated_axi_iic_function_return'
        owner = 'vivado_axi_iic_ip'
        next_action = 'monitor'
        allowed = $true
      }
    }
    'Synth 8-7071' {
      return [pscustomobject]@{
        classification = 'generated_ip_unconnected_port'
        owner = 'generated_bd_ip'
        next_action = 'monitor_or_remove_retired_ip_owner'
        allowed = $true
      }
    }
    'Synth 8-7023' {
      return [pscustomobject]@{
        classification = 'generated_ip_partial_instance_connections'
        owner = 'generated_bd_ip'
        next_action = 'monitor_or_remove_retired_ip_owner'
        allowed = $true
      }
    }
    'Synth 8-7129' {
      return [pscustomobject]@{
        classification = 'generated_ip_unloaded_port'
        owner = 'generated_bd_ip'
        next_action = 'monitor_or_remove_retired_ip_owner'
        allowed = $true
      }
    }
    'Synth 8-5396' {
      return [pscustomobject]@{
        classification = 'xpm_cdc_keep_attribute'
        owner = 'vivado_xpm_ip'
        next_action = 'monitor'
        allowed = $true
      }
    }
    'Synth 8-3332' {
      return [pscustomobject]@{
        classification = 'generated_ip_trimmed_unused_sequential'
        owner = 'generated_bd_ip'
        next_action = 'monitor'
        allowed = $true
      }
    }
    'Synth 8-589' {
      return [pscustomobject]@{
        classification = 'hls_frozen_case_equality_rewrite'
        owner = 'hls_frozen_ip'
        next_action = 'monitor'
        allowed = $true
      }
    }
    'Synth 8-3936' {
      return [pscustomobject]@{
        classification = 'hls_frozen_internal_register_trim'
        owner = 'hls_frozen_ip'
        next_action = 'monitor'
        allowed = $true
      }
    }
    'Opt 31-163' {
      return [pscustomobject]@{
        classification = 'retired_hdmi_unobservable_serdes_regression'
        owner = 'retired_hdmi_must_not_reappear'
        next_action = 'remove_retired_hdmi_bd_or_source_residue'
        allowed = $false
      }
    }
    'Opt 31-32' {
      return [pscustomobject]@{
        classification = 'redundant_ibuf_removed'
        owner = 'generated_bd_ip'
        next_action = 'monitor_or_remove_retired_ip_owner'
        allowed = $true
      }
    }
    'Synth 8-5785' {
      return [pscustomobject]@{
        classification = 'constant_propagation_removed_ram'
        owner = 'generated_bd_or_tx_gate_logic'
        next_action = 'monitor'
        allowed = $true
      }
    }
    'Synth 8-3295' {
      return [pscustomobject]@{
        classification = 'xpm_cdc_undriven_pin_tied_zero'
        owner = 'vivado_xpm_ip'
        next_action = 'monitor'
        allowed = $true
      }
    }
    'Power 33-332' {
      return [pscustomobject]@{
        classification = 'power_analysis_high_fanout_reset_activity'
        owner = 'vivado_power_estimator'
        next_action = 'monitor'
        allowed = $true
      }
    }
    'Timing 38-436' {
      return [pscustomobject]@{
        classification = 'bus_skew_report_reminder'
        owner = 'vivado_timing_report'
        next_action = 'run_bus_skew_before_board_release'
        allowed = $true
      }
    }
    'DRC RTSTAT-10' {
      return [pscustomobject]@{
        classification = 'allowed_drc_no_routable_loads'
        owner = 'generated_bd_unused_internal_nets'
        next_action = 'monitor'
        allowed = $true
      }
    }
    'Route 35-328' {
      return [pscustomobject]@{
        classification = 'router_estimated_timing_not_met_intermediate'
        owner = 'vivado_route_estimate'
        next_action = 'require_final_timing_summary_timing_met'
        allowed = $true
      }
    }
    default {
      return [pscustomobject]@{
        classification = 'unexpected_warning_code'
        owner = 'unknown'
        next_action = 'investigate_before_handoff'
        allowed = $false
      }
    }
  }
}

function Get-VivadoWarningSummaryRows {
  param([object[]]$Warnings)

  function Convert-ToPortableWarningLine {
    param([string]$Line)
    if ($null -eq $Line) {
      return ''
    }
    return ($Line -replace '(?<![A-Za-z])[A-Za-z]:[\\/][^\]\s,")]+', '$ABS_PATH')
  }

  $summary = New-Object System.Collections.Generic.List[object]
  foreach ($group in ($Warnings | Group-Object code | Sort-Object -Property @{ Expression = 'Count'; Descending = $true }, Name)) {
    $first = $group.Group[0]
    $class = Get-VivadoWarningClassification -Code $group.Name -Line $first.line
    $phaseCounts = @(
      $group.Group |
        Group-Object phase |
        Sort-Object Name |
        ForEach-Object { "$($_.Name):$($_.Count)" }
    ) -join ';'
    $summary.Add([pscustomobject]@{
        code = $group.Name
        count = $group.Count
        phases = $phaseCounts
        classification = $class.classification
        owner = $class.owner
        next_action = $class.next_action
        allowed = [string]$class.allowed
        representative_line = Convert-ToPortableWarningLine -Line $first.line
      })
  }
  return $summary.ToArray()
}

function Test-VivadoWarningPolicy {
  param([object[]]$Warnings)

  $unexpectedCodes = New-Object System.Collections.Generic.List[string]
  foreach ($row in $Warnings) {
    $class = Get-VivadoWarningClassification -Code $row.code -Line $row.line
    if (-not $class.allowed -and -not $unexpectedCodes.Contains($row.code)) {
      $unexpectedCodes.Add($row.code)
    }
  }

  $forbiddenPattern = 'z20-v1_5_20260623\.xdc|constraints_reference[\\/]system_old\.xdc|constrs_1[\\/]system\.xdc|\bNSTD-1\b|\bUCIO-1\b|No ports matched|set_property.*expects at least one object'
  $constraintTruthWarnings = @($Warnings | Where-Object { $_.line -match $forbiddenPattern })
  $retiredHdmiWarnings = @($Warnings | Where-Object { $_.line -match '(?i)hdmi|dvi|tmds' })

  return [pscustomobject]@{
    warning_count = @($Warnings).Count
    allowed_code_count = (@($Warnings | Group-Object code)).Count
    unexpected_warning_codes = ($unexpectedCodes -join ',')
    unexpected_warning_code_count = $unexpectedCodes.Count
    constraint_truth_warning_lines = $constraintTruthWarnings.Count
    retired_hdmi_warning_lines = $retiredHdmiWarnings.Count
  }
}
