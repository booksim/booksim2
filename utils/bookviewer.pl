#!/usr/local/bin/perl
use Tk;

#udpated to display arbitrary collions
#updated packet drawing individual buffer tracking


$filename = $ARGV[0];
$delaytime = $ARGV[1];
$debug = $ARGV[2];

#stores the coordinate of the router box
my $routerxone;
my $routeryone;
my $routerid;
my $routerhasinjection;
my $routerhasejection;
my $routeroutputbuffer;
my $routeroutputbufferid;
my $routerinputbuffer;
my $routerinputbufferid;

#stores the assignment of the nodes
my $node2router;
my $node2routerx;
my $node2routery;
my $nodeid;
my $nodelinkid;
my $nodehasinjection;
my $nodehasejection;

#store the coordinate of the links
my $linksrc;
my $linkdest;
my $linkleng;
my $output2link;
my $input2link;
my $inputcount;
my $outputcount;
my $linkid;
my $linkqueue;
my $linkqueueid;
my $linksrcx;
my $linksrcy;
my $linkdestx;
my $linkdesty;

#store the number of bits on the links
my $linkload;
my $linkinjection;
my $linkunload;

#canvas size
my $width = 900;
my $height = 900;

my $boxwidth = 3;
my $boxheight = 3;

my $totalnodes;
my $totalrouters;
my $totallinks=0;
my $lengmax = 0;
my $flitstotalejected=0;
my $flitstotalinjected=0;
my $flitsejected20=0;
my $flitsinjected20=0;

my $TIME = 0;

my $mw = new MainWindow;
my $timelabel = $mw -> Label(-text=>"TIME $TIME")->grid(-row=>1,
							-column=>1);
my $cns = $mw -> Canvas(-relief=>"sunken", 
			-height=>$height,
			-width=>$width,
			-background=>"grey85",);
$cns->grid(-row=>2,-column=>1);
#create a frame for buttons and shit
my $frm_name = $mw -> Frame() -> grid(-row=>2,-column=>2);

my $frm_name_row = 0;

$frm_name -> Label(-text=>"Flits Injected")->grid(-row=>$frm_name_row++,-column=>1);
$flitstotalinjectedid = $frm_name -> Label(-text=>"Total: $flitstotalinjected")->grid(-row=>$frm_name_row++,-column=>1);
$flitsinjected20id = $frm_name -> Label(-text=>"Last 20c: $flitsinjected20")->grid(-row=>$frm_name_row++,-column=>1);
$frm_name -> Label(-text=>"Flits Ejected")->grid(-row=>$frm_name_row++,-column=>1);
$flitstotalejectedid = $frm_name -> Label(-text=>"Total: $flitstotalejected")->grid(-row=>$frm_name_row++,-column=>1);
$flitsejected20id = $frm_name -> Label(-text=>"Last 20c: $flitsejected20")->grid(-row=>$frm_name_row++,-column=>1);

#creat a greyscale for routers
$frm_name -> Label(-text=>"Router Max Occupancy")->grid(-row=>$frm_name_row++,-column=>1);
my $greyscalei = 0;
for($greyscalei = 0; $greyscalei<10;$greyscalei++){
    my $greyscaletemp = $greyscalei*10;
    $frm_name-> Label( -text=>"$greyscaletemp"."% Free",
		       -height=>1,
		       -width=>15,
		       -foreground=>"blue",
		       -background=>"grey$greyscaletemp",)->grid(-row=>$frm_name_row++,-column=>1);
}

#router node legend
$frm_name -> Label(-text=>"Router/Node Coloring")->grid(-row=>$frm_name_row++,-column=>1);
$frm_name-> Label( -text=>"Ejected",
		   -height=>1,
		   -width=>15,
		   -foreground=>"blue",
		   -background=>"lime green",)->grid(-row=>$frm_name_row++,-column=>1);
$frm_name-> Label( -text=>"Injected",
		   -height=>1,
		   -width=>15,
		   -foreground=>"blue",
		   -background=>"indian red",)->grid(-row=>$frm_name_row++,-column=>1);



