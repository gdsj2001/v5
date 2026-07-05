set script_dir [file dirname [file normalize [info script]]]
set project_dir [file normalize [file join $script_dir ".."]]
set project_file "z20_v1_5_hw_project.xpr"
set impl_run_dir_rel "z20_v1_5_hw_project.runs/impl_1"
set impl_run_dir [file join $project_dir $impl_run_dir_rel]
set top_module "system_top"
set bit_file [file join $impl_run_dir ${top_module}.bit]
set bit_file_history [file join $impl_run_dir_rel ${top_module}.bit]
set timing_rpt $impl_run_dir/${top_module}_timing_summary_routed.rpt
set timing_history_csv $project_dir/artifacts/vivado/timing_history.csv

proc persist_project_state {project_file stage} {
  puts "INFO: project state checkpoint reached after $stage"
}

proc apply_gmii_pre_oddr_estop_patch {script_dir project_dir} {
  set patch_script [file join $script_dir "patch_gmii_pre_oddr_estop_gate.ps1"]
  if {![file exists $patch_script]} {
    error "Missing GMII pre-ODDR E-stop patch script: $patch_script"
  }
  set powershell_cmd [auto_execok powershell]
  if {$powershell_cmd eq ""} {
    set powershell_cmd [auto_execok pwsh]
  }
  if {$powershell_cmd eq ""} {
    error "Cannot find powershell or pwsh for GMII pre-ODDR E-stop patch"
  }
  set patch_output [exec {*}$powershell_cmd -NoProfile -ExecutionPolicy Bypass -File [file nativename $patch_script] -ProjectDir [file nativename $project_dir]]
  puts $patch_output
}

proc csv_escape {s} {
  set quoted [string map {"\"" "\"\""} $s]
  return "\"$quoted\""
}

proc parse_timing_summary {timing_rpt} {
  set result [dict create wns NA tns NA whs NA ths NA]
  if {![file exists $timing_rpt]} {
    return $result
  }
  set fp [open $timing_rpt r]
  set data [read $fp]
  close $fp
  foreach line [split $data "\n"] {
    if {[regexp {^\s*(-?[0-9]+\.[0-9]+)\s+(-?[0-9]+\.[0-9]+)\s+[0-9]+\s+[0-9]+\s+(-?[0-9]+\.[0-9]+)\s+(-?[0-9]+\.[0-9]+)\s+[0-9]+\s+[0-9]+} $line -> wns tns whs ths]} {
      dict set result wns $wns
      dict set result tns $tns
      dict set result whs $whs
      dict set result ths $ths
      break
    }
  }
  return $result
}

proc parse_worst_path {timing_rpt} {
  set source N/A
  set destination N/A
  set path_group N/A
  set best_slack ""
  if {![file exists $timing_rpt]} {
    return [dict create source $source destination $destination path_group $path_group owner N/A]
  }

  set fp [open $timing_rpt r]
  set lines [split [read $fp] "\n"]
  close $fp

  set in_max_delay 0
  for {set i 0} {$i < [llength $lines]} {incr i} {
    set line [lindex $lines $i]
    if {[string first "Max Delay Paths" $line] >= 0} {
      set in_max_delay 1
      continue
    }
    if {[string first "Min Delay Paths" $line] >= 0 || [string first "Pulse Width Checks" $line] >= 0} {
      set in_max_delay 0
      continue
    }
    if {!$in_max_delay} {
      continue
    }
    if {[regexp {^\s*Slack\s+\((MET|VIOLATED)\)\s*:\s*(-?[0-9]+\.[0-9]+)ns} $line -> _ slack]} {
      set cand_source N/A
      set cand_destination N/A
      set cand_path_group N/A
      set last_j [expr {$i + 80}]
      if {$last_j >= [llength $lines]} {
        set last_j [expr {[llength $lines] - 1}]
      }
      for {set j [expr {$i + 1}]} {$j <= $last_j} {incr j} {
        set path_line [lindex $lines $j]
        if {[regexp {^\s*Slack\s+\((MET|VIOLATED)\)} $path_line]} {
          break
        }
        if {$cand_source eq "N/A" && [regexp {^\s*Source:\s+(.+)$} $path_line -> v]} {
          set cand_source [string trim $v]
          continue
        }
        if {$cand_destination eq "N/A" && [regexp {^\s*Destination:\s+(.+)$} $path_line -> v]} {
          set cand_destination [string trim $v]
          continue
        }
        if {$cand_path_group eq "N/A" && [regexp {^\s*Path Group:\s+(.+)$} $path_line -> v]} {
          set cand_path_group [string trim $v]
        }
        if {$cand_source ne "N/A" && $cand_destination ne "N/A" && $cand_path_group ne "N/A"} {
          break
        }
      }
      if {$cand_source ne "N/A" && $cand_destination ne "N/A"} {
        if {$best_slack eq "" || $slack < $best_slack} {
          set best_slack $slack
          set source $cand_source
          set destination $cand_destination
          set path_group $cand_path_group
        }
      }
    }
  }

  if {$best_slack eq ""} {
    for {set i 0} {$i < [llength $lines]} {incr i} {
      set line [lindex $lines $i]
      if {[string first "Max Delay Paths" $line] >= 0} {
        set in_max_delay 1
        continue
      }
      if {!$in_max_delay} {
        continue
      }
      if {[regexp {^\s*Source:\s+(.+)$} $line -> v]} {
        set source [string trim $v]
        continue
      }
      if {[regexp {^\s*Destination:\s+(.+)$} $line -> v]} {
        set destination [string trim $v]
        continue
      }
      if {[regexp {^\s*Path Group:\s+(.+)$} $line -> v]} {
        set path_group [string trim $v]
        break
      }
    }
  }

  set owner N/A
  if {[regexp {system_i/([^/]+)/} $source -> module_name]} {
    set owner $module_name
  }

  return [dict create source $source destination $destination path_group $path_group owner $owner]
}

