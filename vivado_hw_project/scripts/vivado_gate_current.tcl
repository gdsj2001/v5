set script_dir [file dirname [file normalize [info script]]]
set project_dir [file normalize [file join $script_dir ".."]]
set project_file $project_dir/vivado_hw_project.xpr
set impl_run_dir $project_dir/vivado_hw_project.runs/impl_1
set synth_run_dir $project_dir/vivado_hw_project.runs/synth_1
set bit_file $impl_run_dir/system_wrapper.bit
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

proc append_timing_history {csv_file build_status timing_status timing_dict path_dict synth_status impl_status bit_file} {
  file mkdir [file dirname $csv_file]
  set write_header [expr {![file exists $csv_file]}]
  set fp [open $csv_file a]
  if {$write_header} {
    puts $fp "timestamp,build_status,timing_status,wns,tns,whs,ths,worst_owner,worst_path_group,worst_source,worst_destination,synth_status,impl_status,bit_file"
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
    [csv_escape $synth_status] \
    [csv_escape $impl_status] \
    [csv_escape $bit_file] \
  ] ","]
  puts $fp $row
  close $fp
}

proc apply_dvi_reset_async_exceptions {} {
  set pins [get_pins -hier -quiet -filter {REF_PIN_NAME == PRE && NAME =~ *DVI_Transmitter_0*/reset_syn_*/*_reg/PRE}]
  puts "DVI_RESET_ASYNC_EXCEPTION_PINS=[llength $pins]"
  if {[llength $pins] > 0} {
    set_false_path -to $pins
  }
}

open_project $project_file
update_compile_order -fileset sources_1
persist_project_state $project_file "compile order refresh"
open_bd_design [get_files $project_dir/vivado_hw_project.srcs/sources_1/bd/system/system.bd]
validate_bd_design
save_bd_design
persist_project_state $project_file "bd validation"

reset_run synth_1
launch_runs synth_1 -jobs 8
wait_on_run synth_1

set synth_status [get_property STATUS [get_runs synth_1]]
puts "SYNTH_STATUS=$synth_status"
if {[string match "*Complete*" $synth_status] == 0} {
  puts "Synthesis did not complete successfully."
  close_project
  exit 1
}
catch {open_run synth_1}
persist_project_state $project_file "synth_1 completion"

set impl_run [get_runs impl_1]
set_property STEPS.OPT_DESIGN.ARGS.DIRECTIVE Explore $impl_run
set_property STEPS.PLACE_DESIGN.ARGS.DIRECTIVE ExtraNetDelay_high $impl_run
set_property STEPS.PHYS_OPT_DESIGN.ARGS.DIRECTIVE AggressiveExplore $impl_run
set_property STEPS.ROUTE_DESIGN.ARGS.DIRECTIVE MoreGlobalIterations $impl_run
set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.IS_ENABLED true $impl_run
set_property STEPS.POST_ROUTE_PHYS_OPT_DESIGN.ARGS.DIRECTIVE AggressiveExplore $impl_run

set impl_start_epoch [clock seconds]
reset_run impl_1
launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1

set impl_status [get_property STATUS [get_runs impl_1]]
puts "IMPL_STATUS_RAW=$impl_status"
persist_project_state $project_file "impl_1 completion"

set bit_fresh 0
if {[file exists $bit_file]} {
  if {[file mtime $bit_file] >= $impl_start_epoch} {
    set bit_fresh 1
  }
}

set build_status [expr {$bit_fresh ? "bitstream_generated" : "bitstream_missing"}]

open_run impl_1
apply_dvi_reset_async_exceptions
report_drc -file "$impl_run_dir/system_wrapper_drc_routed.rpt"
report_timing_summary -file "$timing_rpt"
report_pulse_width -file "$impl_run_dir/system_wrapper_pulse_width_routed.rpt"
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

append_timing_history $timing_history_csv $build_status $timing_status $timing_dict $path_dict $synth_status $impl_status $bit_file
persist_project_state $project_file "timing history append"

if {!$bit_fresh} {
  puts "Implementation did not produce a fresh bitstream in this run."
  close_project
  exit 1
}

close_project
exit 0