#channel lengend
$frm_name -> Label(-text=>"Channel Time Average")->grid(-row=>$frm_name_row++,-column=>1);
$frm_name-> Label( -text=>"<25% Load",
		   -height=>1,
		   -width=>15,
		   -foreground=>"blue",
		   -background=>"green",)->grid(-row=>$frm_name_row++,-column=>1);
$frm_name-> Label( -text=>"25-50% load",
		   -height=>1,
		   -width=>15,
		   -foreground=>"blue",
		   -background=>"yellow",)->grid(-row=>$frm_name_row++,-column=>1);
$frm_name-> Label( -text=>"50-75% load",
		   -height=>1,
		   -width=>15,
		   -foreground=>"blue",
		   -background=>"orange",)->grid(-row=>$frm_name_row++,-column=>1);
$frm_name-> Label( -text=>">75% Load",
		   -height=>1,
		   -width=>15,
		   -foreground=>"blue",
		   -background=>"red",)->grid(-row=>$frm_name_row++,-column=>1);





#buttons
my $startbutton = $frm_name -> Button(-text=> "Start",-command=>\&startprog)->grid(-row=>$frm_name_row++,-column=>1);
my $exitbutton = $frm_name -> Button(-text => "Quit",-command =>\&exitprog)->grid(-row=>$frm_name_row++,-column=>1);


MainLoop;

sub startprog{
    
#input file
    open(inputFile,"<","$filename") or die "can't open input file";
#read routers
    while($line = <inputFile>){
	#Topology
	if($line =~m/Topology ([a-zA-Z0-9]+)/){
	    $topology = $1;
	}
	#how many routers in the x and y direction
	if($line =~m/Network Width ([0-9]+)/){
	    $xrouters = $1;
	}
	if($line =~m/Network Height ([0-9]+)/){
	    $yrouters = $1;
	}
	#concenration per router
	if($line =~m/Concentration ([0-9]+)/){
	    $concentration = $1;
	}
	#how many nodes in the x y direction of a router
	if($line =~m/Router Width ([0-9]+)/){
	    $xnodes = $1;
	}
	if($line =~m/Router Height ([0-9]+)/){
	    $ynodes = $1;
	}
	#total amount of buffere space in a router
	if($line =~m/Total Storage ([0-9]+)/){
	    $total_storage = $1;
	}
	if($line =~m/Setup Finished Router/){
	    last;
	}
    }
    $totalrouters = $xrouters*$yrouters;
    $totalnodes = $totalrouters*$concentration;

    #size up the dimensions
    $routerwidth=($width/$xrouters/3);
    $routerheight=($height/$yrouters/3);
    $xspacer = (($width-$xrouters*$routerwidth)/($xrouters+1));
    $yspacer = (($height -$yrouters*$routerheight)/($yrouters+1));
    $nodewidth = ($routerwidth/3);
    $nodeheight= ($routerheight/3);
    
    #inital node drawing
    resetrandomshit();
    resetnodes();
    resetrouters();


    #unfortunately differently topology require different link parser
    if($topology eq "flatfly"){
	flatflygetlinks();
    } elsif($topology eq "cmesh"){
	cmeshgetlinks();
    }

    #inital link drawing
    resetrandomshit2();
    resetlinkload();
    resetlinkunload();

    #DRAW!
    drawrouters();
    drawnodes();
    drawlinks();
    drawbuffers();

    if($debug!=0){
	die;
    }

    #process flits
    while($line = <inputFile>){
	#Parse TIME and 
	#reset all the nodes and routers
	if($line=~m/TIME ([0-9]+)/){
	    $TIME = $1;
	    $timelabel->configure(-text=>"TIME $TIME");
	    $mw->update;
	    updateflitscount();
	    #draw order: injection, router, link, router, ejection
	    drawnodeinjection();
	    drawrouterinjection();
	    drawlinks();
	    updatelinkload();
	    drawlinks();
	    drawrouterejection();
	    drawnodeejection();
	    drawbuffers();
	    #rest for next cycle
	    resetlinkunload();
	    resetnodes();
	    resetrouters();
	}
	#set new flit flag, draw the node color
	if($line =~m/New Flit ([0-9]+)/){
	    $id = $1;
	    $nodehasinjection{"N$id"} = 1;
	    $flitstotalinjected++;
	    $flitsinjected20++;
	}
	#process router
	if($line =~m/Router ([0-9]+)/){
	    $router = $1;
	}
	#reset the input channel color
	if($line =~m/Input Channel ([0-9]+)/){
	    $inport = $1;
	    #new flit
	    if($inport<$concentration){
		$routerhasinjection{"R$router"} = 1;
	    } else {
		#record if a link reduces
		my $search = "R$router"."O$inport";
		my $realin = $input2link{$search};
		$linkunload{"L$realin"}++;
	    }
	}
	#we shade the colors by the amount of credit it has out
	#all subsequent parses uses this
	if($line =~m/Rload ([0-9]+)/){
	    my $temp = $1;
	    if($inport>=$concentration){
		my $search = "R$router"."O$inport";
		my $realin = $input2link{$search};
		$routerinputbuffer{"R$router"."L$realin"} = $temp;
	    }
	}
	#process link, intermediate router is yellow, exit is green
	#outpupt node is highlighted, but this is calculated which could 
	#cause a shitstore later
	
	if($line =~m/Outport ([0-9]+) *([0-9]*)/){
	    $outport = $1;
	    if($outport<$concentration){
		$routerhasejection{"R$router"} = 1;
		#calculate which node it is going to
		my $h = int($outport/$xnodes);
		my $g = $outport%$xnodes;
		my $y_index = int($router/$xrouters);
		my $x_index = $router%$xrouters;
		my $n = ($xrouters * $xnodes) * ($ynodes * $y_index + $h) + ($xnodes * $x_index + $g) ;	
		$nodehasejection{"N$n"} = 1;
		$flitstotalejected++;
		$flitsejected20++;
	    }
	    #update channel load
	    if($outport>=$concentration){
		my $search = "R$router"."O$outport";
		my $realout = $output2link{$search};
		$routeroutputbuffer{"R$router"."L$realout"}++;
		#we record the injection on to the channels
		    $linkinjection{"L$realout"}=1;
	    }
	}
	#clear board for the next flit
	if($line =~m/Stop Mark/){
	    $router = 0;
	    $id = 0;
	    $inport = 0;
	    $outport = 0;
	}
    }

    #print "finished file read\n";
    close(inputFile);
}

