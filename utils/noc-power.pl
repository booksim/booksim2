#!/usr/bin/perl

# $Id$

# Copyright (c) 2007-2012, Trustees of The Leland Stanford Junior University
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
# Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

$inputFileName = $ARGV[0] ;
$outputFileName = $ARGV[1];
$WMAX = $ARGV[2];
$WMIN = $ARGV[3];

# ----------------------------------------------------------------------
#
#   Network Parameters
#
# ----------------------------------------------------------------------
if($WMAX == 0){
    $WMAX = 256;
}

$WireLength{"mesh"}          = 1.5 ;
$WireLength{"cmesh"}          = 1.5 ;
$WireLength{"flatfly"}           = 1.5 ;
$WireLength{"KN"}             = 1.5 ;
$WireLength{"cmo"}             = 1.5 ;

# ----------------------------------------------------------------------
#
#   Technology Parameters
#
# ----------------------------------------------------------------------

#
# Metal Parameters
#
$Cw_cpl = 0.0651E-15 * 1E3 ; # Wire left/right coupling capacitance [ F/mm ]
$Cw_gnd = 0.0354E-15 * 1E3 ; # Wire up/down groudn capacitance      [ F/mm ]
$Cw = 2.0 * $Cw_cpl + 2.0 * $Cw_gnd ;
$Rw = 2.0 * 1E3 ;

$MetalPitch = 200E-9 * 1E3 ;  # metal pitch [mm]

#
# Device Parameters
#

$LAMBDA = 0.065 / 2.0  ;       # [um/LAMBDA]
$Cd     = 0.70E-15 ;           # [F/um] (for Delay)
$Cg     = 1.20E-15 ;           # [F/um] (for Delay)
$Cgdl   = 0.29E-15 ;           # [F/um] (for Delay)
			       
$Cd_pwr = 0.70E-15 ;           # [F/um] (for Power)
$Cg_pwr = 1.26E-15 ;           # [F/um] (for Power)
			       
$IoffN  = 10.0E-9 ;            # [A/um]
$IoffP  =  5.0E-9 ;            # [A/um]

$IoffSRAM = 4 * 5.0E-9 ;  # Leakage from bitlines, two-port cell  [A] 

$R        = 1.2E3  ;                         # [Ohm] ( D1=1um Inverter)
$Ci_delay = (1.0 + 2.0) * ( $Cg + $Cgdl );   # [F]   ( D1=1um Inverter - for Power )
$Co_delay = (1.0 + 2.0) * $Cd ;              # [F]   ( D1=1um Inverter - for Power )


$Ci = (1.0 + 2.0) * $Cg_pwr ;
$Co = (1.0 + 2.0) * $Cd_pwr ;

$Vdd    = 1.0;
$FO4    = $R * ( 3.0 * $Cd + 12 * $Cg + 12 * $Cgdl);		     
$tCLK   = 20 * $FO4;
$fCLK   = 1.0 / $tCLK;                

#
# Standard Cell Parameters
#
$Height{"INVD2"} = 8  ;
$Width{"INVD2"}  = 3  ;

$Height{"DFQD1"} = 8  ;
$Width{"DFQD1"}  = 16 ;

$Height{"ND2D1"} = 8 ;
$Width{"ND2D1"}  = 3 ;

$Height{"SRAM"} = 8  ;
$Width{"SRAM"}  = 6 ;


# ----------------------------------------------------------------------
#
#  Display derived Parameters
#
# ----------------------------------------------------------------------
print "Technology Parameters:\n" ;
 $t = $FO4 * 1E12 ;
print "  + FO-4 Delay: $t [ps] \n" ;
 $t = $tCLK * 1E12 ;
print "  + tCLK:       $t [ps] \n" ;
 $t = $fCLK / 1E9 ;
print "  + fCLK:       $t [GHz]\n" ;

print "Cw_cpl = $Cw_cpl, Cw_gnd = $Cw_gnd, Rw = $Rw \n" ;
print "Ci (Delay): $Ci_delay\n" ;
print "Co (Delay): $Co_delay\n" ;
print "Ci (Power): $Ci\n" ;
print "Co (Power): $Co\n" ;
  $t =  0.5 * ($IoffN + 2 * $IoffP) ;
