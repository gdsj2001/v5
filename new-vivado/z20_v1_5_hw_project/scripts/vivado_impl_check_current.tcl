set script_dir [file dirname [file normalize [info script]]]
set project_dir [file normalize [file join $script_dir ".."]]
set project_file "z20_v1_5_hw_project.xpr"
set impl_run_dir $project_dir/z20_v1_5_hw_project.runs/impl_1
set route_dcp_file $impl_run_dir/system_wrapper_routed.dcp
set timing_rpt $impl_run_dir/system_wrapper_timing_summary_routed.rpt
set timing_history_csv $project_dir/artifacts/vivado/timing_history.csv

proc persist_project_state {project_file stage} {
  puts "INFO: project state checkpoint reached after $stage"
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
  if {![file exists $timing_rpt]} {
    return [dict create source $source destination $destination path_group $path_group owner N/A]
  }

  set fp [open $timing_rpt r]
  set lines [split [read $fp] "\n"]
  close $fp

  # Prefer the first violated max-delay path; fallback to the first max-delay path.
  set use_block_start 0
  for {set i 0} {$i < [llength $lines]} {incr i} {
    set line [lindex $lines $i]
    if {[regexp {^\s*Slack\s+\(VIOLATED\)} $line]} {
      set use_block_start $i
      break
    }
  }
  if {$use_block_start == 0} {
    for {set i 0} {$i < [llength $lines]} {incr i} {
      set line [lindex $lines $i]
      if {[string first "Max Delay Paths" $line] >= 0} {
        set use_block_start $i
        break
      }
    }
  }

  for {set i $use_block_start} {$i < [llength $lines]} {incr i} {
    set line [lindex $lines $i]
    if {$source eq "N/A" && [regexp {^\s*Source:\s+(.+)$} $line -> v]} {
      set source [string trim $v]
      continue
    }
    if {$destination eq "N/A" && [regexp {^\s*Destination:\s+(.+)$} $line -> v]} {
      set destination [string trim $v]
      continue
    }
    if {$path_group eq "N/A" && [regexp {^\s*Path Group:\s+(.+)$} $line -> v]} {
      set path_group [string trim $v]
      if {$source ne "N/A" && $destination ne "N/A"} {
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

cd $project_dir
open_project $project_file
update_compile_order -fileset sources_1
persist_project_state $project_file "compile order refresh"
set impl_run [get_runs impl_1]
# Keep check-only implementation settings aligned with the full bitstream build.
set_property STEPS.OPT_DESIGN.ARGS.DIRECTIVE Explore $impl_run
set_property STEPS.PLACE_DESIGN.ARGS.DIRECTIVE ExtraNetDelay_high $impl_run
set_property STEPS.PHYS_OPT_DESIGN.ARGS.DIRECTIVE AggressiveExplore $impl_run
set_property STEPS.ROUTE_DESIGN.ARGS.DIRECTIVE MoreGlobalIterations $impl_run
set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.IS_ENABLED true $impl_run
set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.ARGS.DIRECTIVE AggressiveExplore $impl_run
set jobs 8
if {[info exists ::env(VIVADO_JOBS)]} {
  set jobs $::env(VIVADO_JOBS)
}
set impl_start_epoch [clock seconds]
reset_run impl_1
launch_runs impl_1 -to_step route_design -jobs $jobs
wait_on_run impl_1

set impl_status [get_property STATUS $impl_run]
puts "IMPL_STATUS_RAW=$impl_status"
persist_project_state $project_file "impl_1 route completion"

if {[string first "route_design Complete" $impl_status] < 0} {
  close_project
  exit 3
}

open_run impl_1
report_drc -file "$impl_run_dir/system_wrapper_drc_routed.rpt"
report_timing_summary -file "$timing_rpt"
report_pulse_width -file "$impl_run_dir/system_wrapper_pulse_width_routed.rpt"
persist_project_state $project_file "impl_1 route reports"

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

set route_dcp_fresh 0
if {[file exists $route_dcp_file]} {
  if {[file mtime $route_dcp_file] >= $impl_start_epoch} {
    set route_dcp_fresh 1
  }
}

set timing_rpt_fresh 0
if {[file exists $timing_rpt]} {
  if {[file mtime $timing_rpt] >= $impl_start_epoch} {
    set timing_rpt_fresh 1
  }
}

set output_file N/A
set build_status route_reports_missing
if {$route_dcp_fresh} {
  set output_file $route_dcp_file
  set build_status route_complete
} elseif {$timing_rpt_fresh} {
  set output_file $timing_rpt
  set build_status route_reports_generated
}

puts "BUILD_STATUS=$build_status"
puts "TIMING_STATUS=$timing_status"
puts "TIMING_WNS=[dict get $timing_dict wns] TIMING_TNS=[dict get $timing_dict tns] TIMING_WHS=[dict get $timing_dict whs] TIMING_THS=[dict get $timing_dict ths]"
puts "WORST_PATH_OWNER=[dict get $path_dict owner] GROUP=[dict get $path_dict path_group]"
puts "WORST_PATH_SOURCE=[dict get $path_dict source]"
puts "WORST_PATH_DEST=[dict get $path_dict destination]"

append_timing_history $timing_history_csv $build_status $timing_status $timing_dict $path_dict $impl_status $output_file

close_project
if {!$timing_rpt_fresh} {
  exit 4
}
exit 0