################################DRAWING UPDATE################################

sub drawnodeinjection{
    my $i = 0; 
    for($i = 0; $i<$totalnodes; $i++){
	if($nodehasinjection{"N$i"}!= 0){
	    drawnode($i,"indian red");
	} else {
	    drawnode($i,"grey85");
	}
    } 
    $mw->update;
    select(undef,undef,undef,$delaytime);   
}

sub drawnodeejection{
    my $i = 0; 
    for($i = 0; $i<$totalnodes; $i++){
	if($nodehasejection{"N$i"}!= 0){
	    drawnode($i,"lime green");
	} else {
	    drawnode($i,"grey85");
	}
    }  
    $mw->update;
    select(undef,undef,undef,$delaytime);   
}

sub drawrouterinjection{
    my $i = 0; 
    for($i=0; $i<$totalrouters; $i++){
	if($routerhasinjection{"R$i"}!= 0){
	    drawrouter($i,"grey85");
	} else {
	    drawrouter($i,"grey85");
	}
    }
    $mw->update;
    select(undef,undef,undef,$delaytime);   
}

sub drawrouterejection{
    my $i = 0; 
    for($i=0; $i<$totalrouters; $i++){
	if($routerhasejection{"R$i"}!= 0){
	    drawrouter($i,"grey85");
	} else {
	    drawrouter($i,"grey85");
	}
    }
    $mw->update;
    select(undef,undef,undef,$delaytime);   
}

sub drawlinks{
    my $i = 0; 
    for($i=0; $i<$totallinks; $i++){
	if($linkleng{"L$i"}!=0){ 
	    drawlink($i,"black");
	}
    }
    $mw->update;
    select(undef,undef,undef,$delaytime);   
}

