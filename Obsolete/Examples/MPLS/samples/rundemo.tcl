#!/bin/sh
# the next line restarts using wish (on cygwin, use wish83 or the like) \
exec wish "$0" "$@"

# use the 'demo' executable from this directory (1), or the individual simulation
# executables from the other subdirs (0) ?
set use_single_executable 0

foreach i {
  {mpls-ldp ../ldp-mpls1 "MPLS-LDP"}
   {testTE1 ../TestTE1 "CSPF Routing"}
   {testTE2 ../TestTE2  "Hop-by-Hop Routing"}
   {testTE3 ../TestTE3   "ER Routing"}
   {testTE4 ../TestTE4   "Traffic Reroute 1"}
   {testTE5 ../TestTE5  "Traffic Reroute 2"}
   {testTE6 ../TestTE6  "Reservation preemption"}
} {
   set sample [lindex $i 0]
   lappend samples $sample
   set dir($sample) [lindex $i 1]
   set name($sample) [lindex $i 2]
}


proc createWindow {} {
    global samples dir name fonts colors

    wm focusmodel . passive
    wm minsize . 1 1
    wm overrideredirect . 0
    wm resizable . 1 1
    wm deiconify .
    wm title . "OMNeT++ Demo"
    wm protocol . WM_DELETE_WINDOW {exit}

    #################################
    # Menu bar
    #################################

    menu .menubar
    . config -menu .menubar

    # Create menus
    foreach i {
       {filemenu     -label File -underline 0}
       {helpmenu     -label Help -underline 0}
    } {
       eval .menubar add cascade -menu .menubar.$i
       menu ".menubar.[lindex $i 0]" -tearoff 0
    }

    # File menu
    foreach i {
      {command -command exit -label Exit -underline 1}
    } {
      eval .menubar.filemenu add $i
    }

    # Help menu
    foreach i {
      {command -command helpAbout -label {About OMNeT++} -underline 0}
    } {
      eval .menubar.helpmenu add $i
    }

    #################################
    # Create main display area
    #################################

    frame .sel -bd 2 -relief groove
    frame .main -bd 2 -relief groove
    pack .sel -expand 0 -fill y  -side left -padx 2
    pack .main -expand 1 -fill both  -side right  -padx 2

    frame .main.up
    frame .main.lo
    frame .main.mid
    pack .main.up -expand 0 -fill x  -side top
    pack .main.lo -expand 0 -fill x  -side bottom
    pack .main.mid -expand 1 -fill both -side top

    label .main.up.banner -bg $colors(banner) -relief groove  -font $fonts(bold)
    pack .main.up.banner -fill x -side top

    text .main.mid.text -yscrollcommand ".main.mid.sb set" -width 74 -height 20  -wrap none
    scrollbar .main.mid.sb -command ".main.mid.text yview"
    pack .main.mid.sb -anchor center -expand 0 -fill y  -side right
    pack .main.mid.text -anchor center -expand 1 -fill both -side left

    button .main.lo.start -bg $colors(startbutton) -font $fonts(bold)
    pack .main.lo.start -expand 1 -fill x  -padx 10 -pady 6

    label .sel.banner -text "Sample simulations:" -bg $colors(banner) -relief groove -font $fonts(bold)
    pack .sel.banner -fill x -side top
    foreach i $samples {
        button .sel.$i -text $name($i) -command "showDemo $i"
        pack .sel.$i -fill x -side top -padx 5 -pady 2
    }
    showDemo [lindex $samples 0]
}

proc helpAbout {} {
    tk_messageBox -title {About OMNeT++ Demo} -type ok -icon info -message \
{\
OMNeT++ Discrete Event Simulation System
Sample Simulations

(c) Andras Varga, 1992-99
Technical University of Budapest
Dept. of Telecommunications
}

}

proc showDemo {sample} {
   global samples dir name colors use_single_executable tcl_platform

   foreach i $samples {
        .sel.$i config -relief raised -bg $colors(unselbutton)
   }
   .sel.$sample config -relief sunken -bg $colors(selbutton)

   if {$use_single_executable} {
      set cmd "demo -f $dir($sample)/omnetpp.ini"
   } else {
      if {$tcl_platform(platform) == "unix"} {
        set runprog "./$sample &"
      } else {
        set runprog "start $sample" 
      }
      set cmd "cd $dir($sample); $runprog"
   }

   .main.up.banner config -text "$name($sample) README"
   .main.lo.start config -text "Launch $name($sample)  (command: $cmd)"
   .main.lo.start config -command "runSample $sample"

   loadFile .main.mid.text "$dir($sample)/README"
}