proc append_timing_history {csv_file build_status timing_status timing_dict path_dict impl_status bit_file} {
  file mkdir [file dirname $csv_file]
  set write_header [expr {![file exists $csv_file]}]
  set fp [open $csv_file a]
  if {$write_header} {
    puts $fp "timestamp,build_status,timing_status,wns,tns,whs,ths,worst_owner,worst_path_group,worst_source,worst_destination,impl_status,bit_file"
  }
  set ts [clock format [clock seconds] -format {%Y-%m-%d %H:%M:%S}]
  set row [join [list \
    [csv_escape $ts] \
    [csv_escape $build_status] \
    [csv_escape $timing_status] \
    [csv_escape [dict get $timing_dict wns]] \
    [csv_escape [dict get $timing_dict tns]] \
    [csv_escape [dict get $timing_dict whs]] \
    [csv_escape [dict get $timing_dict ths]] \
    [csv_escape [dict get $path_dict owner]] \
    [csv_escape [dict get $path_dict path_group]] \
    [csv_escape [dict get $path_dict source]] \
    [csv_escape [dict get $path_dict destination]] \
    [csv_escape $impl_status] \
    [csv_escape $bit_file] \
  ] ","]
  puts $fp $row
  close $fp
}

proc wait_for_fresh_bitstream {bit_file start_epoch timeout_seconds} {
  set deadline [expr {[clock seconds] + $timeout_seconds}]
  while {[clock seconds] <= $deadline} {
    if {[file exists $bit_file] && [file mtime $bit_file] >= $start_epoch} {
      return 1
    }
    after 1000
  }
  return 0
}

proc env_or_default {name default_value} {
  if {[info exists ::env($name)] && $::env($name) ne ""} {
    return $::env($name)
  }
  return $default_value
}

proc refresh_bd_output_products {} {
  set bd_obj [get_files -quiet system.bd]
  if {[llength $bd_obj] == 0} {
    error "Cannot refresh BD output products; system.bd not found"
  }
  update_ip_catalog -rebuild -quiet
  reset_target all $bd_obj
  generate_target all $bd_obj
  export_ip_user_files -of_objects $bd_obj -no_script -sync -force -quiet
}