############################DRAWING UPDATE################################


############################INDIVIDUAL DRAWER#############################

#drawing individual routers
#fill color is based on rload 
sub drawrouter{
    my $r = $_[0];
    my $color = $_[1];
    my $i = 0;
    my $rload =0;
    my $rtotal = $total_storage;
    #tally up totally how many packets are in a router
    for($i = 0; $i<$inputcount{"R$r"};$i++){
	my $templ = $i + $concentration;
	$l = $output2link{"R$r"."O$templ"};
	if($linkleng{"L$l"}!=0){
	    $rload+= $routeroutputbuffer{"R$r"."L$l"};
	}
    }    
    for($i=0; $i<$inputcount{"R$r"}; $i++){
	$templ = $i + $concentration;
	$l = $input2link{"R$r"."O$templ"};
	if($linkleng{"L$l"}!=0){
	    $rload += $routerinputbuffer{"R$r"."L$l"};
	}
    }

    my $grayscale = int((1-$rload/$rtotal)*100)-2;
    if($grayscale<0){
	$grayscale = 0;
    }
    my $fillcolor = "grey"."$grayscale";
    #print "$grayscale\n";
    
    #sweat jesus every item has an id
    my $id = $routerid{"R$r"};
    if($id == 0){
	my $x=$routerxone{"R$r"};
	my $y=$routeryone{"R$r"};
	$id = $cns ->createRectangle($x,$y,$x+$routerwidth,$y+$routerheight,-outline=>"$color", -width=>6,-fill=>"$fillcolor");
	$routerid{"R$r"} = $id;
    } else {
	$cns->itemconfigure($id,-outline=>"$color", -width=>$routerwidth*0.1,-fill=>"$fillcolor");
    }

}

sub drawbuffer{
    my $r = $_[0];
    my $i = 0;
    my $l = 0;
    my $templ;
    for($i=0; $i<$outputcount{"R$r"}; $i++){
	$templ = $i + $concentration;
	$l = $output2link{"R$r"."O$templ"};
	if($linkleng{"L$l"}!=0){
	    my $tempoutputbuffer = $routeroutputbuffer{"R$r"."L$l"};
	    my $bufferid = $routeroutputbufferid{"R$r"."B$i"};
	    if($bufferid == 0){
		my $linkid = $linkid{"L$l"};
		$x = $linksrcx{"L$l"};
		$y = $linksrcy{"L$l"};
		$routeroutputbufferid{"R$r"."B$i"} = $cns->createText($x,$y,-text=>"$tempoutputbuffer", -fill=>"blue");
	    } else {
		$cns->itemconfigure($bufferid,-text=>"$tempoutputbuffer");
	    }
	}
    }

    for($i=0; $i<$inputcount{"R$r"}; $i++){
	$templ = $i + $concentration;
	$l = $input2link{"R$r"."O$templ"};
	if($linkleng{"L$l"}!=0){
	    my $tempinputbuffer = $routerinputbuffer{"R$r"."L$l"};
	    my $bufferid = $routerinputbufferid{"R$r"."B$i"};
	    if($bufferid == 0){
		my $linkid = $linkid{"L$l"};
		$x = $linkdestx{"L$l"};
		$y = $linkdesty{"L$l"};
		$routerinputbufferid{"R$r"."B$i"} = $cns->createText($x,$y,-text=>"$tempinputbuffer", -fill=>"red");
	    } else {
		$cns->itemconfigure($bufferid,-text=>"$tempinputbuffer");
	    }
	}
    }
		
}