print "Ioff : $t \n" ;

# ----------------------------------------------------------------------
#
#  Channel Power
#
# ----------------------------------------------------------------------
$ChannelPitch = 2.0 * $MetalPitch ;

sub wireOptimize {
    my $L = @_[0] ;

    if ( $memo{"$L"} eq "Yes" )  {
	$K = $memoK{"$L"} ;
	$M = $memoM{"$L"} ;
	$N = $memoN{"$L"} ;
	return ;
    } 
    my $W = 64 ;
    my $bestMetric = 100000000 ;
    for (my $K = 1.0 ; $K < 10 ; $K+=0.1 ) {
	for (my $N = 1.0 ; $N < 40 ; $N += 1.0 ) {
	    for (my $M = 1.0 ; $M < 40.0 ; $M +=1.0 ) {

		my $l = 1.0 * $L/( $N * $M) ;

		$k0 = $R * ($Co_delay + $Ci_delay) ;
		$k1 = $R/$K * $Cw + $K * $Rw * $Ci_delay ;
		$k2 = 0.5 * $Rw * $Cw ;

		$Tw = $k0 + ($k1 * $l) + $k2 * ($l * $l) ;

		$alpha = 0.2 ;
		$power = $alpha * $W * powerRepeatedWire( $L, $K, $M, $N) + powerWireDFF( $M, $W, $alpha ) ;
		$metric = $M * $M * $M * $M * $power ;

		#print "[$K,$N,$M] -> Tw = $Tw : tCLK = $tCLK ( $l $k0 $k1 $k2 ) \n" ;
		if ( ($N*$Tw) < (0.8 * $tCLK) ) {
		    if ( $metric < $bestMetric ) {
			$bestMetric = $metric ;
			$bestK = $K ;
			$bestM = $M ;
			$bestN = $N ;
		    }
		}
	    }
	}
    }

    print "L = $L K = $bestK M = $bestM N = $bestN\n";

    $memo{"$L"}  = "Yes" ;
    $memoK{"$L"} = $bestK ;
    $memoM{"$L"} = $bestM ;
    $memoN{"$L"} = $bestN ;
    $K = $bestK ;
    $N = $bestN ;
    $M = $bestM ;
}

sub powerRepeatedWire {
    my $L     = @_[0] ;
    my $K     = @_[1] ;
    my $M     = @_[2] ;
    my $N     = @_[3] ;

    # Repeaters - Active Power
    my $segments = 1.0 * $M * $N ;
    my $Ca = $K * ($Ci + $Co) + $Cw * ($L/$segments) ;
    my $Pa = 0.5 * $Ca * $Vdd * $Vdd * $fCLK;
    return $Pa * $M * $N  ;
}

sub powerRepeatedWireLeak {
    my $K     = @_[0] ;
    my $M     = @_[1] ;
    my $N     = @_[2] ;
    
    $Pl = $K * 0.5 * ( $IoffN + 2.0 * $IoffP ) * $Vdd  ;
    return $Pl * $M * $N ;
}

sub powerWireClk {
    my $M     = @_[0] ;  # number of retiming elements in the channel
    my $W     = @_[1] ;  # channel width in bits, for clock network

    # number of clock wires running down one repeater bank
    $columns = $Height{"DFQD1"} * $MetalPitch /  $ChannelPitch ;

    # length of clock wire
    $clockLength = $W * $ChannelPitch ;
    $Cclk = (1 + 5.0/16.0 * (1+$Co_delay/$Ci_delay)) * ($clockLength * $Cw * $columns +$W * $Ci_delay);

    return $M * $Cclk * ($Vdd * $Vdd) * $fCLK ;
}

sub powerWireDFF {
    my $M     = @_[0] ;  # number of retiming elements in the channel
    my $W     = @_[1] ;  # channel width in bits, for clock network
    my $alpha = @_[2] ;  # input node activity factor (toggle rate) 

    my $Cdin = 2 * 0.8 * ($Ci + $Co) + 2 * ( 2.0/3.0 * 0.8 * $Co )  ;
    my $Cclk = 2 * 0.8 * ($Ci + $Co) + 2 * ( 2.0/3.0 * 0.8 * $Cg_pwr) ;
    my $Cint = ($alpha * 0.5) * $Cdin + $alpha * $Cclk ;

    return $Cint * $M * $W * ($Vdd*$Vdd) * $fCLK ;
}


