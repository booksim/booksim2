The "anynet" option in booksim2.0 gives you some ability setup arbitrary topology. Attached is a config file and a listing file for the "anynet". The listing file describes the connections between modules in the network (routers and nodes). For example, the first line of the file
"router 0 node 0 node 1 node 2 router 1"
means router0 is connected to node0, node1 node2, and router1. This also implies that router1 is connected to router 0, so on second line of the listing file, which describes the connections to router1, router0 can be omitted. There should be no restriction on the numbering of router and nodes, as long as they are unique. The only restriction is that a node can only be connected to a single router. 

After booksim parses and builds the network from the listing file, it builds a routing table in each router, which describes the minimal path between any two nodes. As a result there is no path diversity, there is a single path between any two nodes in the network. Of course you can change this by writing your own routing function. 

When you run booksim with these config files, it will print out a bunch of information on the connectivity and routing of the network. Check to makes sure it is what you expect. 

This feature is very experimental, be careful when you are using it, and double check to make sure the results is what you expected.

It has been about 2 years since I last used this, so definitely becareful. If your topology is regular, I recommend using it as a topology class instead of using anynet. 


====================================


 I just made a major change to anynet. In addition to better pathfinding and parser.  Now it recognize link weights. The new format is 

Router 0 Router 1 10 Router 2 5

Router 0 is connected to router 1 with a 10-cycle channel and router 2 with a 5-cycle channel. If link latency is not present it assumes single cycle channel.

Also the channel latency specification between routers are not bi-directional. In the example above, the channel from router 1 back to router 0 is single-cycle because it was not explicitly specified.