#drawing individual nodes
sub drawnode{
    my $n = $_[0];
    my $color = $_[1];
    my $r = $node2router{"N$n"};
    my $x = $node2routerx{"N$n"};
    my $y = $node2routery{"N$n"};
    
    #dear god every item has an id
    my $id = $nodeid{"N$n"};
    my $lid = $nodelinkid{"N$n"};
    if($id == 0){
	#center of the router
	my $routercenterx = $routerxone{"R$r"};
	my $routercentery = $routeryone{"R$r"};
	$routercenterx += $routerwidth/2;
	$routercentery += $routerheight/2;
	
	#determine which quadrant around the router
	my $dirx = 1;
	my $diry = 1;    
	if($x<int($xnodes/2)){
	    $dirx = -1;
	}
	if($y<int($ynodes/2)){
	    $diry = -1;
	}
	
	#find the right starting corner
	my $drawx = $routercenterx + $dirx*$routerwidth/2;
	my $drawy = $routercentery + $diry*$routerheight/2;
	#offset it by the routers already ther
	if($x<int($xnodes/2)){
	    $drawx += ($x-int($xnodes/2)+1)*$nodewidth;
	} else {
	    $drawx += ($x-int($xnodes/2))*$nodewidth;
	}
	if($y<int($ynodes/2)){
	    $drawy += ($y-int($ynodes/2)+1)*$nodeheight;
	} else {
	    $drawy += ($y-int($ynodes/2))*$nodeheight;
	}
	
	#draw link, determine the center of the nodes
	my $nodecenterx = $drawx+$dirx*$nodewidth/2;
	my $nodecentery = $drawy+$diry*$nodeheight/2;
	
	$nodeid{"N$n"} = $cns->createOval($drawx, $drawy, $drawx+$dirx*$nodewidth, $drawy+$diry*$nodeheight, -fill=>"$color", -width=>2.0);
	$nodelinkid{"N$n"} = $cns->createLine($drawx, $drawy, $nodecenterx, $nodecentery, -fill=>"$color",-width=>3.0);
    } else {
	$cns->itemconfigure($id,-fill=>"$color", -width=>2.0);
	$cns->itemconfigure($lid,-fill=>"$color",-width=>3.0);
    }
}


