$inputfilename = $ARGV[0];
$outputfilename = $ARGV[1];
$packetsize = $ARGV[2];

#output datafile
open(outfile, ">",$outputfilename) or die "can't open output file $outputfilename";
#loop through injectionrates
for($i = 1/$packetsize; $i<80/$packetsize; $i+=3/$packetsize){
    $injectionrate = $i/100;
    $inputfile = "$inputfilename"."$injectionrate";
    open(infile , "<",$inputfile) or die "can't open input file $inputfile";
    
    print outfile "$injectionrate\t";
    #integer and decimal portion of the data 
    $integer = 0;
    $decimal = 0;
    $zero = "";
    while($line = <infile>){
	$integer = 0;
	$decimal = 0;

	if($line=~m/Average latency is getting huge */){
	    close(infile);
	    close(outfile);
	    $dierate= $packetsize * $injectionrate;
	    print "GAME OVER at $dierate\n" and die;
	}
	   
	$line =~m/Overall average latency *= *([0-9]+)\.(0*)([1-9][0-9]*)/;
	#zero is used for zeros after the decimal e.g. 5.05
	$integer = $1;
	$zero = "1".$2;
	$decimal = $3;
	$result = 0 ;
	if($integer!=0 || $decimal!=0){
	    for($j=0;$j<20;$j++){
		if($decimal<10**$j){

		    if($zero>1){
			$result =$integer + $decimal/10**$j/$zero; 
		    } else {
			$result =$integer + $decimal/10**$j;
		    }
		    last;
		}
	    }
	    print outfile "$result";
	    last;
	}
    }
    print outfile "\n";
    close(infile);
}
close(outfile);