# ----------------------------------------------------------------------
#
#  Memory Power
#
# ----------------------------------------------------------------------
sub powerWordLine {
    my $memoryWidth =  @_[0] ;
    my $memoryDepth =  @_[1] ;

    # wordline capacitance
    my $Ccell = 2 * ( 4.0 * $LAMBDA ) * $Cg_pwr +  6 * $MetalPitch * $Cw ;     
    my $Cwl = $memoryWidth * $Ccell ; 

    # wordline circuits
    my $Warray = 8 * $MetalPitch + $memoryDepth ;
    my $x = 1.0 + (5.0/16.0) * (1 + $Co/$Ci)  ;
    $Cpredecode = $x * ($Cw * $Warray  * $Ci) ;
    $Cdecode    = $x * $Cwl ;

    # bitline circuits
    my $Harray =  6 * $memoryWidth * $MetalPitch ;
    my $y = (1 + 0.25) * (1 + $Co/$Ci) ;
    $Cprecharge = $y * ( $Cw * $Harray + 3 * $w * $Ci ) ;
    $Cwren      = $y * ( $Cw * $Harray + 2 * $w * $Ci ) ;

    $Cbd = $Cprecharge + $Cwren ;
    $Cwd = 2 * $Cpredecode + $Cdecode ;

    return ( $Cbd + $Cwd ) * $Vdd * $Vdd * $fCLK ;
}

# Read power for cells on one bitline
sub powerMemoryBitRead {
    my $memoryDepth =  @_[0] ;
    
    # bitline capacitance
    my $Ccell  = 4.0 * $LAMBDA * $Cd_pwr + 8 * $MetalPitch * $Cw ; 
    my $Cbl    = $memoryDepth * $Ccell ;
    my $Vswing = $Vdd  ;

    return ( $Cbl ) * ( $Vdd * $Vswing ) * $fCLK ;

}

# Write power for cells on one bitline
sub powerMemoryBitWrite { 
    my $memoryDepth =  @_[0] ;
    
    # bitline capacitance
    my $Ccell  = 4.0 * $LAMBDA * $Cd_pwr + 8 * $MetalPitch * $Cw ; 
    my $Cbl    = $memoryDepth * $Ccell ;

    # internal capacitance
    my $Ccc    = 2 * ($Co + $Ci) ;

    return (0.5 * $Ccc * ($Vdd*$Vdd)) + ( $Cbl ) * ( $Vdd * $Vdd ) * $fCLK ;
}

# Leakage for elements on one bitline
sub powerMemoryBitLeak { 
    my $memoryDepth =  @_[0] ;
    return $memoryDepth * $IoffSRAM * $Vdd ;
}

# ----------------------------------------------------------------------
#
#  Switch Power
#
# ----------------------------------------------------------------------
$CrossbarPitch = 2.0 * $MetalPitch ;

sub powerCrossbar  {
    my $width   = @_[0] ;
    my $inputs  = @_[1] ;
    my $outputs = @_[2] ;
    my $from    = @_[3] ;
    my $to      = @_[4] ;

    # datapath traversal power
    my $Wxbar = $width * $outputs * $CrossbarPitch ;
    my $Hxbar = $width * $inputs  * $CrossbarPitch ;

    # wires
    my $CwIn  = $Wxbar * $Cw ;
    my $CwOut = $Hxbar * $Cw ;

    # cross-points
    my $Cxi = (1.0/16.0) * $CwOut ;
    my $Cxo = 4.0 * $Cxi * ($Co_delay/$Ci_delay) ;

    # drivers
    my $Cti = (1.0/16.0) * $CwIn ;
    my $Cto = 4.0 * $Cti * ($Co_delay/$Ci_delay) ;

    my $CinputDriver = 5.0/16.0 * (1 + $Co_delay/$Ci_delay) * (0.5 * $Cw * $Wxbar + $Cti) ;

    # total switched capacitance
    my $Cin  = $CinputDriver + $CwIn + $Cti + ($outputs * $Cxi) ;
    if ( $to < $outputs/2 ) {
    	$Cin -= ( 0.5 * $CwIn + $outputs/2 * $Cxi) ;
    }
    my $Cout = $CwOut + $Cto + ($inputs * $Cxo) ;
    if ( $from < $inputs/2) {
    	$Cout -= ( 0.5 * $CwOut + ($inputs/2 * $Cxo)) ;
    }
    return 0.5 * ($Cin + $Cout) * ($Vdd * $Vdd * $fCLK) ;
}