sub drawlink{
    my $output = $_[0];
    my $color = $_[1];
    
    my $x1;
    my $x2; 
    my $y1;
    my $y2;
    my $hori;
    my $vert;

    #print "output port is $output\n";
    my $src = $linksrc{"L$output"};
    my $dest = $linkdest{"L$output"};
    my $leng = $linkleng{"L$output"};

    #color depend on color average
    my $load = $linkload{"L$output"};
    if($TIME!=0){
	if($load/$TIME>0.75){
	    $color = "red";
	} elsif($load/$TIME>0.25 && $load/$TIME<=0.50){
	    $color = "yellow";
	}elsif($load/$TIME>0.50 && $load/$TIME<=0.75){
	    $color = "orange";
	} else {
	    $color = "green";
	}
    } else {
	$color = "green";
    }
    
    #firt draw or are we just modifying the color?
    my $id = $linkid{"L$output"};
    if($id == 0){
	#how far the links are spaced
	my $offsetscale = ($routerwidth*.8)/2/$lengmax;
	
	my $xoffset = $leng*$offsetscale;
	my $yoffset = $leng*$offsetscale;
	#decide between vertical link or horizontal
	if(int($src/$xrouters) == int($dest/$xrouters)){
	    $hori = 1;
	    $vert = -1;
	    $xoffset = $routerwidth/2;
	    #oh this sillyness correct the spacing betweent he channels
	    if($xnodes>$ynodes){
	    } else {
		$yoffset = $yoffset*$ynodes/$xnodes;
	    }
	    if($src>$dest){
		$xoffset = -$routerwidth/2;
		$yoffset = -1*$yoffset;
	    }
	} else {
	    $hori = -1;
	    $vert = 1;
	    $yoffset = $routerheight/2;
	    #oh this sillyness correct the spacing betweent he channels
	    if($xnodes>$ynodes){
		$xoffset = $xoffset*$xnodes/$ynodes;
	    } else {
	    }
	    if($src>$dest){
		$yoffset = -$routerheight/2;
		$xoffset = -1*$xoffset;
	    }
	}
	my $srcx = $src%$xrouters;
	my $srcy = int($src/$xrouters);
	my $destx = $dest%$xrouters;
	my $desty = int($dest/$xrouters);
	#offset from the router 2 terms + space out around the router + offseting to the router outline
	$x1 = ($srcx+1)*$xspacer + $srcx*$routerwidth + $xoffset + $routerwidth/2 ;
	$y1 = ($srcy+1)*$yspacer + $srcy*$routerheight + $yoffset + $routerheight/2 ;
	$x2 = ($destx+1)*$xspacer + $destx*$routerwidth + $xoffset*$vert + $routerwidth/2;
	$y2 = ($desty+1)*$yspacer + $desty*$routerheight + $yoffset*$hori + $routerheight/2;

	$linksrcx{"L$output"}=$x1;
	$linksrcy{"L$output"}=$y1;
	$linkdestx{"L$output"}=$x2;
	$linkdesty{"L$output"}=$y2;
	$linkid{"L$output"} = $cns->createLine($x1,$y1,$x2,$y2,-arrow=>"last",-fill=>"$color",-width=>2.0);
    } else {
	$cns->itemconfigure($id,-arrow=>"last",-fill=>"$color",-width=>2.0);
    }
    
    #draw boxes
    my $i = 0;
    my $queue = $linkqueue{"L$output"};
    my $queuecolor;
    my $queueid;

    for($i = 0; $i<$leng;$i++){
	#determine the color to draw the box
	if(substr($queue,$i,1) eq "_"){
	    $queuecolor = "grey85";
	 } elsif (substr($queue,$i,1) eq "-"){
	     $queuecolor = "pink";
	 } else {
	     $queuecolor = "black";
	 }
	#draw box
	$queueid = $linkqueueid{"L$output"."Q$i"};
	my $xcenter = $x1;
	my $ycenter = $y1;
	#find the center point of the box
	if($queueid == 0){
	    if($vert == 1){
		$xcenter = ($x2-$x1)*($i+1)/($leng+1)+$x1;
		$ycenter = ($y2-$y1)*($i+1)/($leng+1)+$y1;
		if($src>$dest){
		    $xcenter = ($x1-$x2)*($i+1)/($leng+1)+$x2;
		    $ycenter = ($y1-$y2)*($i+1)/($leng+1)+$y2;
		}
	    } else {
		$ycenter = ($y2-$y1)*($i+1)/($leng+1)+$y1;
		$xcenter = ($x2-$x1)*($i+1)/($leng+1)+$x1;
		if($src>$dest){
		    $ycenter = ($y1-$y2)*($i+1)/($leng+1)+$y2;
		    $xcenter = ($x1-$x2)*($i+1)/($leng+1)+$x2;
		}
	    }
	    #calculate the boundry of the box
	    my $qx1 = $xcenter+$boxwidth/2;
	    my $qx2 = $xcenter-$boxwidth/2;
	    my $qy1 = $ycenter+$boxheight/2;
	    my $qy2 = $ycenter-$boxheight/2;
	    $linkqueueid{"L$output"."Q$i"} = $cns->createRectangle($qx1,$qy1,$qx2,$qy2,-fill=>"$queuecolor",-outline=>"$queuecolor");
	} else {
	    $cns->itemconfigure($queueid,-fill=>"$queuecolor",-outline=>"$queuecolor");
	}
    }
}
################################INDIVIDUAL DRAWER##########################

sub exitprog{
    exit;
}

################################RESET################################
sub resetnodes{
    my $i = 0;
    for($i = 0; $i<$totalnodes; $i++){
	$nodehasinjection{"N$i"} = 0;
	$nodehasejection{"N$i"} = 0;
    }
}
sub resetrouters{
    my $i = 0;
    for($i = 0; $i<$totalrouters; $i++){
	$routerhasinjection{"R$i"} = 0;
	$routerhasejection{"R$i"} = 0;
    }
}
sub resetlinkunload{
    my $i = 0;
    for($i = 0; $i<$totallinks; $i++){
	$linkunload{"L$i"} = 0;
	$linkinjection{"L$i"} = 0;
    }
}
sub resetlinkload{
    my $i = 0;
    for($i = 0; $i<$totallinks; $i++){
	$linkload{"L$i"} = 0;
    }
}

