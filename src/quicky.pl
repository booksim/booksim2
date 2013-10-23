$config = $ARGV[0];
for($i=0;$i<20; $i+=1){
    $c=($i+1)/20*0.6;
    system("./booksim  $config configs/ftree configs/combined_addon sample_period=1000000 \"injection_rate={$c,0.0}\"");
}
