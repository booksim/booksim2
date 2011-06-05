$run_time = 100000;
$points = 20;


$config_file = $ARGV[0];
$max_rate = $ARGV[1];
$modifiers = $ARGV[2];
if($max_rate == 0){
    print "need the max rate\n";
    die;
}
if($config_file eq ""){
    print "no config file specified\n";
    die;
}

for($i = 0; $i<$points; $i++){
    
    if($i==0){
	$rate = 0.001;
    } else {
	$rate = sqrt($i)/sqrt($points)*$max_rate;
    }

    $run_string = "./booksim $config_file injection_rate=$rate sample_period=$run_time max_samples=6 $modifiers |";
    print "running: ".$run_string."\n";
    open(booksim_output, $run_string);
    
    $unstable = 0;
    $result = 0;
    while($line = <booksim_output>){
	if($line =~m/^Overall average latency *= *([0-9]+\.?[0-9]*)/){
	    $result = $1 ;
	}
	if($line =~m/^Simulation unstable/){
	    $unstable = 1;
	}
    }

    $print_rate[$i] = $rate;
    if($unstable==1){
	$print_lat[$i] =-1; 
	last;
    } else {
	$print_lat[$i] =$result;
    }
    
   close(booksim_output);
}
print "==============================\n";
for($i = 0; $i<$points; $i++){
    print $print_rate[$i]."\t".$print_lat[$i]."\n";
}
print "==============================\n";