sub resetrandomshit{
   my $i = 0;
    for($i = 0; $i<$totalrouters; $i++){
	$routerid{"R$i"} = 0 ;
	$outputcount{"R$i"} = 0;
	$inputcount{"R$i"} = 0;
    }
   for($i = 0; $i<$totalnodes; $i++){
       $nodeid{"N$i"} = 0;
       $nodelinkid{"N$i"} = 0;
   }
}

sub resetrandomshit2{
    my $i = 0;
    my $j = 0;
    for($i = 0; $i<$totallinks; $i++){
	$linkid{"L$i"} = 0;
	if($linkleng{"L$i"}!=0){
	    $linkqueue{"L$i"}="";
	    for($j = 0; $j<$linkleng{"L$i"}; $j++){
		$linkqueue{"L$i"}=$linkqueue{"L$i"}."_";
		$linkqueueid{"L$i"."Q$j"}=0;
	    }
	}
    } 
}

sub updateflitscount{
    if($TIME%20 == 0){
	$flitstotalinjectedid->configure(-text=>"Total: $flitstotalinjected");
	$flitstotalejectedid->configure(-text=>"Total: $flitstotalejected");
	$flitsinjected20id->configure(-text=>"Last 20c: $flitsinjected20");
	$flitsejected20id->configure(-text=>"Last 20c: $flitsejected20");
	$flitsinjected20 = 0;
	$flitsejected20 = 0;
    }
    $frm_name->update;
}

sub updatelinkload{
    my $i = 0;
    my $r = 0;
    for($i = 0; $i<$totallinks; $i++){
	if($linkleng{"L$i"}!=0){
	    #update the buffer of the thing
	    if($linkunload{"L$i"}!=0){
		$linkload{"L$i"}++;
		$r = $linksrc{"L$i"};
		$routeroutputbuffer{"R$r"."L$i"}--;
	    }
	    #we remove a packet
	    #if we injected a packet, then add a 0 or else _
	    chop $linkqueue{"L$i"};
	    if($linkinjection{"L$i"} == 1){
		$linkqueue{"L$i"} = "0".$linkqueue{"L$i"};
	    #for possible marking of nonminimal packets, so far no luck
	    } elsif ($linkinjection{"L$i"} == -1){
		$linkqueue{"L$i"} = "-".$linkqueue{"L$i"};
	    } else {
		$linkqueue{"L$i"} = "_".$linkqueue{"L$i"};
	    }
	}
    }  
}
################################RESET################################


################################INTIALIZATION################################

#initialization drawing the whole board
sub drawrouters{
    my $i = 0; 
    my $j = 0;
    my $r = 0;
    for($i = 0; $i<$yrouters;$i+=1){
	for($j = 0; $j<$xrouters;$j+=1){
	    my $x = ($j+1)*$xspacer + $j*$routerwidth;
	    my $y = ($i+1)*$yspacer + $i*$routerheight;
	    $routerxone{"R$r"}=$x;
	    $routeryone{"R$r"}=$y;
	    drawrouter($r,"grey85");
	    $r++;
	}
    } 
}

sub drawbuffers{
    my $i = 0; 
    if($TIME%5==0){
	for($i = 0; $i<$totalrouters;$i+=1){
	    drawbuffer($i);
	}
    }
}

sub drawnodes{
    my $i = 0; 
    my $j = 0;
    my $r = 0;
    my $h = 0;
    my $g = 0;
    for($i = 0; $i<$yrouters;$i+=1){
	for($j = 0; $j<$xrouters;$j+=1){
	    #assign nodes to a router
	    for($h = 0; $h<$ynodes; $h+=1){
		for($g = 0; $g<$xnodes; $g+=1){
		    my $y_index = int($r/$xrouters);
		    my $x_index = $r%$xrouters;
		    $n = ($xrouters * $xnodes) * ($ynodes * $y_index + $h) + ($xnodes * $x_index + $g) ;	
		    $node2router{"N$n"}=$r;
		    $node2routerx{"N$n"} = $g;
		    $node2routery{"N$n"} = $h;
		    drawnode($n,"grey85");
		}
	    }
	    $r++;
	}
    } 
}