cd $project_dir
apply_gmii_pre_oddr_estop_patch $script_dir $project_dir
open_project $project_file
refresh_bd_output_products
update_compile_order -fileset sources_1
apply_gmii_pre_oddr_estop_patch $script_dir $project_dir
update_compile_order -fileset sources_1
persist_project_state $project_file "compile order refresh"
set impl_run [get_runs impl_1]
set impl_strategy [env_or_default VIVADO_IMPL_STRATEGY ""]
if {$impl_strategy ne ""} {
  set_property STRATEGY $impl_strategy $impl_run
}
# Route-driven closure for the small setup margin. If VIVADO_IMPL_STRATEGY is
# set, inherit the strategy's directives unless a step override is provided.
if {$impl_strategy ne ""} {
  set default_opt_directive [get_property STEPS.OPT_DESIGN.ARGS.DIRECTIVE $impl_run]
  set default_place_directive [get_property STEPS.PLACE_DESIGN.ARGS.DIRECTIVE $impl_run]
  set default_phys_opt_directive [get_property STEPS.PHYS_OPT_DESIGN.ARGS.DIRECTIVE $impl_run]
  set default_route_directive [get_property STEPS.ROUTE_DESIGN.ARGS.DIRECTIVE $impl_run]
  set default_post_route_phys_opt_directive [get_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.ARGS.DIRECTIVE $impl_run]
} else {
  set default_opt_directive Explore
  set default_place_directive ExtraNetDelay_high
  set default_phys_opt_directive AggressiveExplore
  set default_route_directive MoreGlobalIterations
  set default_post_route_phys_opt_directive AggressiveExplore
}
set opt_directive [env_or_default VIVADO_OPT_DIRECTIVE $default_opt_directive]
set place_directive [env_or_default VIVADO_PLACE_DIRECTIVE $default_place_directive]
set phys_opt_directive [env_or_default VIVADO_PHYS_OPT_DIRECTIVE $default_phys_opt_directive]
set route_directive [env_or_default VIVADO_ROUTE_DIRECTIVE $default_route_directive]
set post_route_phys_opt_directive [env_or_default VIVADO_POST_ROUTE_PHYS_OPT_DIRECTIVE $default_post_route_phys_opt_directive]
puts "IMPL_STRATEGY=$impl_strategy"
puts "IMPL_OPT_DIRECTIVE=$opt_directive"
puts "IMPL_PLACE_DIRECTIVE=$place_directive"
puts "IMPL_PHYS_OPT_DIRECTIVE=$phys_opt_directive"
puts "IMPL_ROUTE_DIRECTIVE=$route_directive"
puts "IMPL_POST_ROUTE_PHYS_OPT_DIRECTIVE=$post_route_phys_opt_directive"
set_property STEPS.OPT_DESIGN.ARGS.DIRECTIVE $opt_directive $impl_run
set_property STEPS.PLACE_DESIGN.ARGS.DIRECTIVE $place_directive $impl_run
set_property STEPS.PHYS_OPT_DESIGN.ARGS.DIRECTIVE $phys_opt_directive $impl_run
set_property STEPS.ROUTE_DESIGN.ARGS.DIRECTIVE $route_directive $impl_run
set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.IS_ENABLED true $impl_run
set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.ARGS.DIRECTIVE $post_route_phys_opt_directive $impl_run
set jobs 8
if {[info exists ::env(VIVADO_JOBS)]} {
  set jobs $::env(VIVADO_JOBS)
}
set impl_start_epoch [clock seconds]
reset_run synth_1
reset_run impl_1
launch_runs synth_1 -jobs $jobs
wait_on_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs $jobs
wait_on_run impl_1

set impl_status [get_property STATUS $impl_run]
puts "IMPL_STATUS_RAW=$impl_status"
persist_project_state $project_file "impl_1 completion"

set bit_fresh [wait_for_fresh_bitstream $bit_file $impl_start_epoch 600]
set build_status [expr {$bit_fresh ? "bitstream_generated" : "bitstream_missing"}]

open_run impl_1
report_drc -file "$impl_run_dir/${top_module}_drc_routed.rpt"
report_timing_summary -file "$timing_rpt"
report_pulse_width -file "$impl_run_dir/${top_module}_pulse_width_routed.rpt"
persist_project_state $project_file "impl_1 reports"

set timing_dict [parse_timing_summary $timing_rpt]
set path_dict [parse_worst_path $timing_rpt]

set timing_status timing_unknown
if {([dict get $timing_dict wns] ne "NA") && ([dict get $timing_dict whs] ne "NA")} {
  if {([dict get $timing_dict wns] >= 0.0) && ([dict get $timing_dict whs] >= 0.0)} {
    set timing_status timing_met
  } else {
    set timing_status timing_not_met
  }
}

puts "BUILD_STATUS=$build_status"
puts "TIMING_STATUS=$timing_status"
puts "TIMING_WNS=[dict get $timing_dict wns] TIMING_TNS=[dict get $timing_dict tns] TIMING_WHS=[dict get $timing_dict whs] TIMING_THS=[dict get $timing_dict ths]"
puts "WORST_PATH_OWNER=[dict get $path_dict owner] GROUP=[dict get $path_dict path_group]"
puts "WORST_PATH_SOURCE=[dict get $path_dict source]"
puts "WORST_PATH_DEST=[dict get $path_dict destination]"

append_timing_history $timing_history_csv $build_status $timing_status $timing_dict $path_dict $impl_status $bit_file_history

close_project
if {!$bit_fresh} {
  exit 4
}
exit 0
