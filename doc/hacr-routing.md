# History-Aware Congestion Routing (HACR)

HACR is an experimental minimal-adaptive routing extension for BookSim 2. It ranks eligible productive output ports using current router credit occupancy and a smoothed history for each router/port pair.

## Routing score

For port p, U_p is the current used-credit measurement and H_p is an exponentially smoothed value:

~~~text
H_p(t) = lambda * H_p(t - 1) + (1 - lambda) * U_p(t)
S_p(t) = alpha * U_p(t) + (1 - alpha) * H_p(t)
~~~

The implementation chooses the eligible port with the smallest score. Exact ties are resolved by output-port number to make that decision deterministic. The default parameters are hacr_alpha = 0.60 and hacr_lambda = 0.80. Both parameters must lie in [0, 1].

## Available functions

| Topology | Routing function |
| --- | --- |
| Mesh | hacr_min_adapt_mesh |
| Torus | hacr_min_adapt_torus |

The implementation retains a deterministic escape route. Mesh and Torus use different VC arrangements; the Torus variant retains the Torus-specific escape phase and follows the existing minimal-adaptive routing style. Do not change the VC count or VC assignments without checking channel-dependency constraints and rerunning correctness tests.

## Example runs

From the repository root, after building BookSim:

~~~sh
make -C src -j2
./src/booksim runfiles/hacrmeshconfig
./src/booksim runfiles/hacrtorusconfig
~~~

The supplied configurations are low-load smoke-test examples, not benchmark results. Change traffic and injection rate by editing a copied configuration or using BookSim's supported command-line overrides.

## Important limitations

- The congestion history is stored in process-global state keyed by router ID and output port. It is cleared when the routing map is initialized.
- History is updated when the routing function evaluates a candidate; this is not a separate per-cycle congestion monitor.
- A successful build or smoke test is not a formal proof of deadlock freedom. Escape routing and VC constraints require careful review for any new configuration or topology.
- The current prototype has not yet been fully validated across all router organizations, packet classes, VC partitions, or multiple simultaneous networks.
- No universal performance improvement is claimed. Evaluate it against topology-appropriate baselines across traffic patterns and offered loads.
