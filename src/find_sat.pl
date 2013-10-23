$run_time = 1000000;
$iterations = 10;


$config_file = $ARGV[0];
$modifiers = $ARGV[1];

if($config_file eq ""){
    print "no config file specified\n";
    die;
}

#zero load latency
$run_string = "./booksim $config_file injection_rate=0.001 sample_period=$run_time max_samples=6 $modifiers |";
print "running: ".$run_string."\n";
open(booksim_output, $run_string);

$zero_load = 0;
while($line = <booksim_output>){
    if($line =~m/^Overall average latency *= *([0-9]+\.?[0-9]*)/){
	$zero_load = $1 ;
    }
}
close(booksim_output);

if($zero_load == 0){
    print "can not determine zero load latency";
    die;
}
print "==============================\n";
print "zero load latency: $zero_load \n";
print "==============================\n\n";

$last_good_rate = 0;
$last_bad_rate = 1.0;
$rate = 0;
$good_result = 0;
for($i = 0; $i<$iterations; $i++){

    $rate = ($last_good_rate+$last_bad_rate)/2;
    $run_string = "./booksim $config_file injection_rate=$rate sample_period=$run_time max_samples=6 $modifiers |";
    print "running: ".$run_string."\n";
    open(booksim_output, $run_string);

    print "--- Trial $i ---\n";
    printf"bad $last_bad_rate \n";
    printf"current $rate \n";
    printf"good $last_good_rate \n";

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


    if($unstable ==1){
	$last_bad_rate = $rate;
	print "Result UNSTABLE\n\n";
    } else {
	$last_good_rate = $rate;
	$good_result = $result;
	print "Result $result\n\n"
    }


  

    close(booksim_output);
}


print "==============================\n";
print "Final Result: $last_good_rate $good_result \n";
print "==============================\n";