sub powerCrossbarCtrl {
    my $width   = @_[0] ;
    my $inputs  = @_[1] ;
    my $outputs = @_[2] ;
 
    # datapath traversal power
    my $Wxbar = $width * $outputs * $CrossbarPitch ;
    my $Hxbar = $width * $inputs  * $CrossbarPitch ;

    # wires
    my $CwIn  = $Wxbar * $Cw ;
    my $CwOut = $Hxbar * $Cw ;

    # cross-points
    my $Cxi = (5.0/16.0) * $CwOut ;
    my $Cxo = $Cxi * ($Co_delay/$Ci_delay) ;

    # drivers
    my $Cti  = (5.0/16.0) * $CwIn ;
    my $Cto  = $Cti * ($Co_delay/$Ci_delay) ;

    # need some estimate of how many control wires are required
    my $Cctrl  = $width * $Cti + ($Wxbar + $Hxbar) * $Cw  ; 
    my $Cdrive = (5.0/16.0) * (1 + $Co_delay/$Ci_delay) * $Cctrl ;

    return ($Cdrive + $Cctrl) * ($Vdd*$Vdd) * $fCLK ;
}

sub powerCrossbarLeak {
    my $width   = @_[0] ;
    my $inputs  = @_[1] ;
    my $outputs = @_[2] ;
 
    # datapath traversal power
    my $Wxbar = $width * $outputs * $CrossbarPitch ;
    my $Hxbar = $width * $inputs  * $CrossbarPitch ;

    # wires
    my $CwIn  = $Wxbar * $Cw ;
    my $CwOut = $Hxbar * $Cw ;
    # cross-points
    my $Cxi = (1.0/16.0) * $CwOut ;
    # drivers
    my $Cti  = (1.0/16.0) * $CwIn ;

    return 0.5 * ($IoffN + 2 * $IoffP)*$width*($inputs*$outputs*$Cxi+$inputs*$Cti+$outputs*$Cti)/$Ci;
}

# ----------------------------------------------------------------------
#
#   Output Module
#
# ----------------------------------------------------------------------

sub powerOutputCtrl {
    my $width   = @_[0] ;

    my $Woutmod = $w * $ChannelPitch ;
    my $Cen     = $Ci ;

    my $Cenable = (1 + 5.0/16.0)*(1.0+$Co/$Ci)*($Woutmod* $Cw + $width* $Cen) ;

    return $Cenable * ($Vdd*$Vdd) * $fCLK ;
    
}

# ----------------------------------------------------------------------
#
#  Area Models
#
# ----------------------------------------------------------------------

sub areaChannel {
    my $K     = @_[0] ;
    my $N     = @_[1] ;
    my $M     = @_[2] ;

    my $Adff = $M * $Width{"DFQD1"} * $Height{"DFQD1"} ;
    my $Ainv = $M * $N * ( $Width{"INVD2"} + 3 * $K) * $Height{"INVD2"} ;

    return $w * ($Adff + $Ainv) * $MetalPitch * $MetalPitch ;
}

sub areaCrossbar {
    my $Inputs  = @_[0] ;
    my $Outputs = @_[1] ;
    return ($Inputs * $w * $CrossbarPitch) * ($Outputs * $w * $CrossbarPitch) ;
}

sub areaInputModule {
    my $Words = @_[0] ;
    my $Asram =  ( $w * $Height{"SRAM"} ) * ($Words * $Width{"SRAM"}) ;
    return $Asram * ($MetalPitch * $MetalPitch) ;
}

sub areaOutputModule {
    my $Outputs = @_[0] ;
    my $Adff = $Outputs * $Width{"DFQD1"} * $Height{"DFQD1"} ;
    return $w * $Adff * $MetalPitch * $MetalPitch ;
}