proc loadFile {t filename} {
    if [catch {open $filename r} f] {
       messagebox {Error} "Error: $f" info ok
       return
    }
    set contents [read $f]
    close $f

    set curpos [$t index insert]
    $t delete 1.0 end
    $t insert end $contents
    catch {$t mark set insert $curpos}
    $t see insert
}

proc runSample {sample} {
   global samples dir name use_single_executable

   if {$use_single_executable} {
      if [catch {exec ./demo -f "$dir($sample)/omnetpp.ini" &}] {
         tk_messageBox -title {OMNeT++ Demo} -type ok -icon error \
                       -message {Error running compound 'demo' executable. Has it been built?}
      }
   } else {
      if [catch {cd $dir($sample); exec ./$sample &}] {
         tk_messageBox -title {OMNeT++ Demo} -type ok -icon error \
                       -message {Error running the simulation program.}
      }
   }
}

#===================================================================
#    GENERIC BINDINGS
#===================================================================

proc generic_bindings {} {
   bind Button <Return> {tkButtonInvoke %W}
}

#===================================================================
#    FONT BINDINGS
#===================================================================

proc font_bindings {} {
   global fonts tcl_platform colors

   set colors(banner)      #e0e0a0
   set colors(unselbutton) #c0c0c0
   set colors(selbutton)   #ffffff
   set colors(startbutton) #e0e0a0

   #
   # fonts() array elements:
   #  normal:  menus, labels etc
   #  bold:    buttons
   #  icon:    toolbar 'icons'
   #  big:     STOP button
   #  msgname: message name during animation
   #  fixed:   text windows and listboxes
   #

   if {$tcl_platform(platform) == "unix"} {
      set fonts(normal) -Adobe-Helvetica-Medium-R-Normal-*-*-120-*-*-*-*-*-*
      set fonts(bold)   -Adobe-Helvetica-Bold-R-Normal-*-*-120-*-*-*-*-*-*
      set fonts(icon)   -Adobe-Times-Bold-I-Normal-*-*-120-*-*-*-*-*-*
      set fonts(big)    -Adobe-Helvetica-Medium-R-Normal-*-*-180-*-*-*-*-*-*
      set fonts(msgname) -Adobe-Helvetica-Medium-R-Normal-*-*-120-*-*-*-*-*-*
      set fonts(fixed)  fixed
      set fonts(balloon) -Adobe-Helvetica-Medium-R-Normal-*-*-120-*-*-*-*-*-*
   } else {
      # Windows, Mac
      set fonts(normal) -Adobe-Helvetica-Medium-R-Normal-*-*-140-*-*-*-*-*-*
      set fonts(bold)   -Adobe-Helvetica-Bold-R-Normal-*-*-140-*-*-*-*-*-*
      set fonts(icon)   -Adobe-Helvetica-Bold-R-Normal-*-*-140-*-*-*-*-*-*
      set fonts(big)    -Adobe-Helvetica-Medium-R-Normal-*-*-180-*-*-*-*-*-*
      set fonts(msgname) -Adobe-Helvetica-Medium-R-Normal-*-*-140-*-*-*-*-*-*
      set fonts(fixed)  FixedSys
      set fonts(balloon) -Adobe-Helvetica-Medium-R-Normal-*-*-140-*-*-*-*-*-*
   }

   if {$tcl_platform(platform) == "unix"} {
       option add *Scrollbar.width  12
       option add *Menubutton.font  $fonts(normal)
       option add *Menu.font        $fonts(normal)
       option add *Label.font       $fonts(normal)
       option add *Entry.font       $fonts(normal)
       option add *Listbox.font     $fonts(fixed)
       option add *Text.font        $fonts(fixed)
       option add *Button.font      $fonts(bold)
   }
}

proc checkVersion {} {

   global tk_version tk_patchLevel

   catch {package require Tk}
   if {$tk_version<8.0} {
      wm deiconify .
      wm title . "Bad news..."
      frame .f
      pack .f -expand 1 -fill both -padx 2 -pady 2
      label .f.l1 -text "Your version of Tcl/Tk is too old!"
      label .f.l2 -text "Tcl/Tk 8.0 or later required."
      button .f.b -text "OK" -command {exit}
      pack .f.l1 .f.l2 -side top -padx 5
      pack .f.b -side top -pady 5
      focus .f.b
      wm protocol . WM_DELETE_WINDOW {exit}
      tkwait variable ok
   }
#
#   if {[string compare $tk_patchLevel "8.0p1"]<0} {
#      tk_messageBox -title {Warning} -type ok -icon warning \
#        -message {Old Tcl/Tk version. At least 8.0p1 is strongly recommended!}
#
#   }
}

checkVersion
generic_bindings
font_bindings


createWindow