################################INTIALIZATIOn################################



################################READ LINK################################
sub flatflygetlinks(){

    #read links
    while($line = <inputFile>){
	if($line =~m/Link ([0-9]+) ([0-9]+) ([0-9]+) ([0-9]+)/){
	    $link = $1;
	    $linksrc{"L$link"} = $2;
	    $linkdest{"L$link"} = $3;
	    $linkleng{"L$link"} = $4;
	    if($linkleng{"L$link"}>$lengmax){
		$lengmax = $linkleng{"L$link"};
	    }
	    #routing only gives outport and input port
	    #need to convert that into these links
	    my $temproutersrc = $linksrc{"L$link"};
	    my $tempoutputcount = $outputcount{"R$temproutersrc"}+$concentration;
	    my $buff = $outputcount{"R$temproutersrc"};
	    $routeroutputbufferid{"R$temproutersrc"."B$buff"} = 0;
	    $routeroutputbuffer{"R$temproutersrc"."L$link"} = 0;
	    $output2link{"R$temproutersrc"."O$tempoutputcount"} = $link;
	    $outputcount{"R$temproutersrc"}++;
	    #print "adding output $tempoutputcount to router $temproutersrc\n";

	    my $temprouterdest = $linkdest{"L$link"};
	    my $tempinputcount = $inputcount{"R$temprouterdest"}+$concentration;
	    $buff = $inputcount{"R$temprouterdest"};
	    $routerinputbufferid{"R$temprouterdest"."B$buff"} = 0;
	    $routerinputbuffer{"R$temprouterdest"."L$link"} = 0;

	    $input2link{"R$temprouterdest"."O$tempinputcount"} = $link;
	    $inputcount{"R$temprouterdest"}++;
	    $totallinks++;
	    #print "adding input $tempinputcount to router $temprouterdest\n";
	}
	if($line =~m/Setup Finished Link/){
	    last;
	}
    }
}

sub cmeshgetlinks(){
    #read links
    while($line = <inputFile>){
	#outputlink inputlink node length
	if($line =~m/Link *([0-9]+) ([0-9]+) ([0-9]+) ([0-9]+)/){
	    my $outputlink = $1;
	    my $inputlink = $2;
	    my $node = $3;
	    my $checkleng = $4;

	    $linksrc{"L$outputlink"} = $node;
	    $linkdest{"L$inputlink"} = $node;
	    $linkleng{"L$outputlink"} = $checkleng;
	    if($linkleng{"L$outputlink"}>$lengmax){
		$lengmax = $linkleng{"L$outputlink"};
	    }
	    #routing only gives outport and input port
	    #need to convert that into these links
	    my $temproutersrc = $linksrc{"L$outputlink"};
	    my $tempoutputcount = $outputcount{"R$temproutersrc"}+$concentration;
	    my $buff = $outputcount{"R$temproutersrc"};
	    $routeroutputbufferid{"R$temproutersrc"."B$buff"} = 0;
	    $routeroutputbuffer{"R$temproutersrc"."L$outputlink"} = 0;
	    $output2link{"R$temproutersrc"."O$tempoutputcount"} = $outputlink;
	    $outputcount{"R$temproutersrc"}++;
	    #print "adding output $tempoutputcount to router $temproutersrc\n";

	    my $temprouterdest = $linkdest{"L$inputlink"};
	    my $tempinputcount = $inputcount{"R$temprouterdest"}+$concentration;
	    $buff = $inputcount{"R$temprouterdest"};
	    $routerinputbufferid{"R$temprouterdest"."B$buff"} = 0;
	    $routerinputbuffer{"R$temprouterdest"."L$inputlink"} = 0;	  
	    $input2link{"R$temprouterdest"."O$tempinputcount"} = $inputlink;
	    $inputcount{"R$temprouterdest"}++;
	    $totallinks++;
	    #print "adding input $tempinputcount to router $temprouterdest\n";
	}
	if($line =~m/Setup Finished Link/){
	    last;
	}
    }
}


################################READ LINK################################
