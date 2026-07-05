set script_dir [file dirname [file normalize [info script]]]
set project_dir [file normalize [file join $script_dir ".."]]
set rtl_file [file join $project_dir "rtl" "z20_dna_reader_axi_lite.v"]

read_verilog $rtl_file
synth_design -top z20_dna_reader_axi_lite -part xc7z020clg484-2
report_utilization -file [file join $project_dir "reports" "z20_dna_reader_utilization_synth.rpt"]
exit 0