# ----------------------------------------------------------------------
#
#  Main script for processing the input file
#
# ----------------------------------------------------------------------
@flitWidth = ( 0,0,0,0,0 ) ;

#wireOptimize(1.5) ;
#print "L=1.5 K=$K, N=$N, M=$M\n" ;
#wireOptimize(3) ;
#print "L=3 K=$K, N=$N, M=$M\n" ;

open(outFile, ">", $outputFileName) or die "Can't open write file";



print outFile "w\t";
print outFile "channelWirePower\tchannelClkPower\tchannelDFFPower\tchannelLeakPower\t";
print outFile "inputReadPower\tinputWritePower\tinputLeakagePower\t";
print outFile "switchPower\tswitchPowerCtrl\tswitchPowerLeak\t";
print outFile "outputPower\toutputPowerClk\toutputCtrlPower\t";
print outFile "channelArea\tswitchArea\tinputArea\toutputArea\n";

for ($w = $WMAX; $w>$WMIN; $w-=2) {
$flitWidth[0] = $w ;
$flitWidth[1] = $w ;
$flitWidth[2] = $w ;  
$flitWidth[3] = $w ;
$flitWidth[4] = $w ;


$totalTime = 0;
$channelWirePower=0;
$channelClkPower=0;
$channelDFFPower=0;
$channelLeakPower=0;
$inputReadPower=0;
$inputWritePower=0;
$inputLeakagePower=0;
$switchPower=0;
$switchPowerCtrl=0;
$switchPowerLeak=0;
$outputPower=0;
$outputPowerClk=0;
$outputCtrlPower=0;
$channelArea=0;
$switchArea=0;
$inputArea=0;
$outputArea=0;
$maxInputPort = 0;
$maxOutputPort = 0;


open( inFile, "<", $inputFileName ) or die "Failed to open $inputFileName" ;
while ( $line = <inFile> ) {

    #
    #  Gather network and performance parameters for power estimation
    #
    
    #type of network
    if ( $line =~ m/topology *= *([a-zA-Z0-9]+)/ ) {
	$topology = $1 ;
    }
    
    #useless
    if ( $line =~ m/traffic *= *([a-z0-9]+)/) {
	$traffic = $1 ;
    }
    if ( $traffic eq "tcc" && $line =~ m/trace_file *= *([a-z]+)/) {
	$traffic = $1 ;
    }
    #total factor used to determine activity factor
    if ( $line =~ m/Time taken is ([0-9]+) cycles/) {
	$totalTime += $1 ;
    }
    #not necessary, since wires are optimized already
    if ( $line =~ m/k *= *([0-9]+) *\;/) {
	$k= $1 ;
    }

    if ( $line =~ m/n *= *([0-9]+) *\;/) {
	$n= $1 ;
    }
    
    #number of short flit vc channels
    if ( $line =~ m/read_request_begin_vc *= *([0-9]+)/ ) {
	$shortFirstVC = $1 ;
    }
    if ( $line =~ m/write_reply_end_vc *= *([0-9]+)/ ) {
	$shortLastVC  = $1 ;
    }
    #total number of vcs
    if ( $line =~ m/num_vcs *= *([0-9]+)/ ) {
	$numVC    = $1 ;
    }
    #size of the buffer per vc
    if ( $line =~ m/vc_buf_size *= *([0-9]+)/ ) {
	$depthVC  = $1 ;
    }
    
    #set a default width for all


    #
    #  Channel Power
    #  single channel (w bits)
    #
    if ( $line =~ m/FlitChannel/ ) {

	# latency wire length modifier
	$line =~ m/Latency: ([0-9]+)/ ;
	my $channelLength = $1 * $WireLength{$topology} ;
	
	wireOptimize( $channelLength ) ;
	
	$channelArea += areaChannel( $K, $N, $M) ;
       
	# activity factor
	$line =~ m/([0-9]+),([0-9]+),([0-9]+),([0-9]+),([0-9]+)/ ;
	@flits = ( $1 ,$2, $3, $4 ,$5) ;
	for ( my $i = 0 ; $i < 5 ; $i += 1 ) {
	    $a[$i] = $flits[$i] / ( 1.0 * $totalTime ) ;
	}
	
	# channel length depends on connection
	$line =~ m/(router[_0-9]+) / ; 
	my $src = $1 ;

	$line =~ m/ (router[_0-9]+)/ ;
	my $dst = $1 ;

	# power calculation
	#only 1 bit, then modify by activity factor and the width
	$bitPower = powerRepeatedWire( $channelLength, $K, $M, $N ) ;
	$channelWirePower += $bitPower * $a[0] * $flitWidth[0] ;
	$channelWirePower += $bitPower * $a[1] * $flitWidth[1] ;
	$channelWirePower += $bitPower * $a[2] * $flitWidth[2] ; 
	$channelWirePower += $bitPower * $a[3] * $flitWidth[3] ;
	$channelWirePower += $bitPower * $a[4] * $flitWidth[4] ;

	$channelClkPower += powerWireClk( $M, $flitWidth[1] ) ;

	$channelDFFPower += powerWireDFF( $M, $flitWidth[0], $a[0] ) ;
	$channelDFFPower += powerWireDFF( $M, $flitWidth[1], $a[1] ) ;
	$channelDFFPower += powerWireDFF( $M, $flitWidth[2], $a[2] ) ;
	$channelDFFPower += powerWireDFF( $M, $flitWidth[3], $a[3] ) ;
	$channelDFFPower += powerWireDFF( $M, $flitWidth[4], $a[4] ) ;

	$channelLeakPower += powerRepeatedWireLeak( $K, $M, $N) * $flitWidth[1] ;


    }
    
    #
    #  Input Buffer Power
    #  single input buffer, size w
    #
    if ( $line =~ m/bufferMonitor/ ) {
	#number of long and shor tchannels
	my $numShortVC  = $shortLastVC - $shortFirstVC +1 ;
	my $numLongVC   =  $numVC - $numShortVC ;
	#total amount of channels*buffer size
	my $depth       = $numLongVC * $depthVC + $numShortVC * $depthVC ;
	
	#leakage power total amount of bufferes* bits per buffer
	my $Pleak = powerMemoryBitLeak( $numShortVC*$depthVC ) * $flitWidth[0] ;
	$Pleak += powerMemoryBitLeak( $numLongVC * $depthVC) * $flitWidth[1] ;


	$inputArea += areaInputModule( $depth ) ;	
	$line = <inFile> ;

	#read and write activity factor
	while ( $line =~ m/\[ [0-9]+ \]/ ) {
	$inputLeakagePower += $Pleak ;
	    for ( $type = 0 ; $type < 5 ; $type += 1 ) {
		$line =~ m/Type=$type:\(R\#([0-9]+),W\#([0-9]+)\)/ ;
		my $ar = ( 1.0 * $1 ) / $totalTime ;
		my $aw = ( 1.0 * $2 ) / $totalTime ;
		if($ar > 1 || $aw>1){
		    print "Dear god, your activity factor for input module is over 9000!\n";
		    $ar = 0;
		    $aw = 0;
		    #OMGOMGOMGOMGOMGOMGOMGOMGOMGGOGMOGMO
		}
		my $Pwl = powerWordLine( $flitWidth[$type], $depth) ;
		#number of bufferes * width
		my $Prd = powerMemoryBitRead( $depth ) * $flitWidth[$type] ;
		my $Pwr = powerMemoryBitWrite( $depth ) * $flitWidth[$type] ;

		$inputReadPower    += $ar * ( $Pwl + $Prd ) ;
		$inputWritePower   += $aw * ( $Pwl + $Pwr ) ;


	    }
	    $line = <inFile> ;
	}
    }

    #
    #  Switch Power
    #  Single switch, well there is only 1 switch
    #
    if ( $line =~ m/switchMonitor/ ) {
	$line = <inFile> ;
	#the switch specification is organized by input -> output node and includes packet type and activity factor
	while ( $line =~ m/\[([0-9]+) -> ([0-9]+)\]/) {
	    $inputPort  = $1 ;
	    $outputPort = $2 ;
	    #determines the size of the swtich
	    if ( $inputPort+1  > $maxInputPort )  { $maxInputPort  = $inputPort+1; } 
	    if ( $outputPort+1 > $maxOutputPort ) { $maxOutputPort = $outputPort+1; } 
	    #extracts out the activty factor for each of the type of packets 
	    for ( $type = 0 ; $type < 5 ; $type += 1 ) {
		$line =~ m/$type:([0-9]+)/ ;
		$crossbar[$inputPort]->[$outputPort]->[$type] = $1 ;
	    }
	    $line = <inFile> ;
	}
	#area is only dependent on the number of input and outputs
	$switchArea += areaCrossbar( $maxInputPort, $maxOutputPort ) ;
	$outputArea += areaOutputModule( $maxOutputPort ) ;
	$switchPowerLeak += powerCrossbarLeak($flitWidth[1], $maxInputPort,$maxOutputPort );
	#activty factor for each input->output configuration and packet type
	for ( $out = 0 ; $out < $maxOutputPort ; $out +=1 ) {
	    @activity = (0,0,0,0) ;	    

	    for ( $in = 0 ; $in < $maxInputPort ; $in +=1 ) {

		for ( $type = 0 ; $type < 5 ; $type += 1 ) {

		    $a  = $crossbar[$in]->[$out]->[$type] ;
		    $a  = $a / $totalTime ;
		    if ($a > 1.0) {
			print " Error! Switch activity factor is $a > 1.0\n" ;
			#OMGOMGOMGOMGOMGOMGOMG
			$a = 0;
		    }

		    $Px = powerCrossbar( $flitWidth[1], 
					 $maxInputPort, 
					 $maxOutputPort,
					 $in, $out ) ;
		    
		    $switchPower += $a * $flitWidth[$type] * $Px ;
		    
		    $switchPowerCtrl += $a * powerCrossbarCtrl( $flitWidth[1], 
								$maxInputPort, 
								$maxOutputPort ) ;
		    $activity[$type] += $a ;
		}
	    }

	    $outputPowerClk += powerWireClk( 1, $w ) ;
	    for ( $type = 0 ; $type < 5 ; $type+=1 ) {
		$outputPower     += $activity[$type] * powerWireDFF( 1, $flitWidth[$type], 1.0 ) ;
		$outputCtrlPower += $activity[$type] * powerOutputCtrl( $flitWidth[$type] ) ;
	    }
	}
    }   
}
close(inFile);
print outFile "$w\t";
print outFile "$channelWirePower\t$channelClkPower\t$channelDFFPower\t$channelLeakPower\t";
print outFile "$inputReadPower\t$inputWritePower\t$inputLeakagePower\t";
print outFile "$switchPower\t$switchPowerCtrl\t$switchPowerLeak\t";
print outFile "$outputPower\t$outputPowerClk\t$outputCtrlPower\t";
print outFile "$channelArea\t$switchArea\t$inputArea\t$outputArea\n";


}

close(outFile);
print "-----------------------------------------\n" ;
print "- OCN Power Summary\n" ;
print "- Traffic:                $traffic \n" ; 
print "- Completion Time:        $totalTime \n" ;
print "- Flit Widths:            @flitWidth\n" ;
print "- Channel Wire Power:     $channelWirePower \n" ;
print "- Channel Clock Power:    $channelClkPower \n" ;
print "- Channel Retiming Power: $channelDFFPower \n" ;
print "- Channel Leakage Power:  $channelLeakPower \n" ;
       - 
print "- Input Read Power:       $inputReadPower \n" ;
print "- Input Write Power:      $inputWritePower \n" ;
print "- Input Leakage Power:    $inputLeakagePower \n" ;
       - 
print "- Switch Power:           $switchPower \n" ;
print "- Switch Control Power:   $switchPowerCtrl \n" ;
print "- Switch Leakage Power:   $switchPowerLeak \n" ;
       - 
print "- Output DFF Power:       $outputPower \n" ;
print "- Output Clk Power:       $outputPowerClk \n" ;
print "- Output Control Power:   $outputCtrlPower \n" ;
print "-----------------------------------------\n" ;
print "\n" ;
print "-----------------------------------------\n" ;
print "- OCN Area Summary\n" ;
print "- Channel Area: $channelArea\n" ;
print "- Switch  Area: $switchArea\n" ;
print "- Input  Area:  $inputArea\n" ;
print "- Output  Area:  $outputArea\n" ;
print "-----------------------------------------\n" ;